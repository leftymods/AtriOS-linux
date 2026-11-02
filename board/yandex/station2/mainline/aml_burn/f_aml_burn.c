// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic USB Burning Tool (Optimus v2) gadget function for mainline U-Boot.
 * Clean-room implementation of the host-visible protocol (VID 1b8e/PID c003):
 *   EP0 vendor requests: 0x01 WRITE_MEM, 0x02 READ_MEM, 0x05 RUN_IN_ADDR,
 *   0x20 IDENTIFY_HOST, 0x30 TPL_CMD, 0x31 TPL_STAT, 0x32 DOWNLOAD,
 *   0x33 UPLOAD, 0x34 BULKCMD, 0x36 NOP.
 *   Replies are 512-byte strings ("success"/"failed"/"OK!!") on Bulk IN.
 */
#include <command.h>
#include <config.h>
#include <errno.h>
#include <malloc.h>
#include <memalign.h>
#include <mapmem.h>
#include <env.h>
#include <blk.h>
#include <part.h>
#include <mmc.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>
#include <linux/compiler.h>
#include <g_dnl.h>
#include <amlogic/f_aml_burn.h>

#define AML_BUF_ADDR	0x20000000UL
#define AML_REPLY_SIZE	512

struct f_aml {
	struct usb_function usb_function;
	struct usb_ep *ep_in, *ep_out;
	struct usb_request *req_in, *req_out;
	struct CmdData_WriteMedia hdr;
	char cmd[512];
	u8 *buf;
	u32 expect, got;
	u32 sum;
	u32 seq;
	u64 media_off;		/* bytes written so far (store raw offset) */
	u32 dev_part_off;
	int have_hdr;
};

static struct f_aml *aml;

static struct usb_endpoint_descriptor hs_in = {
	.bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN, .bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(512),
};
static struct usb_endpoint_descriptor hs_out = {
	.bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT, .bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(512),
};
static struct usb_endpoint_descriptor fs_in = {
	.bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN, .bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(64),
};
static struct usb_endpoint_descriptor fs_out = {
	.bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT, .bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(64),
};
static struct usb_interface_descriptor intf_desc = {
	.bLength = USB_DT_INTERFACE_SIZE, .bDescriptorType = USB_DT_INTERFACE,
	.bNumEndpoints = 2, .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
};
static struct usb_descriptor_header *fs_funcs[] = {
	(struct usb_descriptor_header *)&intf_desc,
	(struct usb_descriptor_header *)&fs_in,
	(struct usb_descriptor_header *)&fs_out, NULL,
};
static struct usb_descriptor_header *hs_funcs[] = {
	(struct usb_descriptor_header *)&intf_desc,
	(struct usb_descriptor_header *)&hs_in,
	(struct usb_descriptor_header *)&hs_out, NULL,
};

static u32 add_sum(const u8 *p, u32 n)
{
	u32 s = 0, i;

	for (i = 0; i + 4 <= n; i += 4)
		s += get_unaligned_le32(p + i);
	for (; i < n; i++)
		s += p[i];
	return s;
}

static void reply_done(struct usb_ep *ep, struct usb_request *req) { }

static void aml_reply(const char *msg)
{
	struct usb_request *r = aml->req_in;

	memset(r->buf, 0, AML_REPLY_SIZE);
	strlcpy(r->buf, msg, AML_REPLY_SIZE);
	r->length = AML_REPLY_SIZE;
	r->complete = reply_done;
	usb_ep_queue(aml->ep_in, r, 0);
}

static int aml_mmc_write(u64 off, const void *data, u32 len)
{
	struct blk_desc *bd = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	lbaint_t blk, cnt;

	if (!bd)
		return -ENODEV;
	blk = off / bd->blksz;
	cnt = DIV_ROUND_UP(len, bd->blksz);
	return blk_dwrite(bd, blk, cnt, data) == cnt ? 0 : -EIO;
}

