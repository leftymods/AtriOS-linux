// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic USB Burning Tool ("v2" burn protocol) gadget.
 *
 * The vendor BSP implements this in bl33/drivers/usb/gadget/v2_burning on top
 * of its own DWC2 driver (v2_usb_tool/{usb_pcd,dwc_pcd}.c).  This port keeps
 * the protocol (descriptors, vendor requests, command strings, checksums,
 * 64 KiB packet handling) but runs it on the mainline gadget framework:
 * the DWC2 UDC driver (drivers/usb/gadget/dwc2_udc_otg.c) plus a plain
 * struct usb_gadget_driver, so no vendor arch/asm or mailbox code is needed.
 *
 * Protocol description: mainline/docs/uboot-burn.md
 * Port notes / supported commands: mainline/docs/uboot-burn-port.md
 *
 * Deliberately NOT ported: the run_command() fallback of the vendor
 * optimus_working() (that turns the USB port into a remote shell), the AMLC
 * BootROM path (0x50/0x60, 0x01/0x02/0x05 memory pokes), key/efuse access,
 * NAND/SD-Card/SPI media, and upload (0x33) of anything but memory and
 * store partitions.
 */

#include <command.h>
#include <dm.h>
#include <dm/uclass.h>
#include <env.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <stdarg.h>
#include <linux/string.h>
#include <linux/printk.h>
#include <malloc.h>
#include <memalign.h>
#include <stdio.h>
#include <time.h>
#include <vsprintf.h>
#include <asm/byteorder.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <sysreset.h>

#include <aml_burn.h>

#include "aml_burn_proto.h"

#define AML_BURN_DEFAULT_CONNECT_MS	5000
#define AML_BURN_EXIT_DELAY_MS		300

/*
 * "Continue:32" keepalive for slow media.  The vendor gadget sends it from a
 * background timer (16 s / 8 MiB); this port writes synchronously from the
 * main loop, so the ack is sent *before* a packet that is expected to be slow
 * (the previous write took longer than SLOW_MS).  Every Continue costs the
 * host a re-read (pyamlboot even sleeps 3 s), hence the two rate limits.
 */
#define AML_BURN_CONTINUE_SLOW_MS	1000
#define AML_BURN_CONTINUE_MIN_MS	1500
#define AML_BURN_CONTINUE_MIN_BYTES	(8 * 1024 * 1024)
#define AML_BURN_CONTINUE_URGENT_MS	6000

/* ------------------------------------------------------------------ */
/* Descriptors								*/
/* ------------------------------------------------------------------ */

static const struct usb_device_descriptor aml_device_desc = {
	.bLength		= USB_DT_DEVICE_SIZE,
	.bDescriptorType	= USB_DT_DEVICE,
	.bcdUSB			= __constant_cpu_to_le16(AML_BURN_BCD_USB),
	.bDeviceClass		= USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass	= 0,
	.bDeviceProtocol	= 0,
	.bMaxPacketSize0	= AML_BURN_EP0_MPS,
	.idVendor		= __constant_cpu_to_le16(AML_BURN_VID),
	.idProduct		= __constant_cpu_to_le16(AML_BURN_PID),
	.bcdDevice		= __constant_cpu_to_le16(AML_BURN_BCD_DEVICE),
	.iManufacturer		= 0,
	.iProduct		= 0,
	.iSerialNumber		= 0,
	.bNumConfigurations	= 1,
};

struct aml_burn_config {
	struct usb_config_descriptor config;
	struct usb_interface_descriptor intf;
	struct usb_endpoint_descriptor ep_in;
	struct usb_endpoint_descriptor ep_out;
} __packed;

#define AML_BURN_CONFIG(_mps)						\
	{								\
		.config = {						\
			.bLength		= USB_DT_CONFIG_SIZE,	\
			.bDescriptorType	= USB_DT_CONFIG,	\
			.wTotalLength		=			\
				__constant_cpu_to_le16(sizeof(struct aml_burn_config)), \
			.bNumInterfaces		= 1,			\
			.bConfigurationValue	= 1,			\
			.iConfiguration		= 0,			\
			.bmAttributes		= USB_CONFIG_ATT_ONE |	\
						  USB_CONFIG_ATT_SELFPOWER, \
			.bMaxPower		= 1,			\
		},							\
		.intf = {						\
			.bLength		= USB_DT_INTERFACE_SIZE, \
			.bDescriptorType	= USB_DT_INTERFACE,	\
			.bInterfaceNumber	= 0,			\
			.bAlternateSetting	= 0,			\
			.bNumEndpoints		= 2,			\
			.bInterfaceClass	= 0xff,			\
			.bInterfaceSubClass	= 0,			\
			.bInterfaceProtocol	= 0,			\
			.iInterface		= 0,			\
		},							\
		.ep_in = {						\
			.bLength		= USB_DT_ENDPOINT_SIZE,	\
			.bDescriptorType	= USB_DT_ENDPOINT,	\
			.bEndpointAddress	= AML_BURN_BULK_EP_IN,	\
			.bmAttributes		= USB_ENDPOINT_XFER_BULK, \
			.wMaxPacketSize		=			\
				__constant_cpu_to_le16(_mps),		\
			.bInterval		= 0,			\
		},							\
		.ep_out = {						\
			.bLength		= USB_DT_ENDPOINT_SIZE,	\
			.bDescriptorType	= USB_DT_ENDPOINT,	\
			.bEndpointAddress	= AML_BURN_BULK_EP_OUT,	\
			.bmAttributes		= USB_ENDPOINT_XFER_BULK, \
			.wMaxPacketSize		=			\
				__constant_cpu_to_le16(_mps),		\
			.bInterval		= 0,			\
		},							\
	}

static const struct aml_burn_config aml_config_hs = AML_BURN_CONFIG(AML_BURN_BULK_MPS_HS);
static const struct aml_burn_config aml_config_fs = AML_BURN_CONFIG(AML_BURN_BULK_MPS_FS);

