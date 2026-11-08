// SPDX-License-Identifier: GPL-2.0
/*
 * gowin_led_device.c - Quasar LED panel/screen SPI driver
 *
 * Copyright (C) 2025 AtriOS Team
 *
 * SPI-driven LED panel/screen with framebuffer, backlight,
 * virtual GPIO, virtual UART, touch input, JTAG FPGA programming,
 * and firmware update support.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/fb.h>
#include <linux/uaccess.h>
#include <linux/backlight.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/crc-itu-t.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>

#ifndef sysfs_emit
#define sysfs_emit(buf, fmt, ...) scnprintf(buf, PAGE_SIZE, fmt, ##__VA_ARGS__)
#endif

#ifndef FBINFO_VIRTFB
#define FBINFO_VIRTFB 0
#endif

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose Gowin FPGA SPI protocol and framebuffer debug logs");

#define GOWIN_DRV_NAME		"led_panel"
#define GOWIN_FB_NAME_SCREEN	"YNDX LED screen"
#define GOWIN_FB_NAME_PANEL	"led_panel_fb"

#define GOWIN_GPIO_NGPIO	2
#define GOWIN_TTY_MINOR_COUNT	1
#define GOWIN_TTY_BUF_SIZE	64
#define GOWIN_FB_WIDTH_MAX	320
#define GOWIN_FB_HEIGHT_MAX	320

/*
 * Exact PA (application_mode.c) SPI opcodes and status framing:
 * Every command ends with a 2-byte big-endian CRC-16/XMODEM (crc_itu_t),
 * followed by a 35-byte pure SPI read (get_lp_status) verified as a residual
 * crc_itu_t(0, status_buf, 35) == 0.
 */
#define CMD_SHOW_PIC		0x07
#define CMD_SET_BRIGHTNESS	0x1b
#define CMD_SET_ANIMATION	0x24
#define CMD_SET_GPIO		0x56
#define CMD_UART_SEND_DATA	0x5d
#define CMD_RESET_TOUCH_STATE	0xc1
#define CMD_SET_POWER_SAVING	0xec
#define CMD_UART_FLUSH_DATA	0xfd

#define CMD_GET_STATUS_LEN	35
#define PA_APP_MODE		1

/* PA last_cmd_status (status_buf[16]) bit mask */
#define PA_STATUS_CMD_ERROR	BIT(0)
#define PA_STATUS_CRC_ERROR	BIT(1)
#define PA_STATUS_CMD_OVF	BIT(2)
#define PA_STATUS_FRM_OVF	BIT(3)
#define PA_STATUS_SPKT_ERROR	BIT(4)
#define PA_STATUS_LPKT_ERROR	BIT(5)
#define PA_STATUS_UART_OVF	BIT(6)

enum gowin_dev_type {
	LED_PANEL = 0,
	LED_SCREEN = 1,
};

struct gowin_config {
	enum gowin_dev_type type;
	int width;
	int height;
};

static const struct gowin_config cfg_panel = {
	.type = LED_PANEL,
	.width = 25,
	.height = 16,
};

static const struct gowin_config cfg_screen = {
	.type = LED_SCREEN,
	.width = 25,
	.height = 16,
};

struct gowin_touch_regs {
	u32 dbg_val[5];
	u32 metric[5];
	u32 touch_state;
	u32 touch_mask;
};

struct gowin_priv {
	struct spi_device *spi;
	struct device *dev;
	enum gowin_dev_type dev_type;
	struct fb_info *fbi;
	struct backlight_device *bl;
	struct gpio_chip gpio_chip;
	struct tty_driver *tty_drv;
	struct tty_port tty_port;
	struct input_dev *touch_dev;
	struct workqueue_struct *touch_wq;
	struct work_struct touch_work;
	struct work_struct uart_rx_work;
	struct timer_list uart_timer;
	struct delayed_work fb_push_work;

	bool has_gpio;
	bool has_uart;
	bool has_touch;
	bool removed;

	/* GPIO pins */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *irq_gpio;
	struct gpio_desc *jtag_tck;
	struct gpio_desc *jtag_tdo;
	struct gpio_desc *jtag_tdi;
	struct gpio_desc *jtag_tms;
	struct gpio_desc *jtag_sel;
	struct gpio_desc *jtag_reconfig;

	/* pinctrl */
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_default;
	struct pinctrl_state *pinctrl_jtag;

	/* Mode */
	bool jtag_mode;
	bool tms_from_spi_cs;

	/* Resolution & Firmware info */
	int width;
	int height;
	u16 board_rev;
	u8 fw_version;
	u16 uf_crc;
	u8 device_mode;
	u32 last_usercode;
	u32 last_status_code;

	/* GPIO virtual chip cached byte */
	u8 gpio_cached;

	/* Framebuffer */
	void *fb_mem;
	u8 *lum_buf;
	u8 *tx_buf;
	int fb_open_count;
	u32 pseudo_palette[16];

	/* Backlight */
	unsigned int brightness;
	unsigned int max_brightness;

	/* Frame queue state */
	unsigned int next_frame_delay;
	unsigned int frames_in_queue;
	unsigned int frame_queue_overflows;

	/* CRC & protocol statistics */
	unsigned int crc_errors_master;
	unsigned int crc_errors_slave;
	unsigned long frames_pushed;
	unsigned long spi_tx_errors;
	u8 last_status_buf[CMD_GET_STATUS_LEN];
	u32 last_idcode;

	/* Power saving */
	bool power_saving;

	/* Touch state */
	int touch_irq_number;

	/* Firmware status */
	char fw_upd_status[64];

	/* Lock */
	struct mutex lock;

	/* TTY xmit buffer */
	unsigned char xmit_buf[GOWIN_TTY_BUF_SIZE];
	int xmit_count;
};

/* ------------------------------------------------------------------ */
/* App protocol -- SPI command layer                                   */
/* ------------------------------------------------------------------ */

static void lp_append_crc_be(u8 *buf, int total_len)
{
	u16 crc = crc_itu_t(0, buf, total_len - 2);

	buf[total_len - 2] = (crc >> 8) & 0xff;
	buf[total_len - 1] = crc & 0xff;
}

static int get_lp_status_locked(struct gowin_priv *priv, u8 *status_buf)
{
	u8 local_buf[CMD_GET_STATUS_LEN];
	u8 *rx = status_buf ? status_buf : local_buf;
	struct spi_transfer t = {
		.tx_buf = NULL,
		.rx_buf = rx,
		.len = CMD_GET_STATUS_LEN,
	};
	struct spi_message m;
	int tries, ret = -EIO;

	for (tries = 0; tries < 5; tries++) {
		if (tries > 0)
			usleep_range(10, 20);

		memset(rx, 0, CMD_GET_STATUS_LEN);
		spi_message_init(&m);
		spi_message_add_tail(&t, &m);
		ret = spi_sync(priv->spi, &m);
		if (ret) {
			priv->spi_tx_errors++;
			if (debug)
				dev_err(priv->dev,
					"[GOWIN] get_lp_status spi_sync failed: %d\n", ret);
			return ret;
		}

		memcpy(priv->last_status_buf, rx, CMD_GET_STATUS_LEN);

		if (crc_itu_t(0, rx, CMD_GET_STATUS_LEN) != 0) {
			priv->crc_errors_master++;
			if (debug)
				dev_warn(priv->dev,
					 "[GOWIN] status CRC error #%u (try %d): rx=%16ph ...\n",
					 priv->crc_errors_master, tries + 1, rx);
			ret = -EBADMSG;
			continue;
		}

		if (rx[1] != PA_APP_MODE) {
			if (debug)
				dev_warn(priv->dev,
					 "[GOWIN] unexpected mode byte rx[1]=0x%02x (try %d), rx=%16ph\n",
					 rx[1], tries + 1, rx);
			ret = -EPROTO;
			continue;
		}

		priv->fw_version = rx[2];
		priv->board_rev = rx[3] | ((u16)rx[4] << 8);
		priv->uf_crc = rx[5] | ((u16)rx[6] << 8);
		priv->frame_queue_overflows = rx[7] | ((u32)rx[8] << 8) |
					      ((u32)rx[9] << 16) | ((u32)rx[10] << 24);
		priv->crc_errors_slave = rx[11] | ((u32)rx[12] << 8) |
					 ((u32)rx[13] << 16) | ((u32)rx[14] << 24);
		priv->frames_in_queue = rx[15];
		priv->device_mode = rx[17];

		if (rx[16] & ~PA_STATUS_FRM_OVF) {
			if (debug)
				dev_warn(priv->dev,
					 "[GOWIN] slave cmd error flags rx[16]=0x%02x\n",
					 rx[16]);
			return -EIO;
		}

		return 0;
	}

	return ret;
}

