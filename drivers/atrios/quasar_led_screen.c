// SPDX-License-Identifier: GPL-2.0
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/crc-itu-t.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define DRIVER_NAME "quasar_led_screen"

#define SA_RESP_MAGIC		0xFE
#define SA_SPI_SPEED		1000000
#define FW_CHUNK_SIZE		0x8000
#define BL_DELAY_CYCLES		1000
#define BL_DELAY_ITERATIONS	50
#define SA_CMD_BUF_SIZE		64
#define SA_RESP_BUF_SIZE	64

#define SA_CMD_NONE		0x16
#define SA_CMD_REBOOT		0x0B
#define SA_CMD_TEST		0x04
#define SA_CMD_SHOW_PIC		0x07
#define SA_CMD_SAVE_TEST	0x09
#define SA_CMD_SET_BRIGHTNESS	0x1B
#define SA_CMD_START_BOOT_ANIM	0x24
#define SA_CMD_FLUSH_BUFFER	0x2D
#define SA_CMD_SET_CRC_CHECK	0x36

#define BL_CMD_RESTART		0xBE
#define BL_CMD_RUN_APP		0xB5
#define BL_CMD_WRITE		0xB3

#define BL_STATE_READY		0
#define BL_STATE_BUSY		3
#define BL_STATE_BOOT		4
#define BL_STATE_DONE		5

#define QLS_XRES		25
#define QLS_YRES		16
#define QLS_BPP			3

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose Quasar LED screen SPI and protocol logging");

struct sa_device {
	struct spi_device	*spi;
	int			crc_check;
	int			fw_version;
	int			api_version;
	u8			cmdbuf[SA_CMD_BUF_SIZE];
	u8			respbuf[SA_RESP_BUF_SIZE];
	struct mutex		lock;
};

struct bl_info {
	u8  magic;
	u8  fw_version;
	u32 state;
	u32 info_lo;
	u64 info;
	u16 crc;
};

struct sa_response {
	u8  magic;
	u8  fw_version;
	u8  fields[6];
	u32 field8;
	u32 field9;
	u32 field10;
	u16 crc;
};

struct qls_priv {
	struct spi_device	*spi;
	struct sa_device	*sa;
	struct fb_info		*fb;
	struct backlight_device	*bl;
	struct gpio_desc	*reset;

	u8			*videomemory;
	u32			xres;
	u32			yres;
	u32			pixel_depth;

	int			max_brightness;
	int			brightness;

	u16			fw_version;
	u8			min_api_version;
	u8			max_api_version;
	u8			fw_crc;
	u16			expected_crc;

	u32			reset_count;
	int			frames_in_queue;
	int			frame_queue_overflows;
	int			next_frame_delay;
	int			update_result;
	int			disable_test_mode;
	u8			screen_fw_after_start;

	unsigned long		spi_tx_count;
	unsigned long		spi_rx_count;
	unsigned long		spi_errors;
	unsigned long		crc_errors;
	unsigned long		sync_count;

	struct timer_list	flush_timer;
	struct work_struct	flush_work;
	bool			flush_pending;
};

static u16 qls_crc16(const u8 *data, int len)
{
	return crc_itu_t(0, data, len);
}

/* ---- SPI helpers ---- */

static int spi_send(struct spi_device *spi, const void *buf, size_t len)
{
	struct qls_priv *qls = spi_get_drvdata(spi);
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = buf,
		.len = len,
		.speed_hz = SA_SPI_SPEED,
	};
	int ret;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(spi, &msg);
	if (qls) {
		if (ret) {
			qls->spi_errors++;
			dev_err(&spi->dev, "[QLS-SPI] TX error (%zu bytes): %d\n",
				len, ret);
		} else {
			qls->spi_tx_count++;
		}
	}
	return ret;
}

static int spi_recv(struct spi_device *spi, void *buf, size_t len)
{
	struct qls_priv *qls = spi_get_drvdata(spi);
	struct spi_message msg;
	struct spi_transfer xfer = {
		.rx_buf = buf,
		.len = len,
		.speed_hz = SA_SPI_SPEED,
	};
	int ret;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(spi, &msg);
	if (qls) {
		if (ret) {
			qls->spi_errors++;
			dev_err(&spi->dev, "[QLS-SPI] RX error (%zu bytes): %d\n",
				len, ret);
		} else {
			qls->spi_rx_count++;
		}
	}
	return ret;
}

/* ---- SA protocol helpers ---- */