/* Endpoint descriptors handed to usb_ep_enable(); the UDC keeps the pointer. */
#define AML_BURN_EP_IN(_mps) {						\
	.bLength		= USB_DT_ENDPOINT_SIZE,			\
	.bDescriptorType	= USB_DT_ENDPOINT,			\
	.bEndpointAddress	= AML_BURN_BULK_EP_IN,			\
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,		\
	.wMaxPacketSize		= __constant_cpu_to_le16(_mps),		\
	.bInterval		= 0,					\
}

#define AML_BURN_EP_OUT(_mps) {						\
	.bLength		= USB_DT_ENDPOINT_SIZE,			\
	.bDescriptorType	= USB_DT_ENDPOINT,			\
	.bEndpointAddress	= AML_BURN_BULK_EP_OUT,			\
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,		\
	.wMaxPacketSize		= __constant_cpu_to_le16(_mps),		\
	.bInterval		= 0,					\
}

static struct usb_endpoint_descriptor aml_ep_in_hs = AML_BURN_EP_IN(AML_BURN_BULK_MPS_HS);
static struct usb_endpoint_descriptor aml_ep_out_hs = AML_BURN_EP_OUT(AML_BURN_BULK_MPS_HS);
static struct usb_endpoint_descriptor aml_ep_in_fs = AML_BURN_EP_IN(AML_BURN_BULK_MPS_FS);
static struct usb_endpoint_descriptor aml_ep_out_fs = AML_BURN_EP_OUT(AML_BURN_BULK_MPS_FS);

/*
 * The tool does not ask for strings (all string indices are 0), but some
 * hosts still request string 0 and Windows may probe 1..3.
 */
static const u8 aml_string_lang[] = { 4, USB_DT_STRING, 0x09, 0x04 };
static const u8 aml_string_manuf[] = {
	16, USB_DT_STRING, 'A', 0, 'm', 0, 'l', 0, 'o', 0, 'g', 0, 'i', 0, 'c', 0,
};

static const u8 aml_string_product[] = {
	16, USB_DT_STRING, 'M', 0, '8', 0, '-', 0, 'C', 0, 'H', 0, 'I', 0, 'P', 0,
};

static const u8 aml_string_serial[] = {
	18, USB_DT_STRING, '2', 0, '0', 0, '1', 0, '1', 0,
	'0', 0, '4', 0, '0', 0, '6', 0,
};

/* ------------------------------------------------------------------ */
/* Context								*/
/* ------------------------------------------------------------------ */

struct aml_burn {
	struct usb_gadget *gadget;
	struct usb_gadget_driver driver;

	struct usb_ep *in_ep;
	struct usb_ep *out_ep;
	struct usb_request *ep0_req;
	struct usb_request *in_req;
	struct usb_request *out_req;

	u8 *ep0_buf;		/* 512 B, EP0 data stage / command strings */
	u8 *out_buf;		/* 64 KiB, bulk OUT slot */
	u8 *in_buf;		/* 512 B, bulk IN replies */
	u8 result[AML_BURN_REPLY_SZ];

	bool running;
	bool configured;
	bool enumerated;
	bool identified;
	bool identify_window;

	unsigned int connect_ms;
	unsigned int identify_ms;
	unsigned long connect_start;
	unsigned long identify_start;

	/* last EP0 request presented by the host */
	u8 ep0_request;
	u8 ep0_type;
	u16 ep0_value;
	u16 ep0_index;
	bool ep0_out_ready;
	u32 ep0_out_actual;

	/* pending bulk OUT (0x32 data packet) */
	bool out_ready;
	u32 out_len;
	u32 out_seq;
	u32 out_sum;
	u16 out_alg;
	u16 out_ack;

	/* pending bulk IN reply */
	bool in_done;
	bool exit_after_reply;
	unsigned long exit_deadline;

	/* 0x32 slow media keepalive, sent as a second bulk IN request */
	struct usb_request *cont_req;
	u8 *cont_buf;		/* AML_BURN_REPLY_SZ */
	unsigned long dl_cont_ms;
	u64 dl_bytes_since_cont;
	u32 dl_last_write_ms;

	/* 0x33 upload */
	struct aml_burn_upload up;
	bool up_xfer;		/* bulk IN of the current chunk is in flight */
	bool up_ep0_done;	/* the 0x33 setup data stage is complete */
	u32 up_chunk;		/* length announced in the 0x33 reply */

	int action;
	struct aml_burn_img img;
};

enum {
	AML_ACT_NONE = 0,
	AML_ACT_RESET,
	AML_ACT_POWEROFF,
};

static struct aml_burn burn;

/* ------------------------------------------------------------------ */
/* Helpers								*/
/* ------------------------------------------------------------------ */

u32 aml_burn_add_sum(const void *data, u32 len)
{
	const u8 *p = data;
	u32 sum = 0;
	u32 i;

	for (i = 0; i + 4 <= len; i += 4) {
		u32 word;

		memcpy(&word, p + i, sizeof(word));
		sum += word;
	}

	if (i < len) {
		u32 word = 0;

		/* 1..3 trailing bytes, little endian, zero padded */
		memcpy(&word, p + i, len - i);
		sum += word;
	}

	return sum;
}

static int aml_split(char *line, char *argv[], int maxargs)
{
	int argc = 0;
	char *p = line;

	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if (!*p)
			break;
		if (argc == maxargs)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\r' &&
		       *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}

	return argc;
}

static struct usb_ep *aml_find_ep(struct usb_gadget *gadget, const char *name)
{
	struct usb_ep *ep;

	list_for_each_entry(ep, &gadget->ep_list, ep_list) {
		if (!strcmp(ep->name, name))
			return ep;
	}

	return NULL;
}

static struct usb_ep *aml_autoconfig_ep(struct usb_gadget *gadget, u8 addr)
{
	struct usb_endpoint_descriptor desc = {
		.bLength = USB_DT_ENDPOINT_SIZE,
		.bDescriptorType = USB_DT_ENDPOINT,
		.bEndpointAddress = addr & USB_DIR_IN,
		.bmAttributes = USB_ENDPOINT_XFER_BULK,
		.wMaxPacketSize = __constant_cpu_to_le16(AML_BURN_BULK_MPS_HS),
	};
	struct usb_ep *ep = usb_ep_autoconfig(gadget, &desc);