static int get_lp_status(struct gowin_priv *priv, u8 *status_buf)
{
	int ret;

	mutex_lock(&priv->lock);
	if (priv->jtag_mode) {
		mutex_unlock(&priv->lock);
		return -EBUSY;
	}
	ret = get_lp_status_locked(priv, status_buf);
	mutex_unlock(&priv->lock);
	return ret;
}

static int lp_write_locked(struct gowin_priv *priv, u8 *buf, int total_len)
{
	struct spi_transfer t = {
		.tx_buf = buf,
		.len = total_len,
	};
	struct spi_message m;
	int tries, ret;

	if (priv->jtag_mode)
		return -EBUSY;

	lp_append_crc_be(buf, total_len);

	for (tries = 0; tries < 3; tries++) {
		spi_message_init(&m);
		spi_message_add_tail(&t, &m);
		ret = spi_sync(priv->spi, &m);
		if (ret) {
			priv->spi_tx_errors++;
			return ret;
		}

		ret = get_lp_status_locked(priv, NULL);
		if (ret == 0)
			return 0;
	}

	if (debug)
		dev_warn(priv->dev,
			 "[GOWIN] lp_write_locked cmd=0x%02x len=%d failed after 3 tries: %d\n",
			 buf[0], total_len, ret);
	return ret;
}

static int lp_write(struct gowin_priv *priv, u8 *buf, int total_len)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = lp_write_locked(priv, buf, total_len);
	mutex_unlock(&priv->lock);
	return ret;
}

static int show_pic_app_cmd_locked(struct gowin_priv *priv, const u8 *lum)
{
	int npixels = priv->width * priv->height;
	int total_len = 4 + npixels + 2;
	u8 *buf = priv->tx_buf;
	int ret;

	if (!buf)
		return -ENOMEM;

	buf[0] = CMD_SHOW_PIC;
	buf[1] = 0x00;
	buf[2] = priv->next_frame_delay & 0xff;
	buf[3] = (priv->next_frame_delay >> 8) & 0xff;
	memcpy(buf + 4, lum, npixels);

	ret = lp_write_locked(priv, buf, total_len);
	if (ret == 0) {
		priv->frames_pushed++;
		if (debug && (priv->frames_pushed % 30 == 1))
			dev_info(priv->dev,
				 "[GOWIN] frame #%lu pushed OK (fiq=%u, fqo=%u, crc_m=%u, crc_s=%u)\n",
				 priv->frames_pushed, priv->frames_in_queue,
				 priv->frame_queue_overflows,
				 priv->crc_errors_master, priv->crc_errors_slave);
	}
	return ret;
}

static int set_brightness_app_cmd(struct gowin_priv *priv, unsigned int val)
{
	u8 buf[4] = { CMD_SET_BRIGHTNESS, val & 0xff, 0, 0 };

	if (debug)
		dev_info(priv->dev, "[GOWIN] set_brightness=%u\n", val);
	return lp_write(priv, buf, sizeof(buf));
}

static int set_gpio_app_cmd(struct gowin_priv *priv)
{
	u8 buf[4] = { CMD_SET_GPIO, priv->gpio_cached, 0, 0 };

	return lp_write(priv, buf, sizeof(buf));
}

static int set_animation_app_cmd(struct gowin_priv *priv, int mode)
{
	u8 buf[19];

	memset(buf, 0, sizeof(buf));
	buf[0] = CMD_SET_ANIMATION;
	buf[1] = mode & 0xff;

	return lp_write(priv, buf, sizeof(buf));
}

static int set_power_saving_mode_cmd(struct gowin_priv *priv, int on)
{
	u8 buf[4] = { CMD_SET_POWER_SAVING, !!on, 0, 0 };

	return lp_write(priv, buf, sizeof(buf));
}

static int reset_touch_state_app_cmd(struct gowin_priv *priv)
{
	u8 buf[4] = { CMD_RESET_TOUCH_STATE, 0x00, 0, 0 };

	return lp_write(priv, buf, sizeof(buf));
}

static int uart_send_data_app_cmd(struct gowin_priv *priv,
				  const u8 *data, int len)
{
	u8 buf[1 + GOWIN_TTY_BUF_SIZE + 2];

	if (len <= 0)
		return 0;
	if (len > GOWIN_TTY_BUF_SIZE)
		len = GOWIN_TTY_BUF_SIZE;

	buf[0] = CMD_UART_SEND_DATA;
	memcpy(buf + 1, data, len);

	return lp_write(priv, buf, 1 + len + 2);
}

static int uart_receive_data(struct gowin_priv *priv,
			     u8 *rx_data, int *rx_len)
{
	u8 status[CMD_GET_STATUS_LEN];
	int ret;

	*rx_len = 0;
	ret = get_lp_status(priv, status);
	if (ret)
		return ret;

	/* Status byte 17 holds UART RX count if supported */
	if (status[17] > 0) {
		int cnt = min_t(int, status[17], 15);
		memcpy(rx_data, &status[18], cnt);
		*rx_len = cnt;
	}
	return 0;
}

__maybe_unused static int uart_flush_data_app_cmd(struct gowin_priv *priv)
{
	u8 buf[6] = { CMD_UART_FLUSH_DATA, 0, 0, 0, 0, 0 };

	return lp_write(priv, buf, sizeof(buf));
}

/* ------------------------------------------------------------------ */
/* GPIO chip                                                           */
/* ------------------------------------------------------------------ */

static int lp_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	return 0;
}

static int lp_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return -EINVAL;
}

static int lp_gpio_get_value(struct gpio_chip *chip, unsigned int offset)
{
	struct gowin_priv *priv = gpiochip_get_data(chip);
	return (priv->gpio_cached >> offset) & 1;
}

static int lp_gpio_set_value(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	struct gowin_priv *priv = gpiochip_get_data(chip);

	if (value)
		priv->gpio_cached |= BIT(offset);
	else
		priv->gpio_cached &= ~BIT(offset);

	set_gpio_app_cmd(priv);
	return 0;
}

static int lp_gpio_direction_output(struct gpio_chip *chip,
				    unsigned int offset, int value)
{
	lp_gpio_set_value(chip, offset, value);
	return 0;
}

static int gpio_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	int ret;

	priv->gpio_chip.label = "led_panel";
	priv->gpio_chip.base = -1;
	priv->gpio_chip.ngpio = GOWIN_GPIO_NGPIO;
	priv->gpio_chip.get_direction = lp_gpio_get_direction;
	priv->gpio_chip.direction_input = lp_gpio_direction_input;
	priv->gpio_chip.direction_output = lp_gpio_direction_output;
	priv->gpio_chip.get = lp_gpio_get_value;
	priv->gpio_chip.set = lp_gpio_set_value;
	priv->gpio_chip.parent = dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	priv->gpio_chip.fwnode = dev_fwnode(dev);
#else
	priv->gpio_chip.of_node = dev->of_node;
#endif
	priv->gpio_chip.of_gpio_n_cells = 2;
	priv->gpio_chip.can_sleep = true;

	ret = gpiochip_add_data(&priv->gpio_chip, priv);
	if (ret)
		dev_err(dev, "[GOWIN] gpiochip_add_data failed: %d\n", ret);

	return ret;
}

static void gpio_exit(struct gowin_priv *priv)
{
	if (priv->has_gpio)
		gpiochip_remove(&priv->gpio_chip);
}

/* ------------------------------------------------------------------ */
/* TTY / UART                                                         */
/* ------------------------------------------------------------------ */

static int lp_uart_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(tty->port, tty, filp);
}