static int sa_send_tx(struct sa_device *sa, u8 cmd,
		      const u8 *data, int data_len)
{
	u8 buf[SA_CMD_BUF_SIZE];
	u16 crc;
	int txlen;

	buf[0] = cmd;
	if (data && data_len > 0)
		memcpy(buf + 1, data, data_len);

	txlen = 1 + data_len;
	crc = qls_crc16(buf, txlen);
	buf[txlen] = (crc >> 8) & 0xff;
	buf[txlen + 1] = crc & 0xff;
	txlen += 2;

	return spi_send(sa->spi, buf, txlen);
}

static int sa_send_recv(struct sa_device *sa, u8 cmd,
			const u8 *data, int data_len,
			u8 *resp, int resp_len)
{
	int ret;

	ret = sa_send_tx(sa, cmd, data, data_len);
	if (ret)
		return ret;

	return spi_recv(sa->spi, resp, resp_len);
}

static int sa_send_cmd_short(struct sa_device *sa, u8 cmd,
			     u8 param, int param_len)
{
	u8 buf[SA_CMD_BUF_SIZE];
	u16 crc;
	int txlen;

	buf[0] = cmd;
	if (param_len > 0) {
		buf[1] = param;
		txlen = 1 + param_len;
	} else {
		txlen = 1;
	}

	crc = qls_crc16(buf, txlen);
	buf[txlen] = (crc >> 8) & 0xff;
	buf[txlen + 1] = crc & 0xff;
	txlen += 2;

	return spi_send(sa->spi, buf, txlen);
}

static int sa_parse_response(struct qls_priv *qls, const u8 *buf, int len,
			     struct sa_response *out)
{
	u16 crc, crc_exp;

	if (len < 16 || len > SA_RESP_BUF_SIZE)
		return -EINVAL;

	memset(out, 0, sizeof(*out));

	out->magic	= buf[0];
	out->fw_version = buf[1];
	out->fields[0]	= buf[2];
	out->fields[1]	= buf[3];
	out->fields[2]	= buf[4];
	out->fields[3]	= buf[5];
	out->fields[4]	= buf[6];
	out->fields[5]	= buf[7];
	out->field8	= buf[8] | ((u32)buf[9] << 8) |
			  ((u32)buf[10] << 16) | ((u32)buf[11] << 24);
	out->field9	= buf[9] | ((u32)buf[10] << 8) |
			  ((u32)buf[11] << 16) | ((u32)buf[12] << 24);
	out->field10	= buf[10] | ((u32)buf[11] << 8) |
			  ((u32)buf[12] << 16) | ((u32)buf[13] << 24);

	crc_exp = buf[14] | ((u16)buf[15] << 8);
	crc = qls_crc16(buf, 14);

	if (crc != crc_exp) {
		if (qls)
			qls->crc_errors++;
		if (debug && qls)
			dev_warn(&qls->spi->dev,
				 "[QLS-SA] response CRC mismatch: calc=0x%04x != exp=0x%04x (raw: %16ph)\n",
				 crc, crc_exp, buf);
		return -EILSEQ;
	}

	return 0;
}

static int sa_none(struct sa_device *sa, u8 *resp, int resp_len)
{
	return sa_send_recv(sa, SA_CMD_NONE, NULL, 0, resp, resp_len);
}

static int sa_none_blocking(struct qls_priv *qls, struct sa_device *sa)
{
	struct sa_response resp;
	int retry = 10;
	int ret;

	do {
		ret = sa_send_recv(sa, SA_CMD_NONE, NULL, 0,
				   sa->respbuf, SA_RESP_BUF_SIZE);
		if (!ret) {
			ret = sa_parse_response(qls, sa->respbuf,
						SA_RESP_BUF_SIZE, &resp);
			if (!ret && resp.magic == SA_RESP_MAGIC) {
				sa->api_version = resp.fw_version;
				return 0;
			}
		}
		msleep(20);
	} while (--retry > 0);

	return -ETIMEDOUT;
}

static int __maybe_unused sa_reboot(struct sa_device *sa)
{
	return sa_send_cmd_short(sa, SA_CMD_REBOOT, 0, 0);
}

static int sa_set_brightness(struct sa_device *sa, u8 brightness)
{
	return sa_send_cmd_short(sa, SA_CMD_SET_BRIGHTNESS, brightness, 1);
}

static int sa_set_crc_check(struct sa_device *sa, u8 enable)
{
	return sa_send_cmd_short(sa, SA_CMD_SET_CRC_CHECK, enable, 1);
}

static int sa_flush_buffer(struct sa_device *sa)
{
	return sa_send_cmd_short(sa, SA_CMD_FLUSH_BUFFER, 0, 0);
}