	if (ep && desc.bEndpointAddress != addr) {
		printf("aml_burn: autoconfig gave EP 0x%02x, need 0x%02x\n",
		       desc.bEndpointAddress, addr);
		return NULL;
	}

	return ep;
}

/* ------------------------------------------------------------------ */
/* Gadget driver							*/
/* ------------------------------------------------------------------ */

static void aml_bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct aml_burn *b = &burn;

	if (req->status)
		printf("aml_burn: bulk IN status %d\n", req->status);

	/* Data of an upload chunk (0x33), not a command reply. */
	if (b->up_xfer) {
		b->up_xfer = false;
		if (req->status) {
			printf("aml_burn: upload aborted (%d)\n", req->status);
			b->up.active = false;
			return;
		}
		b->up.done += req->actual;
		if (b->up.done >= b->up.total) {
			printf("aml_burn: upload complete (0x%llx bytes)\n",
			       (unsigned long long)b->up.done);
			b->up.active = false;
		}
		return;
	}

	b->in_done = true;

	if (b->exit_after_reply)
		b->exit_deadline = get_timer(0) + AML_BURN_EXIT_DELAY_MS;
}

static void aml_bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	burn.out_ready = true;
}

static void aml_continue_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status)
		printf("aml_burn: Continue:32 status %d\n", req->status);
}

/*
 * Queue "Continue:32" ahead of the "OK!!" of a packet that is about to be
 * written: the host reads both (the vendor sends the same two acks, one from
 * its busy timer and one after the media write).
 */
static int aml_continue_reply(struct aml_burn *b)
{
	u32 len = min_t(u32, b->out_ack, AML_BURN_REPLY_SZ);

	memset(b->cont_buf, 0, AML_BURN_REPLY_SZ);
	memcpy(b->cont_buf, "Continue:32", 11);

	b->cont_req->buf = b->cont_buf;
	b->cont_req->length = len;
	b->cont_req->zero = 0;
	b->cont_req->complete = aml_continue_complete;

	if (usb_ep_queue(b->in_ep, b->cont_req, GFP_ATOMIC)) {
		printf("aml_burn: Continue:32 queue failed\n");
		return -EIO;
	}

	return 0;
}

static int aml_bulk_reply(const void *msg, u32 len)
{
	struct aml_burn *b = &burn;
	u32 n = min_t(u32, len, AML_BURN_REPLY_SZ);

	if (b->up_xfer) {
		printf("aml_burn: reply while an upload chunk is in flight\n");
		return -EBUSY;
	}

	memset(b->in_buf, 0, AML_BURN_REPLY_SZ);
	if (msg && n)
		memcpy(b->in_buf, msg, n);

	b->in_done = false;
	b->in_req->buf = b->in_buf;
	b->in_req->length = len > AML_BURN_REPLY_SZ ? AML_BURN_REPLY_SZ : len;
	b->in_req->zero = 0;
	b->in_req->complete = aml_bulk_in_complete;

	if (usb_ep_queue(b->in_ep, b->in_req, GFP_ATOMIC)) {
		printf("aml_burn: bulk IN queue failed\n");
		return -EIO;
	}

	return 0;
}

static void aml_fail_reply(const char *fmt, ...) __printf(1, 2);

static void aml_fail_reply(const char *fmt, ...)
{
	char msg[AML_BURN_REPLY_SZ];
	va_list args;

	memset(msg, 0, sizeof(msg));
	memcpy(msg, "failed:", 7);
	va_start(args, fmt);
	vsnprintf(msg + 7, sizeof(msg) - 7, fmt, args);
	va_end(args);

	printf("aml_burn: %s\n", msg);
	aml_bulk_reply(msg, AML_BURN_REPLY_SZ);
}

static int aml_set_configuration(struct aml_burn *b, u16 value)
{
	bool hs;
	int ret;

	if (!value) {
		usb_ep_disable(b->in_ep);
		usb_ep_disable(b->out_ep);
		b->configured = false;
		return 0;
	}

	if (value != 1)
		return -EINVAL;

	if (b->configured)
		return 0;

	hs = b->gadget->speed >= USB_SPEED_HIGH;

	/*
	 * Disable first: after a bus reset the UDC has deactivated the
	 * endpoints but usb_ep.enabled is still set.
	 */
	usb_ep_disable(b->in_ep);
	usb_ep_disable(b->out_ep);
	b->out_ready = false;
	b->in_done = false;
	b->up_xfer = false;
	b->up_ep0_done = false;
	b->up.active = false;

	ret = usb_ep_enable(b->in_ep, hs ? &aml_ep_in_hs : &aml_ep_in_fs);
	if (ret) {
		printf("aml_burn: cannot enable EP IN (%d)\n", ret);
		return ret;
	}

	ret = usb_ep_enable(b->out_ep, hs ? &aml_ep_out_hs : &aml_ep_out_fs);
	if (ret) {
		printf("aml_burn: cannot enable EP OUT (%d)\n", ret);
		usb_ep_disable(b->in_ep);
		return ret;
	}

	b->configured = true;
	b->identify_start = get_timer(0);
	b->identify_window = true;
	printf("aml_burn: configured (%s speed, EP 0x%02x/0x%02x)\n",
	       hs ? "high" : "full", AML_BURN_BULK_EP_IN,
	       AML_BURN_BULK_EP_OUT);

	return 0;
}