static void lp_uart_close(struct tty_struct *tty, struct file *filp)
{
	tty_port_close(tty->port, tty, filp);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
static unsigned int lp_uart_write_room(struct tty_struct *tty)
#else
static int lp_uart_write_room(struct tty_struct *tty)
#endif
{
	return GOWIN_TTY_BUF_SIZE;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
static ssize_t lp_uart_write(struct tty_struct *tty, const u8 *buf,
			     size_t count)
#else
static int lp_uart_write(struct tty_struct *tty, const unsigned char *buf,
			 int count)
#endif
{
	struct gowin_priv *priv = container_of(tty->port, struct gowin_priv,
					       tty_port);

	if (count > GOWIN_TTY_BUF_SIZE)
		count = GOWIN_TTY_BUF_SIZE;

	if (uart_send_data_app_cmd(priv, buf, count))
		return 0;

	return count;
}

static void lp_uart_rx_work(struct work_struct *work)
{
	struct gowin_priv *priv = container_of(work, struct gowin_priv, uart_rx_work);
	u8 rx_buf[GOWIN_TTY_BUF_SIZE];
	int rx_len;
	int ret;

	ret = uart_receive_data(priv, rx_buf, &rx_len);
	if (ret || rx_len <= 0)
		return;

	tty_insert_flip_string_fixed_flag(&priv->tty_port, rx_buf,
					  TTY_NORMAL, rx_len);
	tty_flip_buffer_push(&priv->tty_port);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
static void lp_uart_poll(struct timer_list *t)
{
	struct gowin_priv *priv = container_of(t, struct gowin_priv,
					       uart_timer);
#else
static void lp_uart_poll(unsigned long data)
{
	struct gowin_priv *priv = (struct gowin_priv *)data;
#endif

	if (priv->tty_port.tty)
		queue_work(system_dfl_wq, &priv->uart_rx_work);

	mod_timer(&priv->uart_timer, jiffies + HZ / 10);
}

static const struct tty_operations lp_uart_ops = {
	.open = lp_uart_open,
	.close = lp_uart_close,
	.write = lp_uart_write,
	.write_room = lp_uart_write_room,
};

static int uart_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	int ret;

	priv->tty_drv = tty_alloc_driver(GOWIN_TTY_MINOR_COUNT,
					 TTY_DRIVER_REAL_RAW |
					 TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(priv->tty_drv))
		return PTR_ERR(priv->tty_drv);

	priv->tty_drv->driver_name = GOWIN_DRV_NAME;
	priv->tty_drv->name = "ttyLP";
	priv->tty_drv->major = 0;
	priv->tty_drv->minor_start = 0;
	priv->tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	priv->tty_drv->subtype = SERIAL_TYPE_NORMAL;
	priv->tty_drv->init_termios = tty_std_termios;
	priv->tty_drv->init_termios.c_cflag = B115200 | CS8 | CREAD | CLOCAL;
	tty_set_operations(priv->tty_drv, &lp_uart_ops);

	INIT_WORK(&priv->uart_rx_work, lp_uart_rx_work);

	tty_port_init(&priv->tty_port);
	tty_port_link_device(&priv->tty_port, priv->tty_drv, 0);

	ret = tty_register_driver(priv->tty_drv);
	if (ret) {
		dev_err(dev, "[GOWIN] tty_register_driver failed: %d\n", ret);
		tty_driver_kref_put(priv->tty_drv);
		tty_port_destroy(&priv->tty_port);
		return ret;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	timer_setup(&priv->uart_timer, lp_uart_poll, 0);
#else
	setup_timer(&priv->uart_timer, lp_uart_poll, (unsigned long)priv);
#endif
	mod_timer(&priv->uart_timer, jiffies + HZ / 10);

	dev_info(dev, "[GOWIN] uart ttyLP0 initialized\n");
	return 0;
}

static void uart_exit(struct gowin_priv *priv)
{
	if (!priv->has_uart)
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	timer_delete_sync(&priv->uart_timer);
#else
	del_timer_sync(&priv->uart_timer);
#endif
	flush_work(&priv->uart_rx_work);
	tty_unregister_driver(priv->tty_drv);
	tty_port_destroy(&priv->tty_port);
	tty_driver_kref_put(priv->tty_drv);
}

/* ------------------------------------------------------------------ */
/* Touch                                                              */
/* ------------------------------------------------------------------ */

static void touch_work(struct work_struct *work)
{
	struct gowin_priv *priv = container_of(work, struct gowin_priv, touch_work);
	struct gowin_touch_regs regs;
	u8 status[CMD_GET_STATUS_LEN];
	int ret;

	ret = get_lp_status(priv, status);
	if (ret)
		return;

	regs.touch_state = status[18];
	regs.touch_mask = status[28];

	if (regs.touch_mask) {
		input_event(priv->touch_dev, EV_KEY, KEY_BACK, 1);
		input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);
		input_event(priv->touch_dev, EV_KEY, KEY_BACK, 0);
		input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);
		reset_touch_state_app_cmd(priv);
	}
}

static irqreturn_t lp_touch_isr(int irq, void *data)
{
	struct gowin_priv *priv = data;

	queue_work(priv->touch_wq, &priv->touch_work);
	return IRQ_HANDLED;
}

static int init_touch(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	int ret;

	priv->touch_dev = devm_input_allocate_device(dev);
	if (!priv->touch_dev)
		return -ENOMEM;

	priv->touch_dev->name = "midi_touch";
	priv->touch_dev->phys = "led_panel/input0";
	priv->touch_dev->id.bustype = BUS_SPI;
	input_set_capability(priv->touch_dev, EV_KEY, KEY_BACK);

	priv->touch_wq = alloc_workqueue("touch_wq", WQ_UNBOUND, 1);
	if (!priv->touch_wq)
		return -ENOMEM;

	INIT_WORK(&priv->touch_work, touch_work);

	ret = input_register_device(priv->touch_dev);
	if (ret) {
		destroy_workqueue(priv->touch_wq);
		return ret;
	}

	if (priv->irq_gpio) {
		int irq = gpiod_to_irq(priv->irq_gpio);
		if (irq > 0) {
			ret = devm_request_threaded_irq(dev, irq, NULL,
							lp_touch_isr,
							IRQF_TRIGGER_FALLING |
							IRQF_ONESHOT,
							GOWIN_DRV_NAME, priv);
			if (ret) {
				destroy_workqueue(priv->touch_wq);
				return ret;
			}
			priv->touch_irq_number = irq;
		}
	}

	return 0;
}

static void deinit_touch(struct gowin_priv *priv)
{
	if (!priv->has_touch)
		return;
	if (priv->touch_irq_number)
		devm_free_irq(priv->dev, priv->touch_irq_number, priv);
	if (priv->touch_dev)
		input_unregister_device(priv->touch_dev);
	if (priv->touch_wq) {
		flush_work(&priv->touch_work);
		destroy_workqueue(priv->touch_wq);
	}
}

/* ------------------------------------------------------------------ */
/* Framebuffer                                                        */
/* ------------------------------------------------------------------ */

static int lp_fb_sync(struct fb_info *fbi)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;
	const u8 *src = (const u8 *)fbi->screen_buffer;
	int npixels = priv->width * priv->height;
	int i, ret;

	if (!src || !priv->lum_buf)
		return -EINVAL;

	mutex_lock(&priv->lock);
	if (priv->removed || priv->jtag_mode) {
		mutex_unlock(&priv->lock);
		return 0;
	}

	if (fbi->var.bits_per_pixel == 24) {
		for (i = 0; i < npixels; i++) {
			u8 b = src[i * 3 + 0];
			u8 g = src[i * 3 + 1];
			u8 r = src[i * 3 + 2];
			priv->lum_buf[i] = max3(r, g, b);
		}
	} else {
		memcpy(priv->lum_buf, src, npixels);
	}

	ret = show_pic_app_cmd_locked(priv, priv->lum_buf);
	mutex_unlock(&priv->lock);

	return ret;
}

static void lp_fb_push_work_fn(struct work_struct *work)
{
	struct gowin_priv *priv = container_of(to_delayed_work(work),
					       struct gowin_priv,
					       fb_push_work);

	if (priv->fbi)
		lp_fb_sync(priv->fbi);
}

static int lp_fb_open(struct fb_info *fbi, int user)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;

	priv->fb_open_count++;
	return 0;
}

static int lp_fb_release(struct fb_info *fbi, int user)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;

	if (priv->fb_open_count > 0)
		priv->fb_open_count--;
	lp_fb_sync(fbi);
	return 0;
}

static int lp_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;

	var->xres = priv->width;
	var->yres = priv->height;
	var->xres_virtual = priv->width;
	var->yres_virtual = priv->height;

	if (var->bits_per_pixel != 8 && var->bits_per_pixel != 24)
		var->bits_per_pixel = (priv->dev_type == LED_SCREEN) ? 24 : 8;

	var->grayscale = 0;
	if (var->bits_per_pixel == 24) {
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
	} else {
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
	}
	var->transp.offset = 0;
	var->transp.length = 0;
	var->nonstd = 0;
	var->activate = FB_ACTIVATE_NOW;
	var->height = -1;
	var->width = -1;
	var->pixclock = 0;
	var->left_margin = 0;
	var->right_margin = 0;
	var->upper_margin = 0;
	var->lower_margin = 0;
	var->hsync_len = 0;
	var->vsync_len = 0;
	return 0;
}

static int lp_fb_set_par(struct fb_info *fbi)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;

	fbi->fix.line_length = priv->width * (fbi->var.bits_per_pixel / 8);
	return 0;
}

static int lp_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	return lp_fb_sync(fbi);
}

static int lp_fb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	return remap_vmalloc_range(vma, fbi->screen_buffer, vma->vm_pgoff);
}

