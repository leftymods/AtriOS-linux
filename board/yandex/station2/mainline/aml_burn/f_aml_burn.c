// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic USB Burning Tool (Optimus v2) gadget function for mainline U-Boot.
 * Clean-room implementation of the host-visible protocol (VID 1b8e/PID c003):
 *   Supported EP0 requests: 0x20 IDENTIFY_HOST, 0x32 DOWNLOAD,
 *   0x34 BULKCMD, 0x36 NOP. Unsupported operations stall or return failure.
 *   Replies are 512-byte strings ("success"/"failed"/"OK!!") on Bulk IN.
 */
#include <command.h>
#include <cli.h>
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

#define AML_REPLY_SIZE	512

struct f_aml {
 struct usb_function usb_function;
 struct usb_ep *ep_in, *ep_out;
 struct usb_request *req_in, *req_out;
 struct CmdData_WriteMedia hdr;
 char cmd[512];
 u8 *buf;
 u32 expect, got;
 u32 seq, next_seq;
 u64 media_off, media_size;
 struct blk_desc *bd;
 struct disk_partition part;
 bool active, receiving, reply_busy, have_seq;
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

static void reply_done(struct usb_ep *ep, struct usb_request *req)
{
 aml->reply_busy = false;
}

static void aml_reply(const char *msg)
{
 struct usb_request *r = aml->req_in;
 int ret;
 if (aml->reply_busy) {
  printf("AML: reply pending; ignoring overlapping command\n");
  return;
 }
 memset(r->buf, 0, AML_REPLY_SIZE);
 strlcpy(r->buf, msg, AML_REPLY_SIZE);
 r->length = AML_REPLY_SIZE;
 r->complete = reply_done;
 aml->reply_busy = true;
 ret = usb_ep_queue(aml->ep_in, r, 0);
 if (ret) {
  aml->reply_busy = false;
  aml->active = false;
  printf("AML: reply queue failed: %d\n", ret);
 }
}

static int aml_mmc_write(const void *data, u32 len)
{
 lbaint_t blk, cnt;
 if (!aml->active || !aml->bd ||
     !aml_valid_write(aml->media_off, len, aml->media_size, aml->bd->blksz))
  return -EINVAL;
 blk = aml->part.start + aml->media_off / aml->bd->blksz;
 cnt = len / aml->bd->blksz;
 return blk_dwrite(aml->bd, blk, cnt, data) == cnt ? 0 : -EIO;
}

static void bulk_out_done(struct usb_ep *ep, struct usb_request *req)
{
 int ret;
 if (req->status || !req->actual || req->actual > aml->expect - aml->got) {
  aml->receiving = false;
  aml->active = false;
  aml_reply("failed:usb");
  return;
 }
 aml->got += req->actual;
 if (aml->got < aml->expect) {
  req->buf = aml->buf + aml->got;
  req->length = aml->expect - aml->got;
  ret = usb_ep_queue(aml->ep_out, req, 0);
  if (ret) {
   aml->receiving = false;
   aml->active = false;
   aml_reply("failed:queue");
  }
  return;
 }
 aml->receiving = false;
 if (le16_to_cpu(aml->hdr.dataCheckAlgorithm) == WRITE_MEDIA_CKC_ALG_ADDSUM &&
     aml_addsum(aml->buf, aml->expect) != le32_to_cpu(aml->hdr.dataCheckSum)) {
  aml_reply("failed:checksum");
  return;
 }
 if (aml_mmc_write(aml->buf, aml->expect)) {
  aml->active = false;
  aml_reply("failed:write");
 } else {
  aml->media_off += aml->expect;
  aml->next_seq = aml->seq + 1;
  aml->have_seq = true;
  aml_reply("OK!!");
 }
}

static void bulkcmd_done(struct usb_ep *ep, struct usb_request *req)
{
 char *argv[CONFIG_SYS_MAXARGS + 1], *end;
 int argc, dev, pn;
 u64 size;
 const char *target;
 if (req->status || req->actual >= sizeof(aml->cmd)) {
  aml_reply("failed:command");
  return;
 }
 memcpy(aml->cmd, req->buf, req->actual);
 aml->cmd[req->actual] = 0;
 argc = cli_simple_parse_line(aml->cmd, argv);
 if (argc <= 0) { aml_reply("failed:command"); return; }
 if (!strcmp(argv[0], "low_power") || !strcmp(argv[0], "bootloader_is_old")) {
  aml_reply("success");
 } else if (argc == 2 && !strcmp(argv[0], "download") &&
            !strcmp(argv[1], "get_status")) {
  aml_reply(aml->active ? "success" : "failed:no-download");
 } else if (argc == 5 && !strcmp(argv[0], "download") &&
            !strcmp(argv[1], "store") && !strcmp(argv[3], "normal")) {
  aml->active = false;
  target = env_get("aml_mmc_dev");
  if (!target || !*target) { aml_reply("failed:set-aml_mmc_dev"); return; }
  /* Explicit device and user HW partition, never inherit a boot partition. */
  dev = simple_strtoul(target, &end, 10);
  if (*end || dev < 0 || dev > 255) { aml_reply("failed:mmc-device"); return; }
  {
   char spec[16];
   snprintf(spec, sizeof(spec), "%d.0", dev);
   if (blk_get_device_by_str("mmc", spec, &aml->bd) < 0) {
    aml_reply("failed:mmc-device"); return;
   }
  }
  pn = part_get_info_by_name(aml->bd, argv[2], &aml->part);
  /* DOS images have no partition names: these are the image builder's contract. */
  if (pn < 0 && (!strcmp(argv[2], "boot") || !strcmp(argv[2], "rootfs")))
   pn = part_get_info(aml->bd, !strcmp(argv[2], "boot") ? 1 : 2, &aml->part);
  if (pn < 0 || !aml->bd->blksz || aml->part.start > aml->bd->lba ||
      aml->part.size > aml->bd->lba - aml->part.start) {
   aml_reply("failed:partition"); return;
  }
  size = simple_strtoull(argv[4], &end, 16);
  if (*end || !size || size % aml->bd->blksz ||
      size / aml->bd->blksz > aml->part.size) {
   aml_reply("failed:size"); return;
  }
  aml->media_off = 0;
  aml->media_size = size;
  aml->have_seq = false;
  aml->active = true;
  aml_reply("success");
 } else {
  /* Never claim verify/erase/partition/sparse operations we did not perform. */
  aml_reply("failed:unsupported");
 }
}

static void hdr_done(struct usb_ep *ep, struct usb_request *req)
{
 u32 len, seq;
 if (req->status || req->actual != sizeof(aml->hdr)) {
  aml_reply("failed:header"); return;
 }
 memcpy(&aml->hdr, req->buf, sizeof(aml->hdr));
 len = le32_to_cpu(aml->hdr.userDataLen);
 seq = le32_to_cpu(aml->hdr.sequenceNo);
 if (!aml->active || !aml_valid_chunk(len,
     le16_to_cpu(aml->hdr.dataCheckAlgorithm), le16_to_cpu(aml->hdr.bulkInAckLen)) ||
     !aml_valid_write(aml->media_off, len, aml->media_size, aml->bd->blksz) ||
     (aml->have_seq && seq != aml->next_seq)) {
  aml_reply("failed:header"); return;
 }
 aml->seq = seq;
 aml->expect = len;
 aml->got = 0;
 aml->req_out->buf = aml->buf;
 aml->req_out->length = len;
 aml->req_out->complete = bulk_out_done;
 aml->receiving = true;
 if (usb_ep_queue(aml->ep_out, aml->req_out, 0)) {
  aml->receiving = false;
  aml->active = false;
  aml_reply("failed:queue");
 }
}

static int aml_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
 struct usb_composite_dev *cdev = f->config->cdev;
 struct usb_request *req = cdev->req;
 u16 len = le16_to_cpu(ctrl->wLength);
 bool in = ctrl->bRequestType & USB_DIR_IN;
 int value;
 if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_VENDOR ||
     aml->receiving || aml->reply_busy || len > cdev->bufsiz)
  return -EOPNOTSUPP;
 /* Keep the composite core's allocation and ownership for the entire lifetime. */
 switch (ctrl->bRequest) {
 case AM_REQ_IDENTIFY_HOST:
  if (!in) return -EOPNOTSUPP;
  memcpy(req->buf, "\x00\x08\x00\x10", 4);
  value = min_t(int, len, 4);
  break;
 case AM_REQ_BULKCMD:
  if (in || !len || len >= sizeof(aml->cmd)) return -EOPNOTSUPP;
  req->complete = bulkcmd_done;
  value = len;
  break;
 case AM_REQ_DOWNLOAD:
  if (in || len != sizeof(aml->hdr)) return -EOPNOTSUPP;
  req->complete = hdr_done;
  value = len;
  break;
 case AM_REQ_NOP:
  if (len) return -EOPNOTSUPP;
  value = 0;
  break;
 default:
  return -EOPNOTSUPP;
 }
 req->length = value;
 req->zero = value < len;
 return usb_ep_queue(cdev->gadget->ep0, req, 0);
}