static int aml_standard_request(struct aml_burn *b,
				const struct usb_ctrlrequest *ctrl,
				u16 w_length, u16 w_value)
{
	switch (ctrl->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		u8 type = w_value >> 8;

		if (!(ctrl->bRequestType & USB_DIR_IN))
			return -EOPNOTSUPP;

		switch (type) {
		case USB_DT_DEVICE:
			memcpy(b->ep0_buf, &aml_device_desc,
			       sizeof(aml_device_desc));
			return min_t(u16, w_length,
				     sizeof(aml_device_desc));
		case USB_DT_CONFIG:
		case USB_DT_OTHER_SPEED_CONFIG: {
			const struct aml_burn_config *cfg;
			bool hs = b->gadget->speed >= USB_SPEED_HIGH;

			if (type == USB_DT_OTHER_SPEED_CONFIG)
				hs = !hs;
			cfg = hs ? &aml_config_hs : &aml_config_fs;
			memcpy(b->ep0_buf, cfg, sizeof(*cfg));
			return min_t(u16, w_length, sizeof(*cfg));
		}
		case USB_DT_STRING: {
			const u8 *str;
			u8 sz;

			switch (w_value & 0xff) {
			case 0:
				str = aml_string_lang;
				break;
			case 1:
				str = aml_string_manuf;
				break;
			case 2:
				str = aml_string_product;
				break;
			case 3:
				str = aml_string_serial;
				break;
			default:
				return -EOPNOTSUPP;
			}
			sz = str[0];
			memcpy(b->ep0_buf, str, sz);
			return min_t(u16, w_length, sz);
		}
		case USB_DT_DEVICE_QUALIFIER:
			/* high speed only device, like the vendor gadget */
			return -EOPNOTSUPP;
		default:
			return -EOPNOTSUPP;
		}
	}

	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType !=
		    (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE))
			return -EOPNOTSUPP;
		return aml_set_configuration(b, w_value);

	case USB_REQ_GET_CONFIGURATION:
		b->ep0_buf[0] = b->configured ? 1 : 0;
		return min_t(u16, w_length, 1);

	case USB_REQ_SET_INTERFACE:
		return 0;

	case USB_REQ_GET_INTERFACE:
		b->ep0_buf[0] = 0;
		return min_t(u16, w_length, 1);

	case USB_REQ_GET_STATUS:
		b->ep0_buf[0] = 0;
		b->ep0_buf[1] = 0;
		return min_t(u16, w_length, 2);

	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int aml_vendor_request(struct aml_burn *b,
			      const struct usb_ctrlrequest *ctrl,
			      u16 w_length, u16 w_value, u16 w_index)
{
	b->ep0_request = ctrl->bRequest;
	b->ep0_value = w_value;
	b->ep0_index = w_index;

	switch (ctrl->bRequest) {
	case AM_REQ_IDENTIFY_HOST: {
		static const u8 id[AML_BURN_IDENTIFY_LEN] = {
			AML_BURN_ROM_VER_MAJOR,
			AML_BURN_ROM_VER_MINOR,
			AML_BURN_ROM_STAGE_MAJOR,
			AML_BURN_ROM_STAGE_MINOR,
		};

		if (!(ctrl->bRequestType & USB_DIR_IN))
			return -EOPNOTSUPP;

		b->identified = true;
		b->identify_window = false;
		memcpy(b->ep0_buf, id, sizeof(id));
		printf("aml_burn: ID[%d]\n", AML_BURN_ROM_STAGE_MINOR);

		return min_t(u16, w_length, sizeof(id));
	}

	case AM_REQ_TPL_STAT:
		if (!(ctrl->bRequestType & USB_DIR_IN))
			return -EOPNOTSUPP;
		memcpy(b->ep0_buf, b->result, AML_BURN_REPLY_SZ);
		return min_t(u16, w_length, AML_BURN_REPLY_SZ);

	case AM_REQ_UPLOAD: {
		u32 hdr[4] = { AML_BURN_UPLOAD_MAGIC, 0, 0, 0 };
		u32 left;

		if (!(ctrl->bRequestType & USB_DIR_IN))
			return -EOPNOTSUPP;

		b->up_chunk = 0;
		if (b->up.active) {
			left = min_t(u64, b->up.total - b->up.done,
				     AML_BURN_SLOT_SZ);
			b->up_chunk = left;
			hdr[1] = left;
		}

		memset(b->ep0_buf, 0, AML_BURN_UPLOAD_HDR_SZ);
		memcpy(b->ep0_buf, hdr, sizeof(hdr));
		debug("aml_burn: upload chunk 0x%x at 0x%llx\n", b->up_chunk,
		      (unsigned long long)b->up.done);

		return min_t(u16, w_length, AML_BURN_UPLOAD_HDR_SZ);
	}

	case AM_REQ_TPL_CMD:
	case AM_REQ_BULKCMD:
	case AM_REQ_DOWNLOAD:
		if (ctrl->bRequestType & USB_DIR_IN)
			return -EOPNOTSUPP;
		if (!w_length || w_length > AML_BURN_CMD_SZ)
			return -EOPNOTSUPP;
		/* data stage is the command string / download descriptor */
		return w_length;

	default:
		printf("aml_burn: unsupported vendor request 0x%02x\n",
		       ctrl->bRequest);
		return -EOPNOTSUPP;
	}
}

static void aml_ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct aml_burn *b = &burn;

	if (req->status) {
		printf("aml_burn: EP0 status %d (bRequest 0x%02x)\n",
		       req->status, b->ep0_request);
		return;
	}

	if (!(b->ep0_type & USB_DIR_IN) && b->ep0_request) {
		b->ep0_out_actual = req->actual;
		b->ep0_out_ready = true;
		return;
	}

	/* 0x33: the 16 byte header is out, the bulk IN data may follow. */
	if ((b->ep0_type & USB_DIR_IN) && b->ep0_request == AM_REQ_UPLOAD)
		b->up_ep0_done = true;
}

static int aml_burn_setup(struct usb_gadget *gadget,
			  const struct usb_ctrlrequest *ctrl)
{
	struct aml_burn *b = &burn;
	u16 w_length = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	struct usb_request *req = b->ep0_req;
	int value;

	b->enumerated = true;
	b->ep0_type = ctrl->bRequestType;
	b->ep0_request = 0;

	req->complete = aml_ep0_complete;
	req->length = 0;
	req->zero = 0;

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		value = aml_standard_request(b, ctrl, w_length, w_value);
		break;
	case USB_TYPE_VENDOR:
		value = aml_vendor_request(b, ctrl, w_length, w_value,
					   w_index);
		break;
	default:
		value = -EOPNOTSUPP;
		break;
	}

	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length &&
			    !(value % gadget->ep0->maxpacket);
		if (usb_ep_queue(gadget->ep0, req, GFP_ATOMIC)) {
			printf("aml_burn: EP0 queue failed (bRequest 0x%02x)\n",
			       ctrl->bRequest);
			return -EIO;
		}
	}

	return value;
}

