// SPDX-License-Identifier: GPL-2.0-only
/*
 * atrios-ledpanel.c - front 25x16 LED panel (Gowin FPGA) framebuffer driver
 *
 * The Yandex Station Max has a 25x16 white LED matrix in front of the speaker,
 * driven by a Gowin FPGA attached to spicc1.  Two vendor modules can talk to
 * it, and the DT node ("ya,led_screen", "atri,led-panel") exists in both
 * vendor stacks:
 *
 *   gowin_led_device.ko - framebuffer + backlight + JTAG, the module that
 *                         actually binds on this board.  Its application
 *                         protocol is the one below as "PA".
 *   quasar_led_screen.ko - newer protocol ("SA"), an FPGA-free framebuffer
 *                         replacement plus application firmware update over
 *                         SPI.  Kept as the second backend because the same
 *                         compatible may meet such a firmware revision.
 *
 * Both speak the same physical framing (SPI mode 0, 8 bit, CS asserted for a
 * whole command, CRC-16/XMODEM over the command, sent MSB first, big endian
 * u16 "next frame delay") and both expect one byte of panel luminance per LED,
 * but they differ in the status exchange.  The backend is therefore detected
 * at probe time; see ledpanel_detect().
 *
 * Frame payload (identical for both backends):
 *
 *   xres*yres bytes, row major, x fastest, one byte per LED, the value being
 *   max(red, green, blue) of the RGB888 framebuffer pixel, each channel
 *   rescaled to 8 bits using its fb_var_screeninfo length.
 *
 * PA (gowin_led_device.ko, application_mode.c):
 *
 *   07 00 <delay u16 le> <image> <crc16 be>          (bits 4.. of the packet)
 *   status: a 35-byte pure read (tx_buf = NULL); the CRC is verified as a
 *   residual (crc_itu_t(0, buf, 35) == 0) and application mode is buf[1] (1).
 *   buf[3..4] board_rev, buf[2] fw_version, buf[15] frames_in_queue,
 *   buf[16] last_cmd_status (bit mask, bit 3 "frame queue overflow" is not an
 *   error, bits 1/2 mean "retry", anything else means "failed").
 *
 * SA (quasar_led_screen.ko, screen_app_api.c):
 *
 *   07 <image> <delay u16 le> <crc16 be>
 *   status: 20 zero bytes + crc16; response bytes 0..15 are api_version, xres,
 *   yres, pixel_depth, frame_queue_overflows, frames_in_queue, temp_celsius,
 *   fw_version, last_cmd_status, reserved[5], and its CRC lives in bytes 14..15
 *   and is compared LITTLE endian (the vendor parser does a plain ldrh there,
 *   unlike the big endian command CRC - an inconsistency we reproduce).
 *   Only API 5, 6, 7 and 9 are supported and only 9 uses the CRC write path.
 *
 * Command ids (both backends): 0x07 show picture, 0x1b set brightness
 * (1 byte), 0x24 animation, 0x2d flush, 0x36 CRC mode, 0x0b reboot.
 *
 * Userspace (/usr/lib/atrios/screend) writes 25*16*3 = 1200 bytes of RGB888 to
 * the framebuffer and closes the file.  We convert and push from a delayed
 * work item so bursts of writes are coalesced; mmap clients are served by
 * pushing from fb_pan_display() and fb_release() as well.
 *
 * Not ported: FPGA bitstream/JTAG handling (needs a pinctrl handoff for the
 * pins shared with spicc1 and is only needed to reflash the panel) and the
 * touch/UART/gpio expander functions of the vendor driver.
 */

#include <linux/backlight.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define LEDPANEL_NAME			"atrios-ledpanel"

#define LEDPANEL_DEFAULT_WIDTH		25
#define LEDPANEL_DEFAULT_HEIGHT		16
#define LEDPANEL_BPP			24
#define LEDPANEL_MAX_DIM		255

#define LEDPANEL_DEFAULT_MAX_BRIGHTNESS	200
#define LEDPANEL_DEFAULT_SPI_HZ		1000000
#define LEDPANEL_DEFAULT_REFRESH_MS	20