static int sa_start_boot_animation(struct sa_device *sa)
{
	return sa_send_cmd_short(sa, SA_CMD_START_BOOT_ANIM, 0, 0);
}

static int __maybe_unused sa_test(struct sa_device *sa, const u8 *data, int len)
{
	u8 buf[SA_CMD_BUF_SIZE];
	u16 crc;
	int txlen;

	if (len < 1 || len + 3 > SA_CMD_BUF_SIZE)
		return -EINVAL;

	buf[0] = SA_CMD_TEST;
	memcpy(buf + 1, data, len);
	txlen = 1 + len;

	crc = qls_crc16(buf, txlen);
	buf[txlen] = (crc >> 8) & 0xff;
	buf[txlen + 1] = crc & 0xff;
	txlen += 2;

	return spi_send(sa->spi, buf, txlen);
}

static int __maybe_unused sa_show_pic(struct sa_device *sa, const u8 *data, int len)
{
	u8 buf[SA_CMD_BUF_SIZE];
	u16 crc;
	int txlen;

	if (len + 3 > SA_CMD_BUF_SIZE)
		return -EINVAL;

	buf[0] = SA_CMD_SHOW_PIC;
	memcpy(buf + 1, data, len);
	txlen = 1 + len;

	crc = qls_crc16(buf, txlen);
	buf[txlen] = (crc >> 8) & 0xff;
	buf[txlen + 1] = crc & 0xff;
	txlen += 2;

	return spi_send(sa->spi, buf, txlen);
}

/* ---- Bootloader protocol ---- */

static int bl_send_cmd(struct spi_device *spi, u8 cmd)
{
	u8 buf[3];
	u16 crc;

	buf[0] = cmd;
	crc = qls_crc16(buf, 1);
	buf[1] = crc & 0xff;
	buf[2] = (crc >> 8) & 0xff;

	return spi_send(spi, buf, 3);
}

static int bl_get_info(struct spi_device *spi, struct bl_info *info)
{
	u8 buf[28] = {0};
	u16 crc;
	int ret;

	memset(info, 0, sizeof(*info));

	ret = spi_send(spi, buf, 1);
	if (ret)
		return ret;

	ret = spi_recv(spi, buf, sizeof(buf));
	if (ret)
		return ret;

	if (buf[0] != SA_RESP_MAGIC)
		return -EIO;

	crc = qls_crc16(buf, 12);
	if ((buf[12] | ((u16)buf[13] << 8)) != crc)
		return -EILSEQ;

	info->magic = buf[0];
	info->fw_version = buf[1];
	info->state = buf[2] | ((u32)buf[3] << 8);
	info->info_lo = buf[4] | ((u32)buf[5] << 8) |
			((u32)buf[6] << 16) | ((u32)buf[7] << 24);
	info->info = info->info_lo;
	info->crc = buf[12] | ((u16)buf[13] << 8);

	return 0;
}

static int __maybe_unused bl_restart(struct spi_device *spi)
{
	struct bl_info info;
	int ret;

	ret = bl_get_info(spi, &info);
	if (ret)
		return ret;
	if (info.state == BL_STATE_BUSY)
		return -EBUSY;

	return bl_send_cmd(spi, BL_CMD_RESTART);
}

static int __maybe_unused bl_run_app(struct spi_device *spi)
{
	return bl_send_cmd(spi, BL_CMD_RUN_APP);
}

static int __maybe_unused bl_write_fw(struct spi_device *spi, const u8 *data, int len)
{
	struct bl_info info;
	int ret, offset = 0;

	while (offset < len) {
		int chunk = min(len - offset, FW_CHUNK_SIZE);
		u8 *buf;
		u16 crc;
		int i;

		ret = bl_get_info(spi, &info);
		if (ret)
			return ret;
		if (info.state == BL_STATE_BUSY) {
			msleep(10);
			continue;
		}

		buf = kmalloc(chunk + 3, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		buf[0] = BL_CMD_WRITE;
		memcpy(buf + 1, data + offset, chunk);

		crc = qls_crc16(buf, chunk + 1);
		buf[chunk + 1] = crc & 0xff;
		buf[chunk + 2] = (crc >> 8) & 0xff;

		for (i = 0; i < BL_DELAY_ITERATIONS; i++)
			udelay(BL_DELAY_CYCLES);

		ret = spi_send(spi, buf, chunk + 3);
		kfree(buf);
		if (ret)
			return ret;

		offset += chunk;
	}
	return 0;
}

/* ---- Framebuffer operations ---- */

static int qls_fb_sync(struct fb_info *info);

static int qls_fb_open(struct fb_info *info, int user)
{
	return 0;
}

static int qls_fb_release(struct fb_info *info, int user)
{
	return 0;
}

static int qls_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres != QLS_XRES || var->yres != QLS_YRES ||
	    var->xres_virtual != QLS_XRES || var->yres_virtual != QLS_YRES)
		return -EINVAL;
	if (var->bits_per_pixel != 24 && var->bits_per_pixel != 8)
		return -EINVAL;

	if (var->bits_per_pixel == 24) {
		var->red.offset   = 16;
		var->red.length   = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset  = 0;
		var->blue.length  = 8;
	} else {
		var->red.offset   = 0;
		var->red.length   = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset  = 0;
		var->blue.length  = 8;
	}
	var->transp.offset = 0;
	var->transp.length = 0;
	var->grayscale = 0;
	var->pixclock  = 0;
	var->left_margin  = 0;
	var->right_margin = 0;
	var->upper_margin  = 0;
	var->lower_margin  = 0;
	var->hsync_len = 0;
	var->vsync_len = 0;
	var->sync  = 0;
	var->vmode = FB_VMODE_NONINTERLACED;
	return 0;
}