static int aml_burn_bind(struct usb_gadget *gadget)
{
	struct aml_burn *b = &burn;

	b->gadget = gadget;

	b->in_ep = aml_find_ep(gadget, "ep1in-bulk");
	b->out_ep = aml_find_ep(gadget, "ep2out-bulk");

	if (!b->in_ep || !b->out_ep) {
		b->in_ep = aml_autoconfig_ep(gadget, AML_BURN_BULK_EP_IN);
		b->out_ep = aml_autoconfig_ep(gadget, AML_BURN_BULK_EP_OUT);
	}

	if (!b->in_ep || !b->out_ep) {
		printf("aml_burn: need one bulk IN (0x%02x) and one bulk OUT (0x%02x) endpoint\n",
		       AML_BURN_BULK_EP_IN, AML_BURN_BULK_EP_OUT);
		return -ENODEV;
	}

	b->in_ep->driver_data = b->in_ep;
	b->out_ep->driver_data = b->out_ep;

	b->ep0_req = usb_ep_alloc_request(gadget->ep0, 0);
	b->in_req = usb_ep_alloc_request(b->in_ep, 0);
	b->out_req = usb_ep_alloc_request(b->out_ep, 0);
	b->cont_req = usb_ep_alloc_request(b->in_ep, 0);
	if (!b->ep0_req || !b->in_req || !b->out_req || !b->cont_req)
		return -ENOMEM;

	b->ep0_req->buf = b->ep0_buf;
	b->ep0_req->complete = aml_ep0_complete;
	b->in_req->buf = b->in_buf;
	b->in_req->complete = aml_bulk_in_complete;
	b->out_req->buf = b->out_buf;
	b->out_req->complete = aml_bulk_out_complete;

	printf("aml_burn: bound to %s (%s, %s)\n", gadget->name,
	       b->in_ep->name, b->out_ep->name);

	return 0;
}

static void aml_burn_unbind(struct usb_gadget *gadget)
{
	struct aml_burn *b = &burn;

	if (b->in_ep)
		usb_ep_disable(b->in_ep);
	if (b->out_ep)
		usb_ep_disable(b->out_ep);

	if (b->ep0_req)
		usb_ep_free_request(gadget->ep0, b->ep0_req);
	if (b->in_req)
		usb_ep_free_request(b->in_ep, b->in_req);
	if (b->out_req)
		usb_ep_free_request(b->out_ep, b->out_req);
	if (b->cont_req)
		usb_ep_free_request(b->in_ep, b->cont_req);

	b->ep0_req = NULL;
	b->in_req = NULL;
	b->out_req = NULL;
	b->cont_req = NULL;
}

static void aml_burn_disconnect(struct usb_gadget *gadget)
{
	struct aml_burn *b = &burn;

	usb_ep_disable(b->in_ep);
	usb_ep_disable(b->out_ep);
	b->configured = false;
	b->enumerated = false;
	b->identify_window = false;
	b->connect_start = get_timer(0);
	b->in_done = false;
}

static void aml_burn_reset(struct usb_gadget *gadget)
{
	struct aml_burn *b = &burn;

	/*
	 * A bus reset drops the configuration; disable the endpoints so that
	 * the next SET_CONFIGURATION re-activates them in hardware too.
	 */
	usb_ep_disable(b->in_ep);
	usb_ep_disable(b->out_ep);
	b->configured = false;
	b->in_done = false;
}

/* ------------------------------------------------------------------ */
/* Command handling							*/
/* ------------------------------------------------------------------ */

static int aml_cmd_download(int argc, char *const argv[], char *detail,
			    u32 detail_sz)
{
	struct aml_burn *b = &burn;
	u64 size;
	int ret;

	if (argc >= 2 && !strcmp(argv[1], "get_status")) {
		if (b->img.active && b->img.complete)
			return 0;
		snprintf(detail, detail_sz, "not complete");
		return -1;
	}

	if (argc >= 2 && !strcmp(argv[1], "is_ready")) {
		if (b->img.active && !b->img.complete)
			return 0;
		snprintf(detail, detail_sz, "not ready");
		return -1;
	}

	if (argc < 5) {
		snprintf(detail, detail_sz,
			 "usage: download <media> <part> <type> <size>");
		return -1;
	}

	size = simple_strtoull(argv[4], NULL, 0);
	ret = aml_img_begin(argv[1], argv[2], argv[3], size, &b->img);
	if (ret) {
		snprintf(detail, detail_sz, "cannot start download for %s",
			 argv[2]);
		return -1;
	}

	b->dl_bytes_since_cont = 0;
	b->dl_last_write_ms = 0;
	b->dl_cont_ms = 0;
	b->up.active = false;

	return 0;
}

static int aml_cmd_upload(int argc, char *const argv[], char *detail,
			  u32 detail_sz)
{
	u64 size;
	int ret;

	if (argc < 5) {
		snprintf(detail, detail_sz,
			 "usage: upload <media> <part> <type> <size>");
		return -1;
	}

	size = simple_strtoull(argv[4], NULL, 0);
	ret = aml_up_begin(argv[1], argv[2], argv[3], size, &burn.up);
	if (ret) {
		snprintf(detail, detail_sz, "cannot start upload from %s",
			 argv[2]);
		return -1;
	}

	return 0;
}

static int aml_cmd_verify(int argc, char *const argv[], char *detail,
			  u32 detail_sz)
{
	if (argc != 3 || strcmp(argv[1], "sha1sum")) {
		snprintf(detail, detail_sz, "usage: verify sha1sum <sha1>");
		return -1;
	}

	if (aml_img_verify_sha1(&burn.img, argv[2])) {
		snprintf(detail, detail_sz, "sha1sum mismatch for %s",
			 burn.img.part);
		return -1;
	}

	return 0;
}