#define LEDPANEL_CMD_SHOW_PIC		0x07
#define LEDPANEL_CMD_SET_BRIGHTNESS	0x1b

/* gowin_led_device: application mode marker expected in status byte 1. */
#define LEDPANEL_PA_APP_MODE		1

/* SA last_cmd_status values. */
#define LEDPANEL_SA_STATUS_OK		0x00
#define LEDPANEL_SA_STATUS_CRC_ERROR	0x15

/* PA last_cmd_status bit mask. */
#define LEDPANEL_PA_CMD_ERROR		BIT(0)
#define LEDPANEL_PA_CRC_ERROR		BIT(1)
#define LEDPANEL_PA_CMD_OVF		BIT(2)
#define LEDPANEL_PA_FRM_OVF		BIT(3)
#define LEDPANEL_PA_SPKT_ERROR		BIT(4)
#define LEDPANEL_PA_LPKT_ERROR		BIT(5)
#define LEDPANEL_PA_UART_OVF		BIT(6)

#define LEDPANEL_SA_RESPONSE_LEN	20
#define LEDPANEL_SA_RESPONSE_CRC_LEN	14
#define LEDPANEL_PA_RESPONSE_LEN	35
#define LEDPANEL_PA_STATUS_OFFSET	16
#define LEDPANEL_FRAME_DELAY_LEN	2

#define LEDPANEL_INFO_RETRIES		10
#define LEDPANEL_INFO_RETRY_MS		10
#define LEDPANEL_DETECT_RETRIES		3
#define LEDPANEL_RESET_MS		10
#define LEDPANEL_BOOT_MS		2000

/* frame_format=0: one luminance byte per LED (vendor behaviour). */
#define LEDPANEL_FORMAT_LUMINANCE	0
/*
 * frame_format=1: raw RGB888 passthrough, if some firmware revision does the
 * max(R,G,B) reduction in hardware.  Experimental.
 */
#define LEDPANEL_FORMAT_RGB888		1

enum ledpanel_proto {
	LEDPANEL_PROTO_PA,	/* gowin_led_device.ko, 35-byte status */
	LEDPANEL_PROTO_SA,	/* quasar_led_screen.ko, 20-byte status */
};

static unsigned int refresh_ms = LEDPANEL_DEFAULT_REFRESH_MS;
module_param(refresh_ms, uint, 0444);
MODULE_PARM_DESC(refresh_ms, "Minimum delay between two frame pushes (ms)");

static unsigned int spi_hz = LEDPANEL_DEFAULT_SPI_HZ;
module_param(spi_hz, uint, 0444);
MODULE_PARM_DESC(spi_hz, "SPI clock in Hz (vendors use 1 or 4 MHz, see docs)");

static unsigned int frame_format = LEDPANEL_FORMAT_LUMINANCE;
module_param(frame_format, uint, 0444);
MODULE_PARM_DESC(frame_format, "0 = 8bpp luminance (default), 1 = raw RGB888");

static bool check_status = true;
module_param(check_status, bool, 0444);
MODULE_PARM_DESC(check_status, "Fetch and verify the status word after each command");

static int proto = -1;
module_param(proto, int, 0444);
MODULE_PARM_DESC(proto, "0 = gowin (PA), 1 = quasar (SA), -1 = autodetect");

struct ledpanel_status {
	u8 api_version;
	u8 fw_version;
	u8 xres;
	u8 yres;
	u8 pixel_depth;
	u8 temp_celsius;
	u8 status;
};

struct ledpanel {
	struct spi_device *spi;
	struct fb_info *info;
	struct gpio_desc *reset;
	struct backlight_device *bl;

	enum ledpanel_proto proto;
	u8 api_version;
	u8 fw_version;
	bool crc;
	bool removed;

	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int npixels;
	unsigned int payload_len;
	unsigned int tx_len;
	unsigned int format;
	unsigned int brightness;
	unsigned int max_brightness;
	u16 next_frame_delay;