static ssize_t lp_fb_write(struct fb_info *fbi, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	size_t total_size = fbi->screen_size;

	if (p >= total_size)
		return 0;
	if (count > total_size - p)
		count = total_size - p;

	if (copy_from_user(fbi->screen_buffer + p, buf, count))
		return -EFAULT;

	*ppos += count;
	lp_fb_sync(fbi);
	return count;
}

static void lp_fb_fillrect(struct fb_info *fbi, const struct fb_fillrect *rect)
{
	struct gowin_priv *priv = fbi->par;

	sys_fillrect(fbi, rect);
	mod_delayed_work(system_dfl_wq, &priv->fb_push_work, msecs_to_jiffies(20));
}

static void lp_fb_copyarea(struct fb_info *fbi, const struct fb_copyarea *area)
{
	struct gowin_priv *priv = fbi->par;

	sys_copyarea(fbi, area);
	mod_delayed_work(system_dfl_wq, &priv->fb_push_work, msecs_to_jiffies(20));
}

static void lp_fb_imageblit(struct fb_info *fbi, const struct fb_image *image)
{
	struct gowin_priv *priv = fbi->par;

	sys_imageblit(fbi, image);
	mod_delayed_work(system_dfl_wq, &priv->fb_push_work, msecs_to_jiffies(20));
}

static ssize_t lp_fb_read(struct fb_info *fbi, char __user *buf,
			  size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	size_t total_size = fbi->screen_size;

	if (!fbi->screen_buffer || p >= total_size)
		return 0;
	if (count > total_size - p)
		count = total_size - p;

	if (copy_to_user(buf, fbi->screen_buffer + p, count))
		return -EFAULT;

	*ppos += count;
	return count;
}

static int lp_fb_cursor(struct fb_info *fbi, struct fb_cursor *cursor)
{
	/* Ignore cursor rendering requests from fbcon to prevent screen clipping */
	return 0;
}

static struct fb_ops fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = lp_fb_open,
	.fb_release = lp_fb_release,
	.fb_read = lp_fb_read,
	.fb_write = lp_fb_write,
	.fb_check_var = lp_fb_check_var,
	.fb_set_par = lp_fb_set_par,
	.fb_pan_display = lp_fb_pan_display,
	.fb_mmap = lp_fb_mmap,
	.fb_sync = lp_fb_sync,
	.fb_fillrect = lp_fb_fillrect,
	.fb_copyarea = lp_fb_copyarea,
	.fb_imageblit = lp_fb_imageblit,
	.fb_cursor = lp_fb_cursor,
};

static int fb_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	struct fb_info *fbi;
	int bpp = (priv->dev_type == LED_SCREEN) ? 24 : 8;
	int stride = priv->width * (bpp / 8);
	int fb_len = PAGE_ALIGN(stride * priv->height);
	int npixels = priv->width * priv->height;
	int ret;

	INIT_DELAYED_WORK(&priv->fb_push_work, lp_fb_push_work_fn);

	priv->lum_buf = devm_kzalloc(dev, npixels, GFP_KERNEL);
	priv->tx_buf = devm_kzalloc(dev, 4 + npixels + 2, GFP_KERNEL);
	if (!priv->lum_buf || !priv->tx_buf)
		return -ENOMEM;

	fbi = framebuffer_alloc(0, dev);
	if (!fbi)
		return -ENOMEM;

	priv->fbi = fbi;
	fbi->par = priv;

	priv->fb_mem = vzalloc(fb_len);
	if (!priv->fb_mem) {
		ret = -ENOMEM;
		goto err_fb;
	}

	fbi->screen_buffer = priv->fb_mem;
	fbi->screen_size = fb_len;
	fbi->fix.smem_len = fb_len;
	fbi->fix.smem_start = (unsigned long)priv->fb_mem;
	fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	fbi->fix.visual = (bpp == 24) ? FB_VISUAL_TRUECOLOR : FB_VISUAL_PSEUDOCOLOR;
	fbi->fix.line_length = stride;
	fbi->fix.accel = FB_ACCEL_NONE;
	snprintf(fbi->fix.id, sizeof(fbi->fix.id), "%s",
		 (priv->dev_type == LED_SCREEN) ? GOWIN_FB_NAME_SCREEN : GOWIN_FB_NAME_PANEL);

	fbi->var.xres = priv->width;
	fbi->var.yres = priv->height;
	fbi->var.xres_virtual = priv->width;
	fbi->var.yres_virtual = priv->height;
	fbi->var.bits_per_pixel = bpp;
	fbi->var.activate = FB_ACTIVATE_NOW;
	if (bpp == 24) {
		fbi->var.red.offset = 16;
		fbi->var.red.length = 8;
		fbi->var.green.offset = 8;
		fbi->var.green.length = 8;
		fbi->var.blue.offset = 0;
		fbi->var.blue.length = 8;
	} else {
		fbi->var.red.offset = 0;
		fbi->var.red.length = 8;
		fbi->var.green.offset = 0;
		fbi->var.green.length = 8;
		fbi->var.blue.offset = 0;
		fbi->var.blue.length = 8;
	}

	fbi->pseudo_palette = priv->pseudo_palette;
	fbi->flags = FBINFO_VIRTFB;
	fbi->fbops = &fb_ops;

	ret = register_framebuffer(fbi);
	if (ret) {
		dev_err(dev, "[GOWIN] ERROR: register_framebuffer failed: %d\n", ret);
		goto err_mem;
	}

	/* Clear screen on probe so the panel starts blank */
	lp_fb_sync(fbi);

	dev_info(dev, "[GOWIN] Registered framebuffer fb%d ('%s'): %dx%d %dbpp\n",
		 fbi->node, fbi->fix.id, priv->width, priv->height, bpp);
	return 0;

err_mem:
	vfree(priv->fb_mem);
err_fb:
	framebuffer_release(fbi);
	priv->fbi = NULL;
	return ret;
}

static void fb_exit(struct gowin_priv *priv)
{
	cancel_delayed_work_sync(&priv->fb_push_work);
	if (priv->fbi) {
		unregister_framebuffer(priv->fbi);
		vfree(priv->fb_mem);
		framebuffer_release(priv->fbi);
		priv->fbi = NULL;
	}
}

/* ------------------------------------------------------------------ */
/* Backlight                                                          */
/* ------------------------------------------------------------------ */

static int lp_bl_get_brightness(struct backlight_device *bl)
{
	struct gowin_priv *priv = bl_get_data(bl);
	return priv->brightness;
}

static int lp_bl_update_status(struct backlight_device *bl)
{
	struct gowin_priv *priv = bl_get_data(bl);
	unsigned int br = bl->props.brightness;

	if (br > priv->max_brightness)
		br = priv->max_brightness;

	priv->brightness = br;
	return set_brightness_app_cmd(priv, priv->brightness);
}

static const struct backlight_ops lp_bl_ops = {
	.get_brightness = lp_bl_get_brightness,
	.update_status = lp_bl_update_status,
};

static int backlight_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	const char *bl_name = "led_panel_bl";
	char fb_bl_name[32];
	struct backlight_properties props;
	u32 max_br = 200;

	if (!of_property_read_u32(dev->of_node, "max-brightness", &max_br))
		priv->max_brightness = min_t(u32, max_br, 255);
	else
		priv->max_brightness = 200;

	priv->brightness = priv->max_brightness;

	if (priv->dev_type == LED_SCREEN && priv->fbi) {
		snprintf(fb_bl_name, sizeof(fb_bl_name), "fb%d", priv->fbi->node);
		bl_name = fb_bl_name;
	}

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = priv->max_brightness;
	props.brightness = priv->brightness;

	priv->bl = backlight_device_register(bl_name, dev, priv,
					     &lp_bl_ops, &props);
	if (IS_ERR(priv->bl))
		return PTR_ERR(priv->bl);

	set_brightness_app_cmd(priv, priv->brightness);
	return 0;
}

static void backlight_exit(struct gowin_priv *priv)
{
	if (priv->bl)
		backlight_device_unregister(priv->bl);
}

/* ------------------------------------------------------------------ */
/* JTAG -- Gowin FPGA programming                                      */
/* ------------------------------------------------------------------ */

static struct device *get_spi_master_dev(struct spi_device *spi)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	struct device *mdev = spi->controller ? spi->controller->dev.parent : NULL;

	if (!mdev)
		mdev = spi->controller ? &spi->controller->dev : NULL;
#else
	struct device *mdev = spi->master ? spi->master->dev.parent : NULL;

	if (!mdev)
		mdev = spi->master ? &spi->master->dev : NULL;
#endif
	return mdev;
}

static void idle_spi_master_pins(struct spi_device *spi)
{
	struct device *mdev = get_spi_master_dev(spi);

	if (!mdev)
		return;
	pinctrl_pm_select_idle_state(mdev);
}