static int qls_fb_set_par(struct fb_info *info)
{
	struct qls_priv *qls = info->par;

	qls->xres = info->var.xres;
	qls->yres = info->var.yres;
	info->fix.line_length = qls->xres * QLS_BPP;
	return 0;
}

static int qls_fb_blank(int blank, struct fb_info *info)
{
	struct qls_priv *qls = info->par;
	u8 gowin_bright[3] = { 0x05, 0x00, 0x00 };
	int br = (blank == FB_BLANK_UNBLANK) ? qls->brightness : 0;

	mutex_lock(&qls->sa->lock);
	/* Gowin FPGA: CMD_SET_BRIGHTNESS (0x05) */
	gowin_bright[1] = br & 0xff;
	gowin_bright[2] = (br >> 8) & 0xff;
	spi_send(qls->spi, gowin_bright, 3);

	/* SA protocol: SA_CMD_SET_BRIGHTNESS (0x1B) */
	sa_set_brightness(qls->sa, br);
	mutex_unlock(&qls->sa->lock);
	return 0;
}

static int qls_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	return qls_fb_sync(info);
}

static void qls_flush_work(struct work_struct *work)
{
	struct qls_priv *qls = container_of(work, struct qls_priv, flush_work);

	qls_fb_sync(qls->fb);
	qls->flush_pending = false;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static void qls_flush_timer(struct timer_list *t)
{
	struct qls_priv *qls = container_of(t, struct qls_priv, flush_timer);
#else
static void qls_flush_timer(unsigned long data)
{
	struct qls_priv *qls = (struct qls_priv *)data;
#endif

	if (!qls->flush_pending) {
		qls->flush_pending = true;
		queue_work(system_dfl_wq, &qls->flush_work);
	}
}

static int qls_fb_sync(struct fb_info *info)
{
	struct qls_priv *qls = info->par;
	u8 grayscale[QLS_XRES * QLS_YRES];
	u8 *gowin_buf;
	u8 sa_buf[4 + sizeof(grayscale) + 2];
	u8 show_buf[3] = { 0x03, 0x00, 0x00 };
	u16 crc;
	int i, total_pixels = qls->xres * qls->yres;

	if (qls->pixel_depth == 3) {
		for (i = 0; i < total_pixels; i++) {
			u8 r = qls->videomemory[i * 3 + 0];
			u8 g = qls->videomemory[i * 3 + 1];
			u8 b = qls->videomemory[i * 3 + 2];
			grayscale[i] = max_t(u8, r, max_t(u8, g, b));
		}
	} else {
		memcpy(grayscale, qls->videomemory, total_pixels);
	}

	mutex_lock(&qls->sa->lock);
	qls->sync_count++;

	/* 1. Station Max Gowin FPGA: CMD_WRITE (0x10) + CMD_SHOW_PIC (0x03) */
	gowin_buf = kmalloc(3 + total_pixels, GFP_KERNEL);
	if (gowin_buf) {
		gowin_buf[0] = 0x10;
		gowin_buf[1] = total_pixels & 0xff;
		gowin_buf[2] = (total_pixels >> 8) & 0xff;
		memcpy(gowin_buf + 3, grayscale, total_pixels);
		spi_send(qls->spi, gowin_buf, 3 + total_pixels);
		kfree(gowin_buf);
		spi_send(qls->spi, show_buf, 3);
	}

	/* 2. Station 2 SA protocol: SA_CMD_SHOW_PIC (0x07) + flush */
	sa_buf[0] = SA_CMD_SHOW_PIC;
	sa_buf[1] = 0x00;
	sa_buf[2] = 0x00;
	sa_buf[3] = 0x00;
	memcpy(sa_buf + 4, grayscale, total_pixels);
	crc = qls_crc16(sa_buf, 4 + total_pixels);
	sa_buf[4 + total_pixels] = (crc >> 8) & 0xff;
	sa_buf[4 + total_pixels + 1] = crc & 0xff;
	spi_send(qls->spi, sa_buf, 4 + total_pixels + 2);
	sa_flush_buffer(qls->sa);

	qls->frames_in_queue = 0;
	mutex_unlock(&qls->sa->lock);
	return 0;
}

static ssize_t qls_fb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	size_t total_size = info->screen_size;

	if (p >= total_size)
		return 0;
	if (count > total_size - p)
		count = total_size - p;

	if (copy_from_user(info->screen_buffer + p, buf, count))
		return -EFAULT;

	*ppos += count;
	qls_fb_sync(info);
	return count;
}

static void qls_fb_destroy(struct fb_info *info)
{
	struct qls_priv *qls = info->par;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	timer_delete_sync(&qls->flush_timer);
#else
	del_timer_sync(&qls->flush_timer);
#endif
	cancel_work_sync(&qls->flush_work);
	vfree(qls->videomemory);
	qls->videomemory = NULL;
}

static int qls_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return remap_vmalloc_range(vma, info->screen_buffer, vma->vm_pgoff);
}