	u8 *tx;
	u8 *payload;
	u8 *shadow;

	struct mutex lock;	/* serialises SPI access and ->tx/->payload */
	struct delayed_work push;
};

/*
 * CRC-16/XMODEM, bit identical to crc_itu_t(0, buf, len) from lib/crc/.
 * Kept local so the driver does not depend on the CONFIG_CRC_ITU_T tristate,
 * which is only meaningful for out-of-tree users.
 */
static u16 ledpanel_crc16(const u8 *buf, size_t len)
{
	u16 crc = 0;
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= (u16)buf[i] << 8;
		for (bit = 0; bit < 8; bit++)
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
	}

	return crc;
}

static void ledpanel_schedule_push(struct ledpanel *lp)
{
	mutex_lock(&lp->lock);
	if (!lp->removed)
		mod_delayed_work(system_wq, &lp->push,
				 msecs_to_jiffies(refresh_ms));
	mutex_unlock(&lp->lock);
}

/*
 * Caller must hold lp->lock.  Appends the CRC when the panel firmware expects
 * one and clocks the whole command out as a single SPI transfer.
 */
static int ledpanel_write_locked(struct ledpanel *lp, size_t len)
{
	u16 crc;

	if (len + 2 > lp->tx_len)
		return -EINVAL;

	if (lp->crc) {
		crc = ledpanel_crc16(lp->tx, len);
		lp->tx[len++] = crc >> 8;
		lp->tx[len++] = crc & 0xff;
	}

	return spi_write(lp->spi, lp->tx, len);
}

static int ledpanel_xfer(struct ledpanel *lp, const void *tx, void *rx,
			 unsigned int len)
{
	struct spi_transfer xfer = { };
	struct spi_message msg;

	xfer.tx_buf = tx;
	xfer.rx_buf = rx;
	xfer.len = len;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(lp->spi, &msg);
}

/* PA: one idle read of 35 bytes, CRC checked as a residual. */
static int ledpanel_fetch_status_pa(struct ledpanel *lp,
				    struct ledpanel_status *out)
{
	u8 buf[LEDPANEL_PA_RESPONSE_LEN];
	int ret;

	ret = ledpanel_xfer(lp, NULL, buf, sizeof(buf));
	if (ret)
		return ret;

	if (ledpanel_crc16(buf, sizeof(buf)))
		return -EBADMSG;

	if (out) {
		out->api_version = buf[1];
		out->fw_version = buf[2];
		out->status = buf[LEDPANEL_PA_STATUS_OFFSET];
	}

	return 0;
}

/* SA: 20 zero bytes plus CRC, response has its own little endian CRC. */
static int ledpanel_fetch_status_sa(struct ledpanel *lp,
				    struct ledpanel_status *out)
{
	u8 tx[LEDPANEL_SA_RESPONSE_LEN + 2];
	u8 rx[LEDPANEL_SA_RESPONSE_LEN + 2];
	unsigned int len = LEDPANEL_SA_RESPONSE_LEN;
	u16 crc;
	int ret;

	memset(tx, 0, len);
	crc = ledpanel_crc16(tx, len);
	tx[len++] = crc >> 8;
	tx[len++] = crc & 0xff;
	memset(rx, 0, sizeof(rx));

	ret = ledpanel_xfer(lp, tx, rx, len);
	if (ret)
		return ret;

	crc = ledpanel_crc16(rx, LEDPANEL_SA_RESPONSE_CRC_LEN);
	if (crc != (rx[LEDPANEL_SA_RESPONSE_CRC_LEN] |
		    rx[LEDPANEL_SA_RESPONSE_CRC_LEN + 1] << 8))
		return -EBADMSG;

	if (out) {
		out->api_version = rx[0];
		out->xres = rx[1];
		out->yres = rx[2];
		out->pixel_depth = rx[3];
		out->temp_celsius = rx[6];
		out->fw_version = rx[7];
		out->status = rx[8];
	}

	return 0;
}