static int aml_config_setup(struct usb_configuration *c,
                            const struct usb_ctrlrequest *ctrl)
{
 return aml_setup(&aml->usb_function, ctrl);
}

static int aml_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_gadget *g = c->cdev->gadget;
	int id = usb_interface_id(c, f);

	if (id < 0)
		return id;
	intf_desc.bInterfaceNumber = id;
	fs_in.bEndpointAddress = USB_DIR_IN;
	fs_out.bEndpointAddress = USB_DIR_OUT;
	aml->ep_in = usb_ep_autoconfig(g, &fs_in);
	if (!aml->ep_in) return -ENODEV;
	aml->ep_in->driver_data = aml;
	aml->ep_out = usb_ep_autoconfig(g, &fs_out);
	if (!aml->ep_out) {
		aml->ep_in->driver_data = NULL;
		return -ENODEV;
	}
	aml->ep_out->driver_data = aml;
	hs_in.bEndpointAddress = fs_in.bEndpointAddress;
	hs_out.bEndpointAddress = fs_out.bEndpointAddress;
	f->descriptors = fs_funcs;
	f->hs_descriptors = hs_funcs;
	aml->req_in = usb_ep_alloc_request(aml->ep_in, 0);
	aml->req_out = usb_ep_alloc_request(aml->ep_out, 0);
	if (!aml->req_in || !aml->req_out) {
		aml->ep_in->driver_data = NULL;
		aml->ep_out->driver_data = NULL;
		if (aml->req_in) usb_ep_free_request(aml->ep_in, aml->req_in);
		if (aml->req_out) usb_ep_free_request(aml->ep_out, aml->req_out);
		return -ENOMEM;
	}
	aml->req_in->buf = memalign(CONFIG_SYS_CACHELINE_SIZE, AML_REPLY_SIZE);
	aml->buf = memalign(CONFIG_SYS_CACHELINE_SIZE, AML_MAX_CHUNK);
	if (!aml->req_in->buf || !aml->buf) {
		aml->ep_in->driver_data = NULL;
		aml->ep_out->driver_data = NULL;
		free(aml->buf);
		free(aml->req_in->buf);
		usb_ep_free_request(aml->ep_in, aml->req_in);
		usb_ep_free_request(aml->ep_out, aml->req_out);
		return -ENOMEM;
	}
	return 0;
}

static void aml_unbind(struct usb_configuration *c, struct usb_function *f)
{
	aml->ep_in->driver_data = NULL;
	aml->ep_out->driver_data = NULL;
	free(aml->buf);
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
	ret = usb_ep_enable(aml->ep_out, g->speed == USB_SPEED_HIGH ? &hs_out : &fs_out);
	if (ret) usb_ep_disable(aml->ep_in);
	return ret;
}

static void aml_disable(struct usb_function *f)
{
	aml->active = false;
	aml->receiving = false;
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
	c->setup = aml_config_setup;	/* fallback: composite may not route vendor/device */
	return usb_add_function(c, &aml->usb_function);
}

DECLARE_GADGET_BIND_CALLBACK(usb_dnl_aml_burn, aml_burn_add);
DECLARE_GADGET_BIND_CALLBACK(aml_burn, aml_burn_add);