static int aml_cmd_burn_complete(int argc, char *const argv[], char *detail,
				 u32 detail_sz)
{
	struct aml_burn *b = &burn;
	u32 choice;

	if (argc < 2) {
		snprintf(detail, detail_sz, "burn_complete needs a choice");
		return -1;
	}

	choice = simple_strtoul(argv[1], NULL, 0);

	switch (choice) {
	case AML_BURN_COMPLETE_QUERY:
		return 0;
	case AML_BURN_COMPLETE_POWEROFF_DIRECT:
	case AML_BURN_COMPLETE_POWEROFF_AFTER_POWERKEY:
		b->action = AML_ACT_POWEROFF;
		b->exit_after_reply = true;
		return 0;
	case AML_BURN_COMPLETE_POWEROFF_AFTER_UNPLUG:
		/* keep the gadget alive until the cable is pulled */
		b->action = AML_ACT_NONE;
		return 0;
	case AML_BURN_COMPLETE_REBOOT_NORMAL:
	case AML_BURN_COMPLETE_REBOOT_SDC_BURN:
	case AML_BURN_COMPLETE_REBOOT_UPDATE:
		b->action = AML_ACT_RESET;
		b->exit_after_reply = true;
		return 0;
	default:
		snprintf(detail, detail_sz, "bad burn_complete choice %u",
			 choice);
		return -1;
	}
}

/*
 * Execute one command string from the host.  This is the port of the vendor
 * optimus_working() minus its run_command() fallback: only the commands below
 * exist, anything else is rejected (no remote shell).
 */
static int aml_cmd_exec(char *line, char *out, u32 out_sz)
{
	struct aml_burn *b = &burn;
	char *argv[8];
	char detail[160];
	int argc, ret;

	memset(out, 0, out_sz);
	memset(detail, 0, sizeof(detail));

	argc = aml_split(line, argv, ARRAY_SIZE(argv));
	if (!argc) {
		snprintf(out, out_sz, "failed:no command at all");
		return -1;
	}

	if (!strcmp(argv[0], "low_power")) {
		/*
		 * Vendor blanks the panel and limits the current; there is no
		 * display in U-Boot, so nothing to do.
		 */
		ret = 0;
	} else if (!strcmp(argv[0], "disk_initial")) {
		aml_store_exit();
		ret = aml_store_init();
		if (ret)
			snprintf(detail, sizeof(detail),
				 "cannot init storage (%d)", ret);
	} else if (!strcmp(argv[0], "bootloader_is_old")) {
		/*
		 * Always "new": the tool then skips erase_bootloader and does
		 * not touch the boot partition.
		 */
		snprintf(out, out_sz, "failed:bootloader is new");
		return -1;
	} else if (!strcmp(argv[0], "download")) {
		ret = aml_cmd_download(argc, argv, detail, sizeof(detail));
	} else if (!strcmp(argv[0], "upload")) {
		ret = aml_cmd_upload(argc, argv, detail, sizeof(detail));
	} else if (!strcmp(argv[0], "verify")) {
		ret = aml_cmd_verify(argc, argv, detail, sizeof(detail));
	} else if (!strcmp(argv[0], "save_setting")) {
		/*
		 * The mainline port keeps no persistent env here
		 * (CONFIG_ENV_IS_NOWHERE), so there is nothing to save.
		 */
		ret = 0;
	} else if (!strcmp(argv[0], "burn_complete")) {
		ret = aml_cmd_burn_complete(argc, argv, detail,
					    sizeof(detail));
	} else if (!strcmp(argv[0], "reset")) {
		b->action = AML_ACT_RESET;
		b->exit_after_reply = true;
		ret = 0;
	} else if (!strcmp(argv[0], "poweroff")) {
		b->action = AML_ACT_POWEROFF;
		b->exit_after_reply = true;
		ret = 0;
	} else if (!strcmp(argv[0], "echo")) {
		ret = 0;
	} else if (!strcmp(argv[0], "erase_bootloader")) {
		snprintf(detail, sizeof(detail),
			 "erase_bootloader disabled (boot partition is kept)");
		ret = -1;
	} else {
		snprintf(detail, sizeof(detail), "unsupported command \"%s\"",
			 argv[0]);
		ret = -1;
	}

	if (!ret)
		snprintf(out, out_sz, "success");
	else if (detail[0])
		snprintf(out, out_sz, "failed:%s", detail);
	else
		snprintf(out, out_sz, "failed:");

	return ret;
}

/* ------------------------------------------------------------------ */
/* Protocol work (runs from the main loop, never from the ISR)		*/
/* ------------------------------------------------------------------ */

static void aml_download_begin(struct aml_burn *b, u32 len)
{
	struct aml_burn_write_media wm;
	u32 data_len, seq, sum;
	u16 alg, ack;

	if (!b->img.active) {
		aml_fail_reply("no download command pending");
		return;
	}

	if (len < 16) {
		aml_fail_reply("short download command (%u bytes)", len);
		return;
	}

	memset(&wm, 0, sizeof(wm));
	memcpy(&wm, b->ep0_buf, min_t(u32, len, sizeof(wm)));

	data_len = wm.user_data_len;
	seq = wm.sequence_no;
	sum = wm.data_check_sum;

	if (len >= 0x20) {
		alg = wm.data_check_alg;
		ack = wm.bulk_in_ack_len;
	} else {
		/* old protocol: add-sum and 512 byte acknowledgements */
		alg = AML_CKC_ALG_ADDSUM;
		ack = AML_BURN_REPLY_SZ;
	}

	if (!ack || ack > AML_BURN_REPLY_SZ)
		ack = AML_BURN_REPLY_SZ;

	if (!data_len || data_len > AML_BURN_SLOT_SZ) {
		aml_fail_reply("bad packet length 0x%x", data_len);
		return;
	}

	if (seq < b->img.next_seq) {
		/*
		 * Retransmission of an already accepted packet: acknowledge it
		 * again, do not write it twice.
		 */
		goto queue;
	}

	if (b->img.wire + data_len > b->img.total) {
		aml_fail_reply("packet 0x%x past end of %s (0x%llx left)",
			       data_len, b->img.part,
			       (unsigned long long)(b->img.total -
						    b->img.wire));
		return;
	}

	/*
	 * The vendor buffer manager only accepts short packets for the last
	 * packet of an image ("only last transfer can less 64K").
	 */
	if (data_len < AML_BURN_SLOT_SZ &&
	    b->img.wire + data_len != b->img.total) {
		aml_fail_reply("short packet 0x%x at seq %u, 0x%llx left",
			       data_len, seq,
			       (unsigned long long)(b->img.total -
						    b->img.wire));
		return;
	}

queue:
	b->out_len = data_len;
	b->out_seq = seq;
	b->out_sum = sum;
	b->out_alg = alg;
	b->out_ack = ack;
	b->out_ready = false;
	b->in_done = false;

	b->out_req->buf = b->out_buf;
	b->out_req->length = data_len;
	b->out_req->zero = 0;
	b->out_req->complete = aml_bulk_out_complete;

	if (usb_ep_queue(b->out_ep, b->out_req, GFP_ATOMIC)) {
		printf("aml_burn: bulk OUT queue failed\n");
		aml_fail_reply("usb queue failed");
	}
}