static int ledpanel_fetch_status(struct ledpanel *lp,
				 struct ledpanel_status *out)
{
	if (lp->proto == LEDPANEL_PROTO_PA)
		return ledpanel_fetch_status_pa(lp, out);

	return ledpanel_fetch_status_sa(lp, out);
}

static const char *ledpanel_str_status(struct ledpanel *lp, u8 status)
{
	if (lp->proto == LEDPANEL_PROTO_PA) {
		if (!status)
			return "OK";
		if (status & LEDPANEL_PA_CRC_ERROR)
			return "CRC_ERROR";
		if (status & LEDPANEL_PA_CMD_OVF)
			return "CMD_OVF_ERROR";
		if (status & LEDPANEL_PA_CMD_ERROR)
			return "COMMAND_ERROR";
		return "UNKNOWN";
	}

	switch (status) {
	case LEDPANEL_SA_STATUS_OK:
		return "OK";
	case LEDPANEL_SA_STATUS_CRC_ERROR:
		return "CRC_ERROR";
	default:
		return "UNKNOWN";
	}
}

/* Returns 0 when the panel accepted the last command. */
static int ledpanel_check_status(struct ledpanel *lp)
{
	struct ledpanel_status st;
	int ret;

	if (!check_status)
		return 0;

	ret = ledpanel_fetch_status(lp, &st);
	if (ret)
		return ret;

	/*
	 * PA reports the frame queue overflow bit during normal animation
	 * playback; the vendor driver masks it out instead of failing.
	 */
	if (lp->proto == LEDPANEL_PROTO_PA && st.status & ~LEDPANEL_PA_FRM_OVF) {
		dev_warn_ratelimited(&lp->spi->dev,
				     "panel rejected the command: %s[%02x]\n",
				     ledpanel_str_status(lp, st.status),
				     st.status);
		return -EIO;
	}

	if (lp->proto == LEDPANEL_PROTO_SA &&
	    st.status != LEDPANEL_SA_STATUS_OK) {
		dev_warn_ratelimited(&lp->spi->dev,
				     "panel rejected the command: %s[%02x]\n",
				     ledpanel_str_status(lp, st.status),
				     st.status);
		return -EIO;
	}

	return 0;
}

/*
 * The vendor table at .rodata+0x308 marks API 5, 6, 7 and 9 supported (8 is
 * not), and only API 9 uses the CRC write path (sa_write_v9).
 */
static bool ledpanel_api_supported(u8 api)
{
	return api == 5 || api == 6 || api == 7 || api == 9;
}

static unsigned int ledpanel_chan(u32 pixel, const struct fb_bitfield *bf)
{
	u32 mask;

	if (!bf->length)
		return 0;

	mask = (1U << bf->length) - 1;

	return ((pixel >> bf->offset) & mask) * 255 / mask;
}

static u8 ledpanel_luminance(const struct ledpanel *lp, const u8 *px)
{
	const struct fb_var_screeninfo *var = &lp->info->var;
	u32 pixel = px[0] | px[1] << 8 | px[2] << 16;
	unsigned int r, g, b;

	r = ledpanel_chan(pixel, &var->red);
	g = ledpanel_chan(pixel, &var->green);
	b = ledpanel_chan(pixel, &var->blue);

	return max3(r, g, b);
}

static void ledpanel_convert(struct ledpanel *lp)
{
	const struct fb_info *info = lp->info;
	const u8 *src = info->screen_buffer;
	unsigned int x, y;

	if (lp->format == LEDPANEL_FORMAT_RGB888) {
		memcpy(lp->payload, src, lp->payload_len);
		return;
	}

	for (y = 0; y < lp->height; y++) {
		const u8 *line = src + (size_t)y * info->fix.line_length;
		u8 *dst = lp->payload + (size_t)y * lp->width;

		for (x = 0; x < lp->width; x++)
			dst[x] = ledpanel_luminance(lp, line + x * 3);
	}
}

/*
 * Caller must hold lp->lock.  Returns 0 when the frame was sent or when
 * nothing changed since the last successful push.
 */