static void qls_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	sys_fillrect(info, rect);
	qls_fb_sync(info);
}

static void qls_fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	sys_copyarea(info, area);
	qls_fb_sync(info);
}

static void qls_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	sys_imageblit(info, image);
	qls_fb_sync(info);
}

static struct fb_ops qls_fb_ops = {
	.owner        = THIS_MODULE,
	.fb_open      = qls_fb_open,
	.fb_release   = qls_fb_release,
	.fb_check_var = qls_fb_check_var,
	.fb_set_par   = qls_fb_set_par,
	.fb_blank     = qls_fb_blank,
	.fb_pan_display = qls_fb_pan_display,
	.fb_write     = qls_fb_write,
	.fb_fillrect  = qls_fb_fillrect,
	.fb_copyarea  = qls_fb_copyarea,
	.fb_imageblit = qls_fb_imageblit,
	.fb_sync      = qls_fb_sync,
	.fb_destroy   = qls_fb_destroy,
	.fb_mmap      = qls_fb_mmap,
};

/* ---- Backlight ---- */

static int qls_bl_update_status(struct backlight_device *bd)
{
	struct qls_priv *qls = bl_get_data(bd);
	u8 gowin_bright[3] = { 0x05, 0x00, 0x00 };

	qls->brightness = clamp(bd->props.brightness, 0, qls->max_brightness);
	if (debug)
		dev_info(&qls->spi->dev,
			 "[QLS] set backlight brightness=%d (max=%d)\n",
			 qls->brightness, qls->max_brightness);

	mutex_lock(&qls->sa->lock);
	/* Gowin protocol: CMD_SET_BRIGHTNESS = 0x05 */
	gowin_bright[1] = qls->brightness & 0xff;
	gowin_bright[2] = (qls->brightness >> 8) & 0xff;
	spi_send(qls->spi, gowin_bright, 3);

	/* SA protocol: SA_CMD_SET_BRIGHTNESS = 0x1B */
	sa_set_brightness(qls->sa, qls->brightness);
	mutex_unlock(&qls->sa->lock);
	return 0;
}

static int qls_bl_get_brightness(struct backlight_device *bd)
{
	struct qls_priv *qls = bl_get_data(bd);
	return qls->brightness;
}

static const struct backlight_ops qls_bl_ops = {
	.update_status  = qls_bl_update_status,
	.get_brightness = qls_bl_get_brightness,
};

/* ---- Hwmon ---- */

static int qls_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	*val = 25000;
	return 0;
}

static umode_t qls_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel)
{
	return 0444;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static const struct hwmon_channel_info *qls_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL,
};

static const struct hwmon_ops qls_hwmon_ops = {
	.is_visible = qls_hwmon_is_visible,
	.read = qls_hwmon_read,
};

static const struct hwmon_chip_info qls_hwmon_chip_info = {
	.ops = &qls_hwmon_ops,
	.info = qls_hwmon_info,
};
#endif

/* ---- Firmware update ---- */