static void restore_spi_master_pins(struct spi_device *spi)
{
	struct device *mdev = get_spi_master_dev(spi);

	if (!mdev)
		return;
	pinctrl_pm_select_default_state(mdev);
}

static inline void jtag_set_tms(struct gowin_priv *priv, int val)
{
	if (priv->jtag_tms)
		gpiod_set_raw_value(priv->jtag_tms, val ? 1 : 0);
}

static int select_jtag_mode(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;

	if (priv->jtag_mode)
		return 0;

	/* Switch spicc1 pins (GPIOH_4, GPIOH_5, GPIOH_7) to gpio_periphs ("idle" state) */
	idle_spi_master_pins(priv->spi);

	if (priv->pinctrl && priv->pinctrl_jtag)
		pinctrl_select_state(priv->pinctrl, priv->pinctrl_jtag);

	priv->jtag_tck = devm_gpiod_get_optional(dev, "jtag_tck", GPIOD_OUT_LOW);
	priv->jtag_tdi = devm_gpiod_get_optional(dev, "jtag_tdi", GPIOD_OUT_LOW);
	priv->jtag_tdo = devm_gpiod_get_optional(dev, "jtag_tdo", GPIOD_IN);

	priv->tms_from_spi_cs = false;
	priv->jtag_tms = devm_gpiod_get_optional(dev, "jtag_tms", GPIOD_OUT_LOW);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (IS_ERR_OR_NULL(priv->jtag_tms) && spi_get_csgpiod(priv->spi, 0)) {
		priv->jtag_tms = spi_get_csgpiod(priv->spi, 0);
		priv->tms_from_spi_cs = true;
		jtag_set_tms(priv, 0);
	}
#endif

	if (IS_ERR_OR_NULL(priv->jtag_tck) || IS_ERR_OR_NULL(priv->jtag_tdi) ||
	    IS_ERR_OR_NULL(priv->jtag_tms) || IS_ERR_OR_NULL(priv->jtag_tdo)) {
		dev_err(dev, "[GOWIN-JTAG] Failed to get JTAG GPIOs: tck=%d, tdi=%d, tms=%d, tdo=%d\n",
			PTR_ERR_OR_ZERO(priv->jtag_tck),
			PTR_ERR_OR_ZERO(priv->jtag_tdi),
			PTR_ERR_OR_ZERO(priv->jtag_tms),
			PTR_ERR_OR_ZERO(priv->jtag_tdo));
		if (!IS_ERR_OR_NULL(priv->jtag_tck))
			devm_gpiod_put(dev, priv->jtag_tck);
		if (!IS_ERR_OR_NULL(priv->jtag_tdi))
			devm_gpiod_put(dev, priv->jtag_tdi);
		if (!priv->tms_from_spi_cs && !IS_ERR_OR_NULL(priv->jtag_tms))
			devm_gpiod_put(dev, priv->jtag_tms);
		if (!IS_ERR_OR_NULL(priv->jtag_tdo))
			devm_gpiod_put(dev, priv->jtag_tdo);
		priv->jtag_tck = NULL;
		priv->jtag_tdi = NULL;
		priv->jtag_tms = NULL;
		priv->jtag_tdo = NULL;
		priv->tms_from_spi_cs = false;
		restore_spi_master_pins(priv->spi);
		return -ENODEV;
	}

	if (priv->jtag_sel)
		gpiod_set_value_cansleep(priv->jtag_sel, 0);

	dev_info(dev, "[GOWIN-JTAG] JTAG mode enabled (tck=hw:%d, tdi=hw:%d, tdo=hw:%d, tms=hw:%d)\n",
		 desc_to_gpio(priv->jtag_tck), desc_to_gpio(priv->jtag_tdi),
		 desc_to_gpio(priv->jtag_tdo), desc_to_gpio(priv->jtag_tms));
	priv->jtag_mode = true;
	return 0;
}

static int select_spi_mode(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;

	if (priv->jtag_tck) {
		devm_gpiod_put(dev, priv->jtag_tck);
		priv->jtag_tck = NULL;
	}
	if (priv->jtag_tdi) {
		devm_gpiod_put(dev, priv->jtag_tdi);
		priv->jtag_tdi = NULL;
	}
	if (priv->jtag_tms) {
		/* Leave physical CS pin HIGH (inactive) for SPI mode */
		jtag_set_tms(priv, 1);
		if (!priv->tms_from_spi_cs)
			devm_gpiod_put(dev, priv->jtag_tms);
		priv->jtag_tms = NULL;
		priv->tms_from_spi_cs = false;
	}
	if (priv->jtag_tdo) {
		devm_gpiod_put(dev, priv->jtag_tdo);
		priv->jtag_tdo = NULL;
	}

	if (priv->jtag_sel)
		gpiod_set_value_cansleep(priv->jtag_sel, 1);

	if (priv->pinctrl && priv->pinctrl_default)
		pinctrl_select_state(priv->pinctrl, priv->pinctrl_default);

	restore_spi_master_pins(priv->spi);

	priv->jtag_mode = false;
	return 0;
}

static inline void jtag_pulse_tck(struct gowin_priv *priv)
{
	gpiod_set_value(priv->jtag_tck, 0);
	ndelay(100);
	gpiod_set_value(priv->jtag_tck, 1);
	ndelay(100);
}

static inline void jtag_pulse_tck_n(struct gowin_priv *priv, unsigned int count)
{
	while (count--)
		jtag_pulse_tck(priv);
}

static void jtag_update_tms(struct gowin_priv *priv, u8 tms_bit)
{
	jtag_set_tms(priv, tms_bit & 1);
	jtag_pulse_tck(priv);
}

/*
 * Exact Gowin JTAG TAP state transitions from gowin_led_device.ko (0x3828):
 *   16 = TEST_LOGIC_RESET
 *    1 = RUN_TEST_IDLE
 *    4 = SHIFT_DR
 *    5 = EXIT1_DR
 *   11 = SHIFT_IR
 *   12 = EXIT1_IR
 */
static void jtag_tap_move(struct gowin_priv *priv, u8 from_state, u8 to_state)
{
	int i;

	if (from_state == 16 && to_state == 1) {
		for (i = 0; i < 8; i++)
			jtag_update_tms(priv, 1);
		jtag_update_tms(priv, 0);
		jtag_update_tms(priv, 0);
	} else if (from_state == 1 && to_state == 1) {
		for (i = 0; i < 3; i++)
			jtag_update_tms(priv, 0);
	} else if (from_state == 1 && to_state == 11) {
		jtag_update_tms(priv, 1);
		jtag_update_tms(priv, 1);
		jtag_update_tms(priv, 0);
		jtag_update_tms(priv, 0);
	} else if (from_state == 1 && to_state == 4) {
		jtag_update_tms(priv, 1);
		jtag_update_tms(priv, 0);
		jtag_update_tms(priv, 0);
	} else if ((from_state == 12 && to_state == 1) ||
		   (from_state == 5 && to_state == 1)) {
		jtag_update_tms(priv, 1);
		jtag_update_tms(priv, 0);
		jtag_update_tms(priv, 0);
	}
}

static void jtag_write_byte(struct gowin_priv *priv, u8 byte, u8 exit_tms)
{
	int bit;

	jtag_set_tms(priv, 0);
	for (bit = 0; bit < 8; bit++) {
		gpiod_set_value(priv->jtag_tdi, (byte >> bit) & 1);
		if (bit == 7)
			jtag_set_tms(priv, exit_tms & 1);
		jtag_pulse_tck(priv);
	}
}

static void jtag_write_inst(struct gowin_priv *priv, u8 inst)
{
	jtag_tap_move(priv, 1, 11);
	jtag_write_byte(priv, inst, 1);
	jtag_tap_move(priv, 12, 1);
	jtag_tap_move(priv, 1, 1);
	jtag_tap_move(priv, 1, 1);
	jtag_tap_move(priv, 1, 1);
}

static u32 jtag_read_code(struct gowin_priv *priv, u8 inst)
{
	u32 val = 0;
	int i, bit;

	jtag_write_inst(priv, inst);
	jtag_tap_move(priv, 1, 4);
	jtag_set_tms(priv, 0);

	for (i = 0; i < 32; i++) {
		if (i == 31)
			jtag_set_tms(priv, 1);
		jtag_pulse_tck(priv);
		bit = gpiod_get_value(priv->jtag_tdo);
		val |= (u32)(bit & 1) << i;
	}

	jtag_tap_move(priv, 5, 1);
	if (inst == 0x11)
		priv->last_idcode = val;
	else if (inst == 0x13)
		priv->last_usercode = val;
	else if (inst == 0x41)
		priv->last_status_code = val;

	return val;
}