static int ledpanel_send_frame_locked(struct ledpanel *lp)
{
	unsigned int len;
	int ret;

	ledpanel_convert(lp);

	if (!memcmp(lp->payload, lp->shadow, lp->payload_len))
		return 0;

	lp->tx[0] = LEDPANEL_CMD_SHOW_PIC;
	if (lp->proto == LEDPANEL_PROTO_PA) {
		lp->tx[1] = 0;
		lp->tx[2] = lp->next_frame_delay & 0xff;
		lp->tx[3] = lp->next_frame_delay >> 8;
		memcpy(lp->tx + 4, lp->payload, lp->payload_len);
		len = 4 + lp->payload_len;
	} else {
		memcpy(lp->tx + 1, lp->payload, lp->payload_len);
		len = 1 + lp->payload_len;
		lp->tx[len++] = lp->next_frame_delay & 0xff;
		lp->tx[len++] = lp->next_frame_delay >> 8;
	}

	ret = ledpanel_write_locked(lp, len);
	if (ret)
		return ret;

	ret = ledpanel_check_status(lp);
	if (ret)
		return ret;

	memcpy(lp->shadow, lp->payload, lp->payload_len);

	return 0;
}

static void ledpanel_push_work(struct work_struct *work)
{
	struct ledpanel *lp = container_of(to_delayed_work(work),
					   struct ledpanel, push);
	int ret;

	mutex_lock(&lp->lock);
	ret = ledpanel_send_frame_locked(lp);
	mutex_unlock(&lp->lock);

	if (ret)
		dev_warn_ratelimited(&lp->spi->dev,
				     "failed to update frame: %d\n", ret);
}

/* --- framebuffer ------------------------------------------------------- */

static int ledpanel_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct ledpanel *lp = info->par;
	unsigned int bytes;

	if (var->xres != lp->width || var->yres != lp->height)
		return -EINVAL;

	if (var->bits_per_pixel != LEDPANEL_BPP)
		return -EINVAL;

	var->xres_virtual = lp->width;
	var->yres_virtual = lp->height;
	var->xoffset = 0;
	var->yoffset = 0;

	/*
	 * The panel always takes RGB888; the conversion in ledpanel_convert()
	 * follows these fields.
	 */
	var->red.offset = 16;
	var->red.length = 8;
	var->red.msb_right = 0;
	var->green.offset = 8;
	var->green.length = 8;
	var->green.msb_right = 0;
	var->blue.offset = 0;
	var->blue.length = 8;
	var->blue.msb_right = 0;
	var->transp.offset = 0;
	var->transp.length = 0;

	bytes = var->yres_virtual * var->xres_virtual *
		DIV_ROUND_UP(var->bits_per_pixel, 8);
	if (bytes > info->screen_size)
		return -ENOMEM;

	return 0;
}

static int ledpanel_set_par(struct fb_info *info)
{
	struct ledpanel *lp = info->par;

	info->fix.line_length = lp->stride;

	return 0;
}

static ssize_t ledpanel_write(struct fb_info *info, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct ledpanel *lp = info->par;
	ssize_t ret;

	ret = fb_sys_write(info, buf, count, ppos);
	if (ret > 0)
		ledpanel_schedule_push(lp);

	return ret;
}

static int ledpanel_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	ledpanel_schedule_push(info->par);

	return 0;
}

static int ledpanel_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return remap_vmalloc_range(vma, info->screen_buffer, vma->vm_pgoff);
}

static int ledpanel_release(struct fb_info *info, int user)
{
	ledpanel_schedule_push(info->par);

	return 0;
}

static void ledpanel_destroy(struct fb_info *info)
{
	struct ledpanel *lp = info->par;

	cancel_delayed_work_sync(&lp->push);
	vfree(info->screen_buffer);
	info->screen_buffer = NULL;
}