static int qls_load_firmware_update(struct qls_priv *qls)
{
	const struct firmware *manifest_fw, *fw;
	char manifest_name[] = "yandex-led-screen.bin.manifest";
	char fw_name[] = "yandex-led-screen.bin";
	int ret, parsed;
	u8 min_api, max_api, version;
	u16 crc16;

	ret = request_firmware(&manifest_fw, manifest_name, &qls->spi->dev);
	if (ret) {
		dev_warn(&qls->spi->dev,
			 "[QLS] no firmware manifest '%s' (%d), skipping update\n",
			 manifest_name, ret);
		return 0;
	}

	parsed = sscanf(manifest_fw->data,
			"%hu.%hhu.%hhu.%hhu.%hu.%hhu",
			&qls->fw_version,
			&min_api, &max_api,
			&version, &crc16, &qls->fw_crc);
	release_firmware(manifest_fw);

	if (parsed < 3) {
		dev_err(&qls->spi->dev,
			"[QLS] failed to parse firmware manifest\n");
		return -EINVAL;
	}

	qls->min_api_version = min_api;
	qls->max_api_version = max_api;
	qls->expected_crc = crc16;

	if (qls->sa->api_version < min_api) {
		dev_err(&qls->spi->dev,
			"[QLS] SA API %d below min required %hhu\n",
			qls->sa->api_version, min_api);
		return -EINVAL;
	}

	if (qls->sa->api_version > max_api) {
		dev_err(&qls->spi->dev,
			"[QLS] SA API %d above max supported %hhu\n",
			qls->sa->api_version, max_api);
		return -EINVAL;
	}

	ret = request_firmware(&fw, fw_name, &qls->spi->dev);
	if (ret) {
		dev_err(&qls->spi->dev,
			"[QLS] failed to load firmware update '%s': %d\n",
			fw_name, ret);
		return ret;
	}

	if (crc16 != qls_crc16(fw->data, fw->size)) {
		dev_err(&qls->spi->dev,
			"[QLS] firmware CRC mismatch\n");
		release_firmware(fw);
		return -EILSEQ;
	}

	dev_info(&qls->spi->dev,
		 "[QLS] firmware update: version %hhu, size %zu\n",
		 version, fw->size);

	ret = bl_restart(qls->spi);
	if (ret) {
		dev_err(&qls->spi->dev,
			"[QLS] failed to reboot into bootloader: %d\n", ret);
		release_firmware(fw);
		return ret;
	}

	msleep(100);

	ret = bl_write_fw(qls->spi, fw->data, fw->size);
	if (ret) {
		dev_err(&qls->spi->dev,
			"[QLS] failed to write firmware: %d\n", ret);
		release_firmware(fw);
		return ret;
	}

	ret = bl_run_app(qls->spi);
	if (ret) {
		dev_err(&qls->spi->dev,
			"[QLS] failed to run new firmware: %d\n", ret);
		release_firmware(fw);
		return ret;
	}

	msleep(50);

	ret = sa_none_blocking(qls, qls->sa);
	if (ret)
		dev_warn(&qls->spi->dev,
			 "[QLS] SA not responsive after fw update: %d\n", ret);

	sa_set_crc_check(qls->sa, 1);
	sa_start_boot_animation(qls->sa);

	release_firmware(fw);
	dev_info(&qls->spi->dev, "[QLS] firmware update completed\n");
	return 0;
}

/* ---- Sysfs attributes ---- */

static ssize_t next_frame_delay_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", qls->next_frame_delay);
}

static ssize_t next_frame_delay_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct qls_priv *qls = dev_get_drvdata(dev);
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	qls->next_frame_delay = val;
	return count;
}

static ssize_t screen_fw_after_start_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", qls->screen_fw_after_start);
}

static ssize_t screen_reset_count_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", qls->reset_count);
}

static ssize_t update_result_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", qls->update_result);
}

static ssize_t update_info_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "fw:%u;api:%u;min_api:%hhu;max_api:%hhu;crc:%u\n",
		       qls->fw_version, qls->sa->api_version,
		       qls->min_api_version, qls->max_api_version,
		       qls->fw_crc);
}

static ssize_t frames_in_frame_queue_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", qls->frames_in_queue);
}

static ssize_t frame_queue_overflows_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", qls->frame_queue_overflows);
}

static ssize_t screen_fw_current_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", qls->fw_version);
}

static ssize_t disable_test_mode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct qls_priv *qls = dev_get_drvdata(dev);
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	qls->disable_test_mode = val;
	return count;
}