static int check_status_code(struct gowin_priv *priv, u32 status)
{
	if ((status & 0xfffdffff) != 0x00019020) {
		dev_info(priv->dev,
			"[GOWIN-JTAG] status code: 0x%08x (SRAM erase required)\n",
			status);
		return -EPROTO;
	}
	return 0;
}

static int jtag_erase_flash(struct gowin_priv *priv)
{
	u32 status;
	int i;

	jtag_tap_move(priv, 16, 1);
	status = jtag_read_code(priv, 0x41);
	if (check_status_code(priv, status) != 0) {
		jtag_write_inst(priv, 0x11);
		jtag_write_inst(priv, 0x15);
		jtag_write_inst(priv, 0x05);
		for (i = 0; i < 4000; i++)
			jtag_update_tms(priv, 0);
		jtag_write_inst(priv, 0x09);
		jtag_write_inst(priv, 0x02);
		jtag_write_inst(priv, 0x3a);
		jtag_write_inst(priv, 0x02);
		for (i = 0; i < 1000; i++)
			jtag_update_tms(priv, 0);
		status = jtag_read_code(priv, 0x41);
	}

	dev_info(priv->dev, "[GOWIN-JTAG] Erasing FPGA flash (status=0x%08x)...\n", status);
	jtag_write_inst(priv, 0x11);
	jtag_write_inst(priv, 0x15);
	jtag_write_inst(priv, 0x75);
	for (i = 0; i < 6; i++)
		jtag_update_tms(priv, 0);
	jtag_tap_move(priv, 1, 4);
	for (i = 0; i < 7; i++)
		jtag_update_tms(priv, 0);
	jtag_update_tms(priv, 1);
	jtag_update_tms(priv, 0);
	for (i = 0; i < 16; i++)
		jtag_update_tms(priv, 0);
	jtag_update_tms(priv, 1);
	jtag_update_tms(priv, 0);
	jtag_update_tms(priv, 1);
	jtag_update_tms(priv, 1);
	for (i = 0; i < 4; i++)
		jtag_update_tms(priv, 0);

	jtag_pulse_tck_n(priv, 320000);
	jtag_write_inst(priv, 0x3a);
	jtag_write_inst(priv, 0x02);
	msleep(200);
	jtag_write_inst(priv, 0x11);
	jtag_write_inst(priv, 0x3c);
	jtag_write_inst(priv, 0x02);
	msleep(200);

	status = jtag_read_code(priv, 0x41);
	if (status & 0x0f) {
		dev_err(priv->dev, "[GOWIN-JTAG] Flash erase failed: status=0x%08x (error bits set)\n", status);
		return -EPROTO;
	}
	dev_info(priv->dev, "[GOWIN-JTAG] Flash erase verified OK (status=0x%08x)\n", status);
	return 0;
}

static int jtag_prog_fpga(struct gowin_priv *priv, const struct firmware *fw,
			  u32 expected_user_code)
{
	size_t aligned_len = ALIGN(fw->size, 256);
	size_t num_pages = aligned_len / 256;
	size_t page, offset;
	u8 *buf;
	u32 idcode, usercode;
	int i, bit;

	jtag_tap_move(priv, 16, 1);
	idcode = jtag_read_code(priv, 0x11);
	dev_info(priv->dev, "[GOWIN-JTAG] Programming FPGA (%zu bytes, IDCODE=0x%08x)...\n",
		 aligned_len, idcode);
	if (idcode == 0 || idcode == 0xffffffff) {
		dev_err(priv->dev, "[GOWIN-JTAG] Invalid IDCODE 0x%08x, aborting\n", idcode);
		return -ENODEV;
	}

	buf = kmalloc(aligned_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(buf, 0xff, aligned_len);
	memcpy(buf, fw->data, fw->size);
	buf[0] = 'G';
	buf[1] = 'W';
	buf[2] = '1';
	buf[3] = 'N';

	jtag_write_inst(priv, 0x15);
	for (i = 0; i < 4000; i++)
		jtag_update_tms(priv, 0);

	for (page = 0; page < num_pages; page++) {
		const u8 *p = buf + page * 256;

		jtag_write_inst(priv, 0x15);
		jtag_write_inst(priv, 0x71);
		if (page > 0)
			jtag_pulse_tck_n(priv, 16);

		jtag_tap_move(priv, 1, 4);
		for (i = 0; i < 6; i++) {
			gpiod_set_value(priv->jtag_tdi, 0);
			jtag_pulse_tck(priv);
		}
		for (bit = 0; bit < 26; bit++) {
			gpiod_set_value(priv->jtag_tdi, (page >> bit) & 1);
			if (bit == 25)
				jtag_set_tms(priv, 1);
			jtag_pulse_tck(priv);
		}
		jtag_tap_move(priv, 5, 1);
		jtag_pulse_tck_n(priv, 360);

		for (offset = 0; offset < 256; offset += 4) {
			jtag_tap_move(priv, 1, 4);
			jtag_write_byte(priv, p[offset + 3], 0);
			jtag_write_byte(priv, p[offset + 2], 0);
			jtag_write_byte(priv, p[offset + 1], 0);
			jtag_write_byte(priv, p[offset + 0], 1);
			jtag_tap_move(priv, 5, 1);
			jtag_tap_move(priv, 1, 1);
			jtag_tap_move(priv, 1, 1);
			jtag_tap_move(priv, 1, 1);
			jtag_pulse_tck_n(priv, 24);
		}
		jtag_pulse_tck_n(priv, 14);
	}

	jtag_write_inst(priv, 0x3a);
	jtag_write_inst(priv, 0x02);
	for (i = 0; i < 300; i++)
		jtag_update_tms(priv, 0);
	jtag_write_inst(priv, 0x11);
	jtag_write_inst(priv, 0x3c);
	jtag_write_inst(priv, 0x02);
	msleep(300);
	kfree(buf);

	usercode = jtag_read_code(priv, 0x13);
	dev_info(priv->dev, "[GOWIN-JTAG] Post-program USERCODE=0x%08x (expected 0x%08x)\n",
		 usercode, expected_user_code);
	if (expected_user_code && usercode != expected_user_code) {
		dev_warn(priv->dev, "[GOWIN-JTAG] USERCODE mismatch: actual=0x%08x expected=0x%08x (proceeding)\n",
			 usercode, expected_user_code);
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Firmware update & Reset                                             */
/* ------------------------------------------------------------------ */

static void fpga_hw_reset(struct gowin_priv *priv)
{
	if (priv->reset_gpio) {
		/* reset-gpios is GPIO_ACTIVE_LOW: logical 1 = assert reset, logical 0 = release */
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		msleep(10);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(300);
	}
}

static int lp_update_firmware(struct gowin_priv *priv,
			      const struct firmware *fw, u32 expected_user_code)
{
	int ret;

	ret = select_jtag_mode(priv);
	if (ret)
		return ret;

	fpga_hw_reset(priv);
	ret = jtag_erase_flash(priv);
	if (ret == 0) {
		msleep(200);
		ret = jtag_prog_fpga(priv, fw, expected_user_code);
	}

	select_spi_mode(priv);
	return ret;
}

static int lp_handle_firmware_update(struct gowin_priv *priv)
{
	const char *fw_name;
	const char *manifest_name;
	const struct firmware *fw = NULL;
	const struct firmware *manifest_fw = NULL;
	u8 manifest_ver = 0, force_upd = 0;
	u16 manifest_crc = 0, manifest_uf_crc = 0;
	u32 manifest_user_code = 0;
	int ret;

	if (priv->dev_type == LED_PANEL) {
		fw_name = "yandex_led_panel.bin";
		manifest_name = "yandex_led_panel.bin.manifest";
	} else {
		fw_name = "yandex_led_screen_fpga.bin";
		manifest_name = "yandex_led_screen_fpga.bin.manifest";
	}

	if (request_firmware(&manifest_fw, manifest_name, priv->dev) == 0 &&
	    manifest_fw && manifest_fw->data) {
		char mbuf[128];
		size_t mlen = min_t(size_t, manifest_fw->size, sizeof(mbuf) - 1);

		memcpy(mbuf, manifest_fw->data, mlen);
		mbuf[mlen] = '\0';
		sscanf(mbuf,
		       "fw_version = %hhu\ncrc16 = %hx\nuser_code = %x\nuf_crc16 = %hx\nforce_upd = %hhu",
		       &manifest_ver, &manifest_crc, &manifest_user_code,
		       &manifest_uf_crc, &force_upd);
		release_firmware(manifest_fw);
	}

	/*
	 * Call get_lp_status twice just like vendor gowin_led_device.ko (0x2a10/0x2a1c):
	 * the first SPI transfer wakes/syncs the slave state machine.
	 */
	get_lp_status(priv, NULL);
	ret = get_lp_status(priv, NULL);
	dev_info(priv->dev, "[GOWIN] Initial check: get_lp_status ret=%d (board_rev=%u, fw_version=%u, uf_crc=0x%04x)\n",
		 ret, priv->board_rev, priv->fw_version, priv->uf_crc);

	if (ret == 0 && priv->fw_version > 0 && !force_upd &&
	    (manifest_ver == 0 || priv->fw_version >= manifest_ver)) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "ok: running (board_rev=%u fw_version=%u uf_crc=0x%04x)",
			 priv->board_rev, priv->fw_version, priv->uf_crc);
		dev_info(priv->dev, "[GOWIN] FPGA already running valid firmware (version %u)\n", priv->fw_version);
		return 0;
	}

	/* Try hardware reset once before attempting JTAG flash */
	dev_info(priv->dev, "[GOWIN] Resetting FPGA to probe status...\n");
	fpga_hw_reset(priv);
	get_lp_status(priv, NULL);
	ret = get_lp_status(priv, NULL);
	dev_info(priv->dev, "[GOWIN] Post-reset check: get_lp_status ret=%d (board_rev=%u, fw_version=%u, uf_crc=0x%04x)\n",
		 ret, priv->board_rev, priv->fw_version, priv->uf_crc);

	if (ret == 0 && priv->fw_version > 0 && !force_upd &&
	    (manifest_ver == 0 || priv->fw_version >= manifest_ver)) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "ok: reset (board_rev=%u fw_version=%u uf_crc=0x%04x)",
			 priv->board_rev, priv->fw_version, priv->uf_crc);
		dev_info(priv->dev, "[GOWIN] FPGA operational after reset (version %u)\n", priv->fw_version);
		return 0;
	}

	dev_info(priv->dev, "[GOWIN] Requesting firmware bitstream '%s'...\n", fw_name);
	ret = request_firmware(&fw, fw_name, priv->dev);
	if (ret) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "fw not found: %s (status=%d)", fw_name, ret);
		dev_err(priv->dev, "[GOWIN] request_firmware('%s') failed: %d\n", fw_name, ret);
		return ret;
	}

	snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
		 "flashing %s (%zu bytes)", fw_name, fw->size);
	dev_info(priv->dev, "[GOWIN] Firmware '%s' loaded (%zu bytes). Flashing via JTAG...\n",
		 fw_name, fw->size);

	ret = lp_update_firmware(priv, fw, manifest_user_code);
	release_firmware(fw);

	if (ret) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "flash failed: %d", ret);
		dev_err(priv->dev, "[GOWIN] Firmware flash failed with error %d\n", ret);
	} else {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "ok: %s programmed", fw_name);
		dev_info(priv->dev, "[GOWIN] Firmware flash complete! Probing new status...\n");
		msleep(100);
		get_lp_status(priv, NULL);
		get_lp_status(priv, NULL);
		dev_info(priv->dev, "[GOWIN] Final status: board_rev=%u, fw_version=%u, uf_crc=0x%04x\n",
			 priv->board_rev, priv->fw_version, priv->uf_crc);
	}

	return ret;
}