static const struct fb_ops ledpanel_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= fb_sys_read,
	.fb_write	= ledpanel_write,
	.fb_check_var	= ledpanel_check_var,
	.fb_set_par	= ledpanel_set_par,
	.fb_pan_display	= ledpanel_pan_display,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_mmap	= ledpanel_mmap,
	.fb_release	= ledpanel_release,
	.fb_destroy	= ledpanel_destroy,
};

/* --- backlight --------------------------------------------------------- */

static int ledpanel_bl_update_status(struct backlight_device *bd)
{
	struct ledpanel *lp = bl_get_data(bd);
	int brightness = backlight_get_brightness(bd);
	int ret;

	if (brightness > lp->max_brightness)
		brightness = lp->max_brightness;

	mutex_lock(&lp->lock);
	if (lp->removed) {
		mutex_unlock(&lp->lock);
		return -ENODEV;
	}
	lp->tx[0] = LEDPANEL_CMD_SET_BRIGHTNESS;
	lp->tx[1] = brightness;
	ret = ledpanel_write_locked(lp, 2);
	if (!ret)
		ret = ledpanel_check_status(lp);
	if (!ret)
		lp->brightness = brightness;
	mutex_unlock(&lp->lock);

	if (ret)
		dev_warn_ratelimited(&lp->spi->dev,
				     "failed to set brightness %d: %d\n",
				     brightness, ret);

	return ret;
}

static const struct backlight_ops ledpanel_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= ledpanel_bl_update_status,
};

/* --- probe / remove ---------------------------------------------------- */

static int ledpanel_reset(struct ledpanel *lp)
{
	struct device *dev = &lp->spi->dev;

	lp->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lp->reset))
		return dev_err_probe(dev, PTR_ERR(lp->reset),
				     "failed to get reset gpio\n");

	if (!lp->reset) {
		dev_warn(dev, "no reset gpio, assuming the panel is running\n");
		return 0;
	}

	/* reset-gpios is active low, so logical 1 asserts the line. */
	gpiod_set_value_cansleep(lp->reset, 1);
	msleep(LEDPANEL_RESET_MS);
	gpiod_set_value_cansleep(lp->reset, 0);
	msleep(LEDPANEL_BOOT_MS);

	return 0;
}

/*
 * Both vendor firmwares answer the shared DT node, so find out which protocol
 * is alive.  gowin_led_device answers an idle 35-byte read; if that does not
 * look like a valid PA status block, try the quasar_led_screen poll command.
 */
static int ledpanel_detect(struct ledpanel *lp, struct ledpanel_status *st)
{
	struct device *dev = &lp->spi->dev;
	int ret = -ENODEV;
	int i;

	if (proto < 0 || proto == LEDPANEL_PROTO_PA) {
		lp->proto = LEDPANEL_PROTO_PA;
		for (i = 0; i < LEDPANEL_DETECT_RETRIES; i++) {
			ret = ledpanel_fetch_status(lp, st);
			if (!ret && st->api_version == LEDPANEL_PA_APP_MODE)
				return 0;
			msleep(LEDPANEL_INFO_RETRY_MS);
		}
		if (!ret)
			ret = -ENODEV;
	}

	if (proto < 0 || proto == LEDPANEL_PROTO_SA) {
		lp->proto = LEDPANEL_PROTO_SA;
		for (i = 0; i < LEDPANEL_INFO_RETRIES; i++) {
			ret = ledpanel_fetch_status(lp, st);
			if (!ret && ledpanel_api_supported(st->api_version))
				return 0;
			msleep(LEDPANEL_INFO_RETRY_MS);
		}
		if (!ret)
			ret = -ENODEV;
	}

	dev_err(dev, "no known panel firmware found: %d\n", ret);

	return ret;
}