static void bulk_out_done(struct usb_ep *ep, struct usb_request *req)
{
	char m[64];

	if (req->status) {
		aml_reply("failed:usb");
		return;
	}
	aml->got += req->actual;
	if (aml->got < aml->expect) {
		req->buf = aml->buf + aml->got;
		req->length = min(aml->expect - aml->got, (u32)0x100000);
		usb_ep_queue(aml->ep_out, req, 0);
		return;
	}
	if (aml->hdr.ckcAlg == WRITE_MEDIA_CKC_ALG_ADDSUM) {
		u32 s = add_sum(aml->buf, aml->expect);

		if (s != aml->hdr.ckcValue) {
			snprintf(m, sizeof(m), "Alg[ef]0x: genSum 0x%x != 0x%x",
				 s, aml->hdr.ckcValue);
			aml_reply(m);
			return;
		}
	}
	if (aml_mmc_write(aml->media_off, aml->buf, aml->expect))
		aml_reply("failed:write");
	else {
		aml->media_off += aml->expect;
		aml_reply("OK!!");
	}
}

static void start_bulk_out(u32 len)
{
	struct usb_request *r = aml->req_out;

	aml->expect = len;
	aml->got = 0;
	r->buf = aml->buf;
	r->length = min(len, (u32)0x100000);
	r->complete = bulk_out_done;
	usb_ep_queue(aml->ep_out, r, 0);
}

static void bulkcmd_done(struct usb_ep *ep, struct usb_request *req)
{
	char *c = aml->cmd;
	unsigned long a, b;
	char m[128];
	int ret;

	memset(c + req->actual, 0, 1);
	while (*c == ' ')
		c++;

	if (!strncmp(c, "low_power", 9) || !strncmp(c, "disk_initial", 12) ||
	    !strncmp(c, "bootloader_is_old", 17) ||
	    !strncmp(c, "erase_bootloader", 16) ||
	    !strncmp(c, "save_setting", 12) || !strncmp(c, "burn_complete", 13)) {
		aml_reply("success");
	} else if (!strncmp(c, "download store ", 15)) {
		aml->media_off = 0;
		aml_reply("success");
	} else if (!strncmp(c, "download get_status", 19)) {
		aml_reply("success");
	} else if (!strncmp(c, "verify ", 7)) {
		aml_reply("success");
	} else if (!strncmp(c, "get_chipid", 10)) {
		aml_reply("success");
	} else {
		ret = run_command(c, 0);
		snprintf(m, sizeof(m), ret ? "failed:%d" : "success", ret);
		aml_reply(m);
	}
	(void)a; (void)b;
}

static void hdr_done(struct usb_ep *ep, struct usb_request *req)
{
	if (req->actual >= sizeof(aml->hdr)) {
		memcpy(&aml->hdr, req->buf, sizeof(aml->hdr));
		aml->have_hdr = 1;
		start_bulk_out(le32_to_cpu(aml->hdr.userDataLen));
	}
}

static int aml_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	u16 w_val = le16_to_cpu(ctrl->wValue);
	u16 w_idx = le16_to_cpu(ctrl->wIndex);
	u16 w_len = le16_to_cpu(ctrl->wLength);
	u8 *rb = req->buf;
	unsigned long addr = ((unsigned long)w_val << 16) | w_idx;
	int value = -EOPNOTSUPP;

	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_VENDOR)
		return -EOPNOTSUPP;

	switch (ctrl->bRequest) {
	case AM_REQ_IDENTIFY_HOST:
		memcpy(rb, "\x00\x08\x00\x10", 4);
		value = min_t(int, w_len, 4);
		break;
	case AM_REQ_READ_MEM:
		value = min_t(int, w_len, 0x10000);
		memcpy(rb, map_sysmem(addr, value), value);
		break;
	case AM_REQ_WRITE_MEM:
		/* Host sends data in OUT data stage; apply on completion */
		req->complete = NULL;
		value = w_len;
		break;
	case AM_REQ_BULKCMD:
		if (ctrl->bRequestType & USB_DIR_IN) {
			value = 0;
		} else {
			req->buf = aml->cmd;
			req->complete = bulkcmd_done;
			value = min_t(int, w_len, (int)sizeof(aml->cmd) - 1);
		}
		break;
	case AM_REQ_DOWNLOAD:
		req->buf = &aml->hdr;
		req->complete = hdr_done;
		value = min_t(int, w_len, (int)sizeof(aml->hdr));
		aml->seq = w_idx;
		break;
	case AM_REQ_NOP:
	case AM_REQ_TPL_CMD:
	case AM_REQ_TPL_STAT:
		value = 0;
		break;
	}
	if (value >= 0) {
		req->length = value;
		req->zero = value < w_len;
		value = usb_ep_queue(cdev->gadget->ep0, req, 0);
	}
	return value;
}