static void aml_download_store(struct aml_burn *b, u32 len, bool dup)
{
	struct aml_burn_img *img = &b->img;
	unsigned long start = get_timer(0);
	int ret = 0;

	if (!dup) {
		ret = aml_img_write(img, b->out_buf, len);
		if (!ret)
			img->next_seq++;
	}

	b->dl_last_write_ms = (u32)(get_timer(0) - start);

	if (ret) {
		aml_fail_reply("media error %d at seq %u, len %u", ret,
			       b->out_seq, len);
		return;
	}

	b->dl_bytes_since_cont += len;
	debug("aml_burn: seq %u len 0x%x%s (%u ms)\n", b->out_seq, len,
	      dup ? " (dup)" : "", b->dl_last_write_ms);
	aml_bulk_reply("OK!!", b->out_ack);
}

/*
 * Decide whether this packet gets an early "Continue:32" ack.  Only worth it
 * when the previous write was slow, and only within the rate limits described
 * at the top of the file.
 */
static bool aml_continue_due(struct aml_burn *b, u32 len)
{
	unsigned long now = get_timer(0);

	if (b->dl_last_write_ms < AML_BURN_CONTINUE_SLOW_MS)
		return false;

	if (b->dl_cont_ms &&
	    now - b->dl_cont_ms < AML_BURN_CONTINUE_MIN_MS)
		return false;

	/*
	 * Vendor keeps 8 MiB between two Continues; the first one of a slow
	 * stretch is allowed right away.  If a single packet already took
	 * longer than the host's timeout budget, ignore the byte limit: losing
	 * the link is worse than the extra re-read.
	 */
	if (b->dl_bytes_since_cont &&
	    b->dl_bytes_since_cont + len < AML_BURN_CONTINUE_MIN_BYTES &&
	    b->dl_last_write_ms < AML_BURN_CONTINUE_URGENT_MS)
		return false;

	return true;
}

static void aml_process_download(struct aml_burn *b)
{
	struct aml_burn_img *img = &b->img;
	u32 len = b->out_req->actual;
	int status = b->out_req->status;
	bool dup = false;

	if (status) {
		aml_fail_reply("bulk OUT status %d at seq %u", status,
			       b->out_seq);
		return;
	}

	if (len > b->out_len)
		len = b->out_len;

	if (b->out_alg == AML_CKC_ALG_ADDSUM) {
		u32 gen = aml_burn_add_sum(b->out_buf, len);

		if (gen != b->out_sum) {
			aml_fail_reply("Alg[%x]0x: genSum %x != org %x at seq %x, "
				       "len %x, resend %d", b->out_alg, gen,
				       b->out_sum, b->out_seq, len, 0);
			return;
		}
	} else if (b->out_alg != AML_CKC_ALG_NONE) {
		aml_fail_reply("checksum algorithm 0x%x not supported",
			       b->out_alg);
		return;
	}

	if (b->out_seq < img->next_seq) {
		/* retransmission of an already accepted packet */
		dup = true;
	} else if (b->out_seq != img->next_seq) {
		aml_fail_reply("sequence gap: got %u, want %u (len %u)",
			       b->out_seq, img->next_seq, len);
		return;
	}

	/*
	 * The media write happens synchronously, so the host would see nothing
	 * until it is done.  Keep it alive first, then do the real work.
	 */
	if (!dup && aml_continue_due(b, len)) {
		debug("aml_burn: Continue:32 before seq %u (%u ms)\n",
		      b->out_seq, b->dl_last_write_ms);
		if (!aml_continue_reply(b)) {
			b->dl_bytes_since_cont = 0;
			b->dl_cont_ms = get_timer(0);
		}
	}

	aml_download_store(b, len, dup);
}

static void aml_process_ep0_out(struct aml_burn *b)
{
	u32 len = b->ep0_out_actual;

	if (len > AML_BURN_CMD_SZ)
		len = AML_BURN_CMD_SZ;

	/*
	 * The host does not NUL terminate command strings: do it, and make sure
	 * bytes left over from an earlier request are not parsed.
	 */
	b->ep0_buf[min_t(u32, len, AML_BURN_CMD_SZ - 1)] = '\0';

	switch (b->ep0_request) {
	case AM_REQ_TPL_CMD:
		if (b->ep0_index != 1) {
			debug("aml_burn: tplcmd subcode %u ignored\n",
			      b->ep0_index);
			break;
		}
		printf("aml_burn: tplcmd[%s]\n", (char *)b->ep0_buf);
		aml_cmd_exec((char *)b->ep0_buf, (char *)b->result,
			     sizeof(b->result));
		break;

	case AM_REQ_BULKCMD:
		printf("aml_burn: bulkcmd[%s]\n", (char *)b->ep0_buf);
		aml_cmd_exec((char *)b->ep0_buf, (char *)b->result,
			     sizeof(b->result));
		aml_bulk_reply(b->result, AML_BURN_REPLY_SZ);
		break;

	case AM_REQ_DOWNLOAD:
		aml_download_begin(b, len);
		break;

	default:
		break;
	}
}