static ssize_t flush_frame_queue_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	mutex_lock(&qls->sa->lock);
	sa_flush_buffer(qls->sa);
	qls->frames_in_queue = 0;
	mutex_unlock(&qls->sa->lock);
	return count;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct qls_priv *qls = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE,
			 "res=%ux%u bpp=%u brightness=%d/%d fw=%u api=%d syncs=%lu spi_tx=%lu spi_rx=%lu spi_err=%lu crc_err=%lu\n",
			 qls->xres, qls->yres, qls->pixel_depth * 8,
			 qls->brightness, qls->max_brightness,
			 qls->fw_version, qls->sa->api_version,
			 qls->sync_count, qls->spi_tx_count,
			 qls->spi_rx_count, qls->spi_errors,
			 qls->crc_errors);
}

static DEVICE_ATTR_RW(next_frame_delay);
static DEVICE_ATTR_RO(screen_fw_after_start);
static DEVICE_ATTR_RO(screen_reset_count);
static DEVICE_ATTR_RO(update_result);
static DEVICE_ATTR_RO(update_info);
static DEVICE_ATTR_RO(frames_in_frame_queue);
static DEVICE_ATTR_RO(frame_queue_overflows);
static DEVICE_ATTR_RO(screen_fw_current);
static DEVICE_ATTR_WO(disable_test_mode);
static DEVICE_ATTR_WO(flush_frame_queue);
static DEVICE_ATTR_RO(status);