static int ledpanel_fb_init(struct ledpanel *lp)
{
	struct device *dev = &lp->spi->dev;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(0, dev);
	if (!info)
		return -ENOMEM;

	info->par = lp;
	lp->info = info;

	info->screen_size = PAGE_ALIGN(lp->stride * lp->height);
	info->screen_buffer = vzalloc(info->screen_size);
	if (!info->screen_buffer) {
		ret = -ENOMEM;
		goto err_release;
	}

	info->fbops = &ledpanel_fb_ops;
	info->flags = FBINFO_VIRTFB;
	info->var.activate = FB_ACTIVATE_NOW;

	info->var.xres = lp->width;
	info->var.yres = lp->height;
	info->var.xres_virtual = lp->width;
	info->var.yres_virtual = lp->height;
	info->var.bits_per_pixel = LEDPANEL_BPP;
	info->var.red.offset = 16;
	info->var.red.length = 8;
	info->var.green.offset = 8;
	info->var.green.length = 8;
	info->var.blue.offset = 0;
	info->var.blue.length = 8;

	strscpy(info->fix.id, LEDPANEL_NAME, sizeof(info->fix.id));
	info->fix.smem_len = info->screen_size;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = lp->stride;
	info->fix.accel = FB_ACCEL_NONE;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_free_screen;

	ret = register_framebuffer(info);
	if (ret)
		goto err_free_cmap;

	dev_info(dev, "%ux%u %dbpp framebuffer '%s'\n", lp->width, lp->height,
		 LEDPANEL_BPP, info->fix.id);

	return 0;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_free_screen:
	vfree(info->screen_buffer);
	info->screen_buffer = NULL;
err_release:
	framebuffer_release(info);
	lp->info = NULL;

	return ret;
}