/* ------------------------------------------------------------------ */
/* Sysfs                                                              */
/* ------------------------------------------------------------------ */

static ssize_t help_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf,
		"help                 - this help\n"
		"open_fb_count        - number of open fb instances\n"
		"fw_upd_status        - firmware update status\n"
		"crc_errors           - slave/master CRC errors\n"
		"power_saving_mode    - (rw) 0=OFF 1=ON\n"
		"set_animation_mode   - (wo) 0=stop 1=start anim\n"
		"gpio_mode            - (rw) jtag|spi\n"
		"frame_queue_overflows- frame queue overflow count\n"
		"next_frame_delay     - (rw) frame delay\n"
		"show_debug_info      - full live hardware & SPI status\n"
		"frames_in_queue      - frames in queue\n"
		"press_key            - (wo) keycode to send\n"
		"jtag/jtag_reconfig   - (rw) JTAG reconfig pin\n"
		"jtag/jtag_codes      - ID/USER/STATUS codes\n"
		"jtag/jtag_erase_flash- flash erase attempts\n"
		"jtag/jtag_fpga_prog  - FPGA prog attempts\n"
		"jtag/test_fpga_prog  - (wo) test FPGA prog N times\n");
}

static ssize_t open_fb_count_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", priv->fb_open_count);
}

static ssize_t fw_upd_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%s\n", priv->fw_upd_status);
}

static ssize_t crc_errors_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "master=%u slave=%u\n",
			  priv->crc_errors_master, priv->crc_errors_slave);
}

static ssize_t power_saving_mode_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", priv->power_saving ? 1 : 0);
}

static ssize_t power_saving_mode_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	priv->power_saving = !!val;
	set_power_saving_mode_cmd(priv, priv->power_saving);

	return len;
}

static ssize_t set_animation_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	set_animation_app_cmd(priv, val);
	return len;
}

static ssize_t gpio_mode_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%s\n", priv->jtag_mode ? "jtag" : "spi");
}

static ssize_t gpio_mode_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&priv->lock);
	if (sysfs_streq(buf, "jtag"))
		ret = select_jtag_mode(priv);
	else
		ret = select_spi_mode(priv);
	mutex_unlock(&priv->lock);

	return ret ? ret : len;
}

static ssize_t frame_queue_overflows_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	get_lp_status(priv, NULL);
	return sysfs_emit(buf, "%u\n", priv->frame_queue_overflows);
}

static ssize_t next_frame_delay_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", priv->next_frame_delay);
}

static ssize_t next_frame_delay_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = kstrtouint(buf, 0, &priv->next_frame_delay);
	if (ret)
		return ret;

	return len;
}

static ssize_t show_debug_info_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	int st_ret = get_lp_status(priv, NULL);

	return sysfs_emit(buf,
			  "status_ret=%d mode=%s fw_upd_status=\"%s\"\n"
			  "board_rev=%u fw_version=%u uf_crc=0x%04x last_idcode=0x%08x\n"
			  "frames_pushed=%lu fiq=%u fqo=%u crc_master=%u crc_slave=%u spi_err=%lu\n"
			  "brightness=%u/%u reset_gpio=hw:%d jtag_sel_gpio=hw:%d reconfig_gpio=hw:%d\n"
			  "last_rx=%16ph\n",
			  st_ret, priv->jtag_mode ? "jtag" : "spi",
			  priv->fw_upd_status,
			  priv->board_rev, priv->fw_version, priv->uf_crc,
			  priv->last_idcode,
			  priv->frames_pushed, priv->frames_in_queue,
			  priv->frame_queue_overflows,
			  priv->crc_errors_master, priv->crc_errors_slave,
			  priv->spi_tx_errors,
			  priv->brightness, priv->max_brightness,
			  priv->reset_gpio ? desc_to_gpio(priv->reset_gpio) : -1,
			  priv->jtag_sel ? desc_to_gpio(priv->jtag_sel) : -1,
			  priv->jtag_reconfig ? desc_to_gpio(priv->jtag_reconfig) : -1,
			  priv->last_status_buf);
}

static ssize_t frames_in_queue_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	get_lp_status(priv, NULL);
	return sysfs_emit(buf, "%u\n", priv->frames_in_queue);
}

static ssize_t press_key_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	unsigned int key;
	int ret;

	if (!priv->touch_dev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &key);
	if (ret)
		return ret;

	input_event(priv->touch_dev, EV_KEY, key, 1);
	input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);
	input_event(priv->touch_dev, EV_KEY, key, 0);
	input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);

	return len;
}

/* Main group attributes */
static DEVICE_ATTR_RO(help);
static DEVICE_ATTR_RO(open_fb_count);
static DEVICE_ATTR_RO(fw_upd_status);
static DEVICE_ATTR_RO(crc_errors);
static DEVICE_ATTR_RW(power_saving_mode);
static DEVICE_ATTR_WO(set_animation_mode);
static DEVICE_ATTR_RW(gpio_mode);
static DEVICE_ATTR_RO(frame_queue_overflows);
static DEVICE_ATTR_RW(next_frame_delay);
static DEVICE_ATTR_RO(show_debug_info);
static DEVICE_ATTR_RO(frames_in_queue);
static DEVICE_ATTR_WO(press_key);

static struct attribute *lp_common_attrs[] = {
	&dev_attr_help.attr,
	&dev_attr_open_fb_count.attr,
	&dev_attr_fw_upd_status.attr,
	&dev_attr_crc_errors.attr,
	&dev_attr_power_saving_mode.attr,
	&dev_attr_set_animation_mode.attr,
	&dev_attr_gpio_mode.attr,
	&dev_attr_frame_queue_overflows.attr,
	&dev_attr_next_frame_delay.attr,
	&dev_attr_show_debug_info.attr,
	&dev_attr_frames_in_queue.attr,
	&dev_attr_press_key.attr,
	NULL,
};