static struct attribute *qls_attrs[] = {
	&dev_attr_next_frame_delay.attr,
	&dev_attr_screen_fw_after_start.attr,
	&dev_attr_screen_reset_count.attr,
	&dev_attr_update_result.attr,
	&dev_attr_update_info.attr,
	&dev_attr_frames_in_frame_queue.attr,
	&dev_attr_frame_queue_overflows.attr,
	&dev_attr_screen_fw_current.attr,
	&dev_attr_disable_test_mode.attr,
	&dev_attr_flush_frame_queue.attr,
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group qls_attr_group = {
	.attrs = qls_attrs,
};

/* ---- Probe / Remove ---- */

static int qls_probe(struct spi_device *spi)
{
	struct qls_priv *qls;
	struct sa_device *sa;
	struct fb_info *info;
	struct backlight_device *bl;
	struct backlight_properties bl_props;
	struct device *hwmon_dev;
	struct sa_response resp;
	int ret;

	dev_info(&spi->dev, "[QLS] Probing Quasar LED Screen on SPI CS=%u...\n",
		 spi_get_chipselect(spi, 0));

	qls = devm_kzalloc(&spi->dev, sizeof(*qls), GFP_KERNEL);
	if (!qls)
		return -ENOMEM;

	sa = devm_kzalloc(&spi->dev, sizeof(*sa), GFP_KERNEL);
	if (!sa)
		return -ENOMEM;

	sa->spi = spi;
	mutex_init(&sa->lock);
	qls->spi = spi;
	qls->sa = sa;
	qls->xres = QLS_XRES;
	qls->yres = QLS_YRES;
	qls->pixel_depth = QLS_BPP;
	qls->max_brightness = 200;
	qls->brightness = 200;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	timer_setup(&qls->flush_timer, qls_flush_timer, 0);
#else
	setup_timer(&qls->flush_timer, qls_flush_timer, (unsigned long)qls);
#endif
	INIT_WORK(&qls->flush_work, qls_flush_work);

	spi_set_drvdata(spi, qls);
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = SA_SPI_SPEED;
	spi_setup(spi);

	qls->reset = devm_gpiod_get(&spi->dev, "reset", GPIOD_ASIS);
	if (IS_ERR(qls->reset)) {
		dev_info(&spi->dev, "[QLS] reset GPIO not defined, skipping hardware reset\n");
		qls->reset = NULL;
	} else {
		dev_info(&spi->dev, "[QLS] pulsing reset GPIO...\n");
		gpiod_set_value_cansleep(qls->reset, 1);
		usleep_range(10000, 15000);
		gpiod_set_value_cansleep(qls->reset, 0);
		usleep_range(100000, 120000);
		qls->reset_count++;
	}

	of_property_read_u32(spi->dev.of_node, "max-brightness",
			     &qls->max_brightness);
	if (qls->max_brightness > 255)
		qls->max_brightness = 255;

	ret = sa_none(sa, sa->respbuf, SA_RESP_BUF_SIZE);
	if (ret) {
		dev_warn(&spi->dev, "[QLS] SA not responding (%d), trying fw update\n",
			 ret);
		qls_load_firmware_update(qls);
		ret = sa_none_blocking(qls, sa);
		if (ret) {
			dev_err(&spi->dev,
				"[QLS] SA still not responding after update: %d\n",
				ret);
			return ret;
		}
	}

	sa_set_crc_check(sa, 1);
	sa_start_boot_animation(sa);

	ret = sa_parse_response(qls, sa->respbuf, SA_RESP_BUF_SIZE, &resp);
	if (ret == 0) {
		sa->api_version = resp.fw_version;
		dev_info(&spi->dev, "[QLS] SA response OK: magic=0x%02x fw_version=%u\n",
			 resp.magic, resp.fw_version);
	} else {
		dev_info(&spi->dev,
			 "[QLS] Non-SA response (%d, raw=%8ph), operating in direct Gowin/SA hybrid mode\n",
			 ret, sa->respbuf);
	}

	qls->videomemory = vzalloc(qls->xres * qls->yres * QLS_BPP);
	if (!qls->videomemory)
		return -ENOMEM;

	info = framebuffer_alloc(0, &spi->dev);
	if (!info) {
		vfree(qls->videomemory);
		return -ENOMEM;
	}

	qls->fb = info;
	info->par = qls;
	info->screen_buffer = (char __iomem *)qls->videomemory;
	info->screen_size = qls->xres * qls->yres * QLS_BPP;
	info->fbops = &qls_fb_ops;
	info->flags = 0;

	info->var.xres = qls->xres;
	info->var.yres = qls->yres;
	info->var.xres_virtual = qls->xres;
	info->var.yres_virtual = qls->yres;
	info->var.bits_per_pixel = 24;
	info->var.red.offset = 16;
	info->var.red.length = 8;
	info->var.green.offset = 8;
	info->var.green.length = 8;
	info->var.blue.offset = 0;
	info->var.blue.length = 8;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	info->fix.smem_start = (unsigned long)qls->videomemory;
	info->fix.smem_len = qls->xres * qls->yres * QLS_BPP;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = qls->xres * QLS_BPP;
	info->fix.accel = FB_ACCEL_NONE;
	snprintf(info->fix.id, sizeof(info->fix.id), "quasar-led-screen");

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&spi->dev, "[QLS] failed to register framebuffer: %d\n", ret);
		framebuffer_release(info);
		vfree(qls->videomemory);
		return ret;
	}

	memset(&bl_props, 0, sizeof(bl_props));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.max_brightness = qls->max_brightness;
	bl_props.brightness = qls->brightness;

	bl = devm_backlight_device_register(&spi->dev, "quasar_led_screen",
					    &spi->dev, qls,
					    &qls_bl_ops, &bl_props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);
	qls->bl = bl;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	hwmon_dev = devm_hwmon_device_register_with_info(&spi->dev,
							 "quasar_led_screen",
							 qls,
							 &qls_hwmon_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		dev_warn(&spi->dev, "[QLS] hwmon registration failed\n");
#endif

	ret = sysfs_create_group(&spi->dev.kobj, &qls_attr_group);
	if (ret)
		dev_warn(&spi->dev, "[QLS] failed to create sysfs group: %d\n", ret);

	dev_info(&spi->dev, "[QLS] registered framebuffer '%s', %ux%u %ubpp (max_brightness=%d)\n",
		 info->fix.id, qls->xres, qls->yres, info->var.bits_per_pixel,
		 qls->max_brightness);

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
static void qls_remove(struct spi_device *spi)
#else
static int qls_remove(struct spi_device *spi)
#endif
{
	struct qls_priv *qls = spi_get_drvdata(spi);

	sysfs_remove_group(&spi->dev.kobj, &qls_attr_group);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	timer_delete_sync(&qls->flush_timer);
#else
	del_timer_sync(&qls->flush_timer);
#endif
	cancel_work_sync(&qls->flush_work);

	if (qls->fb) {
		unregister_framebuffer(qls->fb);
		framebuffer_release(qls->fb);
		qls->fb = NULL;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	return 0;
#endif
}

static const struct of_device_id qls_of_match[] = {
	{ .compatible = "quasar_led_screen" },
	{},
};
MODULE_DEVICE_TABLE(of, qls_of_match);

static const struct spi_device_id qls_spi_ids[] = {
	{ "quasar_led_screen", 0 },
	{},
};
MODULE_DEVICE_TABLE(spi, qls_spi_ids);

static struct spi_driver qls_spi_driver = {
	.driver = {
		.name   = DRIVER_NAME,
		.of_match_table = qls_of_match,
	},
	.id_table = qls_spi_ids,
	.probe    = qls_probe,
	.remove   = qls_remove,
};

module_spi_driver(qls_spi_driver);

MODULE_DESCRIPTION("Quasar LED Screen FB driver (Gowin FPGA, SA protocol)");
MODULE_AUTHOR("leftymods");
MODULE_LICENSE("GPL v2");