static int ledpanel_probe(struct spi_device *spi)
{
	struct ledpanel_status st = { };
	struct backlight_properties props = { };
	struct device *dev = &spi->dev;
	struct ledpanel *lp;
	const char *bl_name;
	u32 value;
	int ret;

	lp = devm_kzalloc(dev, sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->spi = spi;
	mutex_init(&lp->lock);
	INIT_DELAYED_WORK(&lp->push, ledpanel_push_work);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (spi_hz && (!spi->max_speed_hz || spi_hz < spi->max_speed_hz))
		spi->max_speed_hz = spi_hz;
	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(dev, ret, "failed to setup SPI\n");

	ret = ledpanel_reset(lp);
	if (ret)
		return ret;

	ret = ledpanel_detect(lp, &st);
	if (ret)
		return ret;

	lp->api_version = st.api_version;
	lp->fw_version = st.fw_version;
	lp->crc = lp->proto == LEDPANEL_PROTO_SA && st.api_version >= 9;

	/*
	 * gowin_led_device keeps the geometry in a board_rev lookup table
	 * instead of reporting it, so take it from DT there.
	 */
	if (lp->proto == LEDPANEL_PROTO_PA) {
		if (device_property_read_u32(dev, "width", &value))
			value = LEDPANEL_DEFAULT_WIDTH;
		lp->width = value;
		if (device_property_read_u32(dev, "height", &value))
			value = LEDPANEL_DEFAULT_HEIGHT;
		lp->height = value;
	} else {
		lp->width = st.xres;
		lp->height = st.yres;
	}

	if (!lp->width || !lp->height ||
	    lp->width > LEDPANEL_MAX_DIM || lp->height > LEDPANEL_MAX_DIM) {
		dev_warn(dev, "implausible geometry %ux%u, using %ux%u\n",
			 lp->width, lp->height, LEDPANEL_DEFAULT_WIDTH,
			 LEDPANEL_DEFAULT_HEIGHT);
		lp->width = LEDPANEL_DEFAULT_WIDTH;
		lp->height = LEDPANEL_DEFAULT_HEIGHT;
	}

	dev_info(dev, "%s protocol, firmware version=%u, %ux%u\n",
		 lp->proto == LEDPANEL_PROTO_PA ? "gowin" : "quasar",
		 lp->fw_version, lp->width, lp->height);

	if (lp->proto == LEDPANEL_PROTO_SA)
		dev_info(dev, "api_version=%u, pixel_depth=%u, temp=%uC\n",
			 lp->api_version, st.pixel_depth, st.temp_celsius);

	lp->stride = lp->width * 3;
	lp->npixels = lp->width * lp->height;
	lp->format = frame_format;
	if (lp->format > LEDPANEL_FORMAT_RGB888) {
		dev_warn(dev, "unknown frame_format %u, using luminance\n",
			 lp->format);
		lp->format = LEDPANEL_FORMAT_LUMINANCE;
	}
	lp->payload_len = lp->format == LEDPANEL_FORMAT_RGB888 ?
			  lp->npixels * 3 : lp->npixels;

	lp->tx_len = 4 + lp->payload_len + LEDPANEL_FRAME_DELAY_LEN + 2;
	lp->tx = devm_kzalloc(dev, lp->tx_len, GFP_KERNEL);
	lp->payload = devm_kzalloc(dev, lp->payload_len, GFP_KERNEL);
	lp->shadow = devm_kzalloc(dev, lp->payload_len, GFP_KERNEL);
	if (!lp->tx || !lp->payload || !lp->shadow)
		return -ENOMEM;

	/* Pretend the last sent frame was all ones so the first push is sent. */
	memset(lp->shadow, 0xff, lp->payload_len);

	if (device_property_read_u32(dev, "max-brightness", &value))
		value = LEDPANEL_DEFAULT_MAX_BRIGHTNESS;
	lp->max_brightness = min_t(u32, value, 255);
	lp->brightness = lp->max_brightness;

	ret = ledpanel_fb_init(lp);
	if (ret)
		return ret;

	spi_set_drvdata(spi, lp);

	bl_name = dev->of_node ? dev->of_node->name : "led-panel";
	props.type = BACKLIGHT_RAW;
	props.max_brightness = lp->max_brightness;
	props.brightness = lp->brightness;
	lp->bl = devm_backlight_device_register(dev, bl_name, dev, lp,
						&ledpanel_bl_ops, &props);
	if (IS_ERR(lp->bl)) {
		ret = PTR_ERR(lp->bl);
		lp->bl = NULL;
		goto err_unregister_fb;
	}

	/*
	 * Push the default brightness once; otherwise the panel keeps whatever
	 * the bootloader left behind.
	 */
	backlight_update_status(lp->bl);

	return 0;

err_unregister_fb:
	unregister_framebuffer(lp->info);
	fb_dealloc_cmap(&lp->info->cmap);
	framebuffer_release(lp->info);
	lp->info = NULL;

	return ret;
}

static void ledpanel_remove(struct spi_device *spi)
{
	struct ledpanel *lp = spi_get_drvdata(spi);
	struct fb_info *info;

	if (!lp)
		return;

	spi_set_drvdata(spi, NULL);

	/*
	 * Stop the worker before devres frees ->tx/->payload/->shadow on
	 * unbind; it is also cancelled from ledpanel_destroy() for the case
	 * where userspace keeps the file open.
	 */
	mutex_lock(&lp->lock);
	lp->removed = true;
	mutex_unlock(&lp->lock);
	cancel_delayed_work_sync(&lp->push);

	info = lp->info;
	if (!info)
		return;

	/*
	 * screen_buffer is freed from ledpanel_destroy() once the last user
	 * is gone; here we only drop the registration reference.
	 */
	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	framebuffer_release(info);
	lp->info = NULL;
}

static const struct of_device_id ledpanel_of_match[] = {
	{ .compatible = "yandex,led-screen" },
	{ .compatible = "atri,led-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, ledpanel_of_match);

static const struct spi_device_id ledpanel_spi_ids[] = {
	{ "led-screen", 0 },
	{ "led-panel", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ledpanel_spi_ids);

static struct spi_driver ledpanel_driver = {
	.probe		= ledpanel_probe,
	.remove		= ledpanel_remove,
	.id_table	= ledpanel_spi_ids,
	.driver = {
		.name		= LEDPANEL_NAME,
		.of_match_table	= ledpanel_of_match,
	},
};
module_spi_driver(ledpanel_driver);

MODULE_AUTHOR("AtriOS mainline port");
MODULE_DESCRIPTION("AtriOS front LED panel (25x16, SPI) framebuffer driver");
MODULE_LICENSE("GPL");