static const struct attribute_group lp_common_group = {
	.attrs = lp_common_attrs,
};

/* JTAG group */
static ssize_t jtag_reconfig_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	int val = priv->jtag_reconfig ? gpiod_get_value_cansleep(priv->jtag_reconfig) : 0;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t jtag_reconfig_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	if (priv->jtag_reconfig)
		gpiod_set_value_cansleep(priv->jtag_reconfig, !!val);

	return len;
}

static ssize_t jtag_fpga_prog_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}

static ssize_t jtag_codes_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	u32 idcode, usercode, status_code;

	mutex_lock(&priv->lock);
	if (priv->jtag_mode) {
		jtag_tap_move(priv, 16, 1);
		idcode = jtag_read_code(priv, 0x11);
		usercode = jtag_read_code(priv, 0x13);
		status_code = jtag_read_code(priv, 0x41);
	} else {
		idcode = priv->last_idcode;
		usercode = priv->last_usercode;
		status_code = priv->last_status_code;
	}
	mutex_unlock(&priv->lock);

	return sysfs_emit(buf, "ID=0x%08x USER=0x%08x STATUS=0x%08x\n",
			  idcode, usercode, status_code);
}

static ssize_t jtag_erase_flash_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0\n");
}

static ssize_t test_fpga_prog_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	return len;
}

static DEVICE_ATTR_RW(jtag_reconfig);
static DEVICE_ATTR_RO(jtag_fpga_prog);
static DEVICE_ATTR_RO(jtag_codes);
static DEVICE_ATTR_RO(jtag_erase_flash);
static DEVICE_ATTR_WO(test_fpga_prog);

static struct attribute *jtag_attrs[] = {
	&dev_attr_jtag_reconfig.attr,
	&dev_attr_jtag_fpga_prog.attr,
	&dev_attr_jtag_codes.attr,
	&dev_attr_jtag_erase_flash.attr,
	&dev_attr_test_fpga_prog.attr,
	NULL,
};

static const struct attribute_group jtag_group = {
	.name = "jtag",
	.attrs = jtag_attrs,
};

static const struct attribute_group *lp_attr_groups[] = {
	&lp_common_group,
	&jtag_group,
	NULL,
};

/* ------------------------------------------------------------------ */
/* SPI driver probe/remove                                            */
/* ------------------------------------------------------------------ */

static const struct spi_device_id led_device_id[] = {
	{ "led_panel", 0 },
	{ "led_screen", 1 },
	{ }
};

MODULE_DEVICE_TABLE(spi, led_device_id);

static const struct of_device_id led_device_of_match[] = {
	{ .compatible = "ya,led_panel", .data = &cfg_panel },
	{ .compatible = "ya,led_screen", .data = &cfg_screen },
	{ }
};

MODULE_DEVICE_TABLE(of, led_device_of_match);

static int probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct of_device_id *match;
	const struct gowin_config *cfg;
	struct gowin_priv *priv;
	int ret;

	dev_info(dev, "[GOWIN] Probing Gowin FPGA LED controller (spi max_speed=%u Hz)...\n",
		 spi->max_speed_hz);

	match = of_match_node(led_device_of_match, dev->of_node);
	if (!match || !match->data)
		return -EINVAL;

	cfg = match->data;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	priv->dev = dev;
	priv->dev_type = cfg->type;
	priv->width = cfg->width;
	priv->height = cfg->height;
	mutex_init(&priv->lock);
	spi_set_drvdata(spi, priv);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = 4000000;
	ret = spi_setup(spi);
	if (ret)
		dev_warn(dev, "[GOWIN] spi_setup returned %d\n", ret);

	/*
	 * Request ONLY reset, jtag_reconfig, and jtag_sel in probe()!
	 * NEVER request jtag_tck, jtag_tdi, jtag_tdo, or jtag_tms here because
	 * they share GPIOH_4..GPIOH_7 with spicc1 MOSI/MISO/CS/SCLK and
	 * requesting them switches the hardware pinmux away from SPI!
	 */
	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio)) {
		dev_warn(dev, "[GOWIN] reset_gpio unavailable (%ld), continuing\n",
			 PTR_ERR(priv->reset_gpio));
		priv->reset_gpio = NULL;
	}

	priv->jtag_reconfig = devm_gpiod_get_optional(dev, "jtag_reconfig",
						       GPIOD_OUT_LOW);
	if (IS_ERR(priv->jtag_reconfig))
		priv->jtag_reconfig = NULL;

	msleep(300);

	priv->jtag_sel = devm_gpiod_get_optional(dev, "jtag_sel", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->jtag_sel))
		priv->jtag_sel = NULL;

	priv->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
	if (IS_ERR(priv->irq_gpio))
		priv->irq_gpio = NULL;

	dev_info(dev, "[GOWIN] GPIOs: reset=hw:%d, jtag_reconfig=hw:%d, jtag_sel=hw:%d, irq=hw:%d\n",
		 priv->reset_gpio ? desc_to_gpio(priv->reset_gpio) : -1,
		 priv->jtag_reconfig ? desc_to_gpio(priv->jtag_reconfig) : -1,
		 priv->jtag_sel ? desc_to_gpio(priv->jtag_sel) : -1,
		 priv->irq_gpio ? desc_to_gpio(priv->irq_gpio) : -1);

	/* pinctrl */
	priv->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(priv->pinctrl)) {
		priv->pinctrl_default = pinctrl_lookup_state(priv->pinctrl,
							    "default");
		if (IS_ERR(priv->pinctrl_default))
			priv->pinctrl_default = NULL;

		priv->pinctrl_jtag = pinctrl_lookup_state(priv->pinctrl,
							  "jtag");
		if (IS_ERR(priv->pinctrl_jtag))
			priv->pinctrl_jtag = NULL;
	} else {
		priv->pinctrl = NULL;
	}

	/* Ensure SPI mux and spicc1 master pinctrl are active */
	select_spi_mode(priv);

	/* Check FPGA status / firmware before registering devices */
	lp_handle_firmware_update(priv);

	/* Optional subsystems based on DT properties */
	if (of_find_property(dev->of_node, "has-gpio", NULL) ||
	    of_find_property(dev->of_node, "gpio-controller", NULL)) {
		ret = gpio_init(priv);
		if (ret)
			return ret;
		priv->has_gpio = true;
	}

	ret = fb_init(priv);
	if (ret)
		goto err_gpio;

	ret = backlight_init(priv);
	if (ret)
		goto err_fb;

	if (of_find_property(dev->of_node, "has-uart", NULL)) {
		ret = uart_init(priv);
		if (ret)
			goto err_bl;
		priv->has_uart = true;
	}

	if (priv->irq_gpio) {
		ret = init_touch(priv);
		if (ret)
			goto err_uart;
		priv->has_touch = true;
	}

	ret = sysfs_create_groups(&dev->kobj, lp_attr_groups);
	if (ret)
		goto err_touch;

	dev_info(dev, "[GOWIN] LED %s probed OK: %dx%d (board_rev=%u, fw_version=%u, uf_crc=0x%04x, status='%s')\n",
		 (priv->dev_type == LED_SCREEN) ? "screen" : "panel",
		 priv->width, priv->height,
		 priv->board_rev, priv->fw_version, priv->uf_crc,
		 priv->fw_upd_status);
	return 0;

err_touch:
	deinit_touch(priv);
err_uart:
	uart_exit(priv);
err_bl:
	backlight_exit(priv);
err_fb:
	fb_exit(priv);
err_gpio:
	gpio_exit(priv);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
static void remove(struct spi_device *spi)
#else
static int remove(struct spi_device *spi)
#endif
{
	struct gowin_priv *priv = spi_get_drvdata(spi);

	mutex_lock(&priv->lock);
	priv->removed = true;
	mutex_unlock(&priv->lock);

	sysfs_remove_groups(&priv->dev->kobj, lp_attr_groups);
	deinit_touch(priv);
	uart_exit(priv);
	backlight_exit(priv);
	fb_exit(priv);
	gpio_exit(priv);

	dev_info(priv->dev, "[GOWIN] LED panel removed\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	return 0;
#endif
}

static struct spi_driver led_device_driver = {
	.driver = {
		.name = GOWIN_DRV_NAME,
		.of_match_table = led_device_of_match,
	},
	.probe = probe,
	.remove = remove,
	.id_table = led_device_id,
};

module_spi_driver(led_device_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("AtriOS Team");
MODULE_DESCRIPTION("Quasar led panel (SPI)");