static void aml_process_pending(struct aml_burn *b)
{
	if (b->ep0_out_ready) {
		b->ep0_out_ready = false;
		aml_process_ep0_out(b);
	}

	if (b->out_ready) {
		b->out_ready = false;
		aml_process_download(b);
	}

	/* 0x33 header sent, stream the announced chunk over bulk IN. */
	if (b->up_ep0_done && !b->up_xfer) {
		u32 chunk = b->up_chunk;
		int ret;

		b->up_ep0_done = false;
		b->up_chunk = 0;

		if (!chunk) {
			b->up.active = false;
			return;
		}

		ret = aml_up_read(&b->up, b->up.done, b->out_buf, chunk);
		if (ret) {
			printf("aml_burn: upload read failed (%d)\n", ret);
			b->up.active = false;
			return;
		}

		b->up_xfer = true;
		b->in_req->buf = b->out_buf;
		b->in_req->length = chunk;
		b->in_req->zero = 0;
		b->in_req->complete = aml_bulk_in_complete;
		if (usb_ep_queue(b->in_ep, b->in_req, GFP_ATOMIC)) {
			b->up_xfer = false;
			printf("aml_burn: bulk IN queue failed\n");
			b->up.active = false;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Entry point								*/
/* ------------------------------------------------------------------ */

int aml_usb_burn_run(unsigned int connect_timeout_ms,
		     unsigned int identify_timeout_ms)
{
	struct aml_burn *b = &burn;
	struct udevice *udc;
	int ret;

	memset(b, 0, sizeof(*b));
	b->connect_ms = connect_timeout_ms ? connect_timeout_ms :
			AML_BURN_DEFAULT_CONNECT_MS;
	b->identify_ms = identify_timeout_ms;
	b->action = AML_ACT_NONE;

	ret = uclass_first_device_err(UCLASS_USB_GADGET_GENERIC, &udc);
	if (ret) {
		printf("aml_burn: no USB device controller in the device tree (%d)\n",
		       ret);
		return ret;
	}

	b->ep0_buf = memalign(ARCH_DMA_MINALIGN, AML_BURN_REPLY_SZ);
	b->out_buf = memalign(ARCH_DMA_MINALIGN, AML_BURN_SLOT_SZ);
	b->in_buf = memalign(ARCH_DMA_MINALIGN, AML_BURN_REPLY_SZ);
	b->cont_buf = memalign(ARCH_DMA_MINALIGN, AML_BURN_REPLY_SZ);
	if (!b->ep0_buf || !b->out_buf || !b->in_buf || !b->cont_buf) {
		ret = -ENOMEM;
		goto out_free;
	}

	aml_store_exit();

	b->driver.function = "aml_burn";
	b->driver.speed = USB_SPEED_HIGH;
	b->driver.bind = aml_burn_bind;
	b->driver.unbind = aml_burn_unbind;
	b->driver.setup = aml_burn_setup;
	b->driver.disconnect = aml_burn_disconnect;
	b->driver.reset = aml_burn_reset;

	printf("aml_burn: waiting for Amlogic USB Burning Tool (VID:PID %04x:%04x, %u ms)\n",
	       AML_BURN_VID, AML_BURN_PID, b->connect_ms);

	ret = usb_gadget_register_driver(&b->driver);
	if (ret) {
		printf("aml_burn: cannot register gadget driver (%d)\n", ret);
		goto out_free;
	}

	b->running = true;
	b->connect_start = get_timer(0);

	while (b->running) {
		if (ctrlc()) {
			printf("\naml_burn: aborted by Ctrl-C\n");
			ret = -EINTR;
			break;
		}

		dm_usb_gadget_handle_interrupts(udc);
		aml_process_pending(b);

		if (b->exit_after_reply && b->in_done &&
		    (long)(get_timer(0) - b->exit_deadline) >= 0) {
			b->running = false;
			break;
		}

		if (!b->enumerated) {
			if (get_timer(b->connect_start) > b->connect_ms) {
				printf("aml_burn: no host after %u ms\n",
				       b->connect_ms);
				ret = -ETIMEDOUT;
				break;
			}
		} else if (b->identify_window && b->identify_ms &&
			   !b->identified) {
			if (get_timer(b->identify_start) > b->identify_ms) {
				printf("aml_burn: no identify request within %u ms\n",
				       b->identify_ms);
				ret = -ETIMEDOUT;
				break;
			}
		}
	}

	usb_gadget_unregister_driver(&b->driver);

	if (b->action == AML_ACT_RESET) {
		printf("aml_burn: burn complete, rebooting\n");
		do_reset(NULL, 0, 0, NULL);
	} else if (b->action == AML_ACT_POWEROFF) {
		printf("aml_burn: burn complete, powering off\n");
		if (IS_ENABLED(CONFIG_SYSRESET))
			sysreset_walk(SYSRESET_POWER_OFF);
		printf("aml_burn: power off not available, halting\n");
	}

out_free:
	free(b->ep0_buf);
	free(b->out_buf);
	free(b->in_buf);
	free(b->cont_buf);
	b->ep0_buf = NULL;
	b->out_buf = NULL;
	b->in_buf = NULL;
	b->cont_buf = NULL;

	return ret;
}

/* ------------------------------------------------------------------ */
/* Command								*/
/* ------------------------------------------------------------------ */

static int do_aml_burn(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	unsigned int connect_ms = AML_BURN_DEFAULT_CONNECT_MS;
	unsigned int identify_ms = 0;
	int ret;

	if (argc > 1)
		connect_ms = simple_strtoul(argv[1], NULL, 0);
	if (argc > 2)
		identify_ms = simple_strtoul(argv[2], NULL, 0);

	ret = aml_usb_burn_run(connect_ms, identify_ms);

	return ret ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(aml_burn, 3, 0, do_aml_burn,
	   "enter Amlogic USB Burning Tool (vendor burn) mode",
	   "[connect_timeout_ms [identify_timeout_ms]]\n"
	   "    Enumerate as 1b8e:c003 (bulk 0x81/0x02, 512 B) and speak the\n"
	   "    Amlogic v2 burn protocol.  With no host the command returns\n"
	   "    after connect_timeout_ms (default 5000).");