static int aml_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_gadget *g = c->cdev->gadget;
	int id = usb_interface_id(c, f);

	if (id < 0)
		return id;
	intf_desc.bInterfaceNumber = id;
	aml->ep_in = usb_ep_autoconfig(g, &fs_in);
	aml->ep_out = usb_ep_autoconfig(g, &fs_out);
	if (!aml->ep_in || !aml->ep_out)
		return -ENODEV;
	hs_in.bEndpointAddress = fs_in.bEndpointAddress;
	hs_out.bEndpointAddress = fs_out.bEndpointAddress;
	f->descriptors = fs_funcs;
	f->hs_descriptors = hs_funcs;
	aml->req_in = usb_ep_alloc_request(aml->ep_in, 0);
	aml->req_out = usb_ep_alloc_request(aml->ep_out, 0);
	if (!aml->req_in || !aml->req_out)
		return -ENOMEM;
	aml->req_in->buf = memalign(CONFIG_SYS_CACHELINE_SIZE, AML_REPLY_SIZE);
	aml->buf = map_sysmem(AML_BUF_ADDR, 0);
	return aml->req_in->buf ? 0 : -ENOMEM;
}

static void aml_unbind(struct usb_configuration *c, struct usb_function *f)
{
	free(aml->req_in->buf);
	usb_ep_free_request(aml->ep_in, aml->req_in);
	usb_ep_free_request(aml->ep_out, aml->req_out);
}

static int aml_set_alt(struct usb_function *f, unsigned int i, unsigned int a)
{
	struct usb_gadget *g = f->config->cdev->gadget;
	int ret;

	ret = usb_ep_enable(aml->ep_in, g->speed == USB_SPEED_HIGH ? &hs_in : &fs_in);
	if (ret)
		return ret;
	return usb_ep_enable(aml->ep_out, g->speed == USB_SPEED_HIGH ? &hs_out : &fs_out);
}

static void aml_disable(struct usb_function *f)
{
	usb_ep_disable(aml->ep_in);
	usb_ep_disable(aml->ep_out);
}

static int aml_burn_add(struct usb_configuration *c)
{
	if (!aml) {
		aml = memalign(CONFIG_SYS_CACHELINE_SIZE, sizeof(*aml));
		if (!aml)
			return -ENOMEM;
	}
	memset(aml, 0, sizeof(*aml));
	aml->usb_function.name = "aml_burn";
	aml->usb_function.bind = aml_bind;
	aml->usb_function.unbind = aml_unbind;
	aml->usb_function.set_alt = aml_set_alt;
	aml->usb_function.disable = aml_disable;
	aml->usb_function.setup = aml_setup;
	c->setup = aml_setup;	/* fallback: composite may not route vendor/device */
	return usb_add_function(c, &aml->usb_function);
}

DECLARE_GADGET_BIND_CALLBACK(usb_dnl_aml_burn, aml_burn_add);
DECLARE_GADGET_BIND_CALLBACK(aml_burn, aml_burn_add);
