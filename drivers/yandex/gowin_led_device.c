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
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/fb.h>
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
#include <linux/timer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>

#ifndef sysfs_emit
#define sysfs_emit(buf, fmt, ...) scnprintf(buf, PAGE_SIZE, fmt, ##__VA_ARGS__)
#endif

#define GOWIN_DRV_NAME		"led_panel"
#define GOWIN_FB_NAME		"led_panel_fb"

#define GOWIN_GPIO_NGPIO	2
#define GOWIN_TTY_MINOR_COUNT	1
#define GOWIN_TTY_BUF_SIZE	64
#define GOWIN_FB_WIDTH_MAX	320
#define GOWIN_FB_HEIGHT_MAX	320

/* SPI command IDs */
#define CMD_NOP			0x00
#define CMD_GET_STATUS		0x01
#define CMD_SHOW_PIC		0x03
#define CMD_SET_BRIGHTNESS	0x05
#define CMD_SET_GPIO		0x06
#define CMD_SET_ANIMATION	0x07
#define CMD_UART_SEND_DATA	0x09
#define CMD_UART_RECV_DATA	0x0a
#define CMD_UART_FLUSH_DATA	0x0b
#define CMD_SET_POWER_SAVING	0x0c
#define CMD_WRITE		0x10
#define CMD_GET_STATUS_LEN	32

/* Status bits */
#define STATUS_TOUCH_MASK(n)	BIT(n)
#define STATUS_CRC_ERROR_S	BIT(2)
#define STATUS_CRC_ERROR_M	BIT(3)

struct gowin_resolution {
	int width;
	int height;
};

static const struct gowin_resolution rev2res_panel = { .width = 25, .height = 16 };
static const struct gowin_resolution rev2res_screen = { .width = 28, .height = 16 };

struct gowin_touch_regs {
	u32 dbg_val[5];
	u32 metric[5];
	u32 touch_state;
	u32 touch_mask;
};

struct gowin_priv {
	struct spi_device *spi;
	struct device *dev;
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

	/* Resolution */
	int width;
	int height;

	/* GPIO virtual chip cached byte */
	u8 gpio_cached;

	/* Framebuffer */
	void *fb_mem;
	int fb_open_count;
	u32 pseudo_palette[16];

	/* Backlight */
	unsigned int brightness;

	/* Frame queue state */
	unsigned int next_frame_delay;
	unsigned int frames_in_queue;
	unsigned int frame_queue_overflows;

	/* CRC errors */
	unsigned int crc_errors_master;
	unsigned int crc_errors_slave;

	/* Power saving */
	bool power_saving;

	/* Touch state */
	u8 touch_irq_number;

	/* Firmware status */
	char fw_upd_status[40];

	/* Lock */
	struct mutex lock;

	/* TTY xmit buffer */
	unsigned char xmit_buf[GOWIN_TTY_BUF_SIZE];
	int xmit_count;
};

/* ------------------------------------------------------------------ */
/* App protocol -- SPI command layer                                   */
/* ------------------------------------------------------------------ */

static int lp_write(struct gowin_priv *priv, const u8 *data, int len)
{
	struct spi_transfer t = {
		.tx_buf = data,
		.len = len,
	};
	struct spi_message m;

	spi_message_init_with_transfers(&m, &t, 1);
	return spi_sync(priv->spi, &m);
}

static int get_lp_status(struct gowin_priv *priv, u8 *status_buf)
{
	u8 cmd = CMD_GET_STATUS;
	struct spi_transfer tx = {
		.tx_buf = &cmd,
		.len = 1,
	};
	struct spi_transfer rx = {
		.rx_buf = status_buf,
		.len = CMD_GET_STATUS_LEN,
	};
	struct spi_message m;

	spi_message_init_with_transfers(&m, &tx, 1);
	spi_message_add_tail(&rx, &m);
	return spi_sync(priv->spi, &m);
}

static int show_pic_app_cmd(struct gowin_priv *priv)
{
	u8 buf[3] = { CMD_SHOW_PIC, 0x00, 0x00 };
	return lp_write(priv, buf, 3);
}

static int set_brightness_app_cmd(struct gowin_priv *priv, unsigned int val)
{
	u8 buf[3] = { CMD_SET_BRIGHTNESS, val & 0xff, (val >> 8) & 0xff };
	return lp_write(priv, buf, 3);
}

static int set_gpio_app_cmd(struct gowin_priv *priv)
{
	u8 buf[3] = { CMD_SET_GPIO, priv->gpio_cached, 0x00 };
	return lp_write(priv, buf, 3);
}

static int set_animation_app_cmd(struct gowin_priv *priv, int on)
{
	u8 buf[3] = { CMD_SET_ANIMATION, !!on, 0x00 };
	return lp_write(priv, buf, 3);
}

static int set_power_saving_mode_cmd(struct gowin_priv *priv, int on)
{
	u8 buf[3] = { CMD_SET_POWER_SAVING, !!on, 0x00 };
	return lp_write(priv, buf, 3);
}

static int uart_send_data_app_cmd(struct gowin_priv *priv,
				  const u8 *data, int len)
{
	u8 buf[4] = { CMD_UART_SEND_DATA, len & 0xff, (len >> 8) & 0xff, 0 };
	int ret;

	ret = lp_write(priv, buf, 4);
	if (ret)
		return ret;

	return lp_write(priv, data, len);
}

static int uart_receive_data(struct gowin_priv *priv,
			     u8 *rx_data, int *rx_len)
{
	u8 cmd[3] = { CMD_UART_RECV_DATA, 0, 0 };
	u8 header[4];
	struct spi_transfer t[3];
	struct spi_message m;

	memset(t, 0, sizeof(t));
	t[0].tx_buf = cmd;
	t[0].len = 3;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0)
	t[1].delay.value = 100;
	t[1].delay.unit = SPI_DELAY_UNIT_USECS;
#else
	t[1].delay_usecs = 100;
#endif
	t[1].len = 0;
	t[2].rx_buf = header;
	t[2].len = 4;
	spi_message_init_with_transfers(&m, t, 3);

	if (spi_sync(priv->spi, &m))
		return -EIO;

	*rx_len = header[0] | (header[1] << 8);
	if (*rx_len > GOWIN_TTY_BUF_SIZE)
		*rx_len = GOWIN_TTY_BUF_SIZE;
	if (*rx_len == 0)
		return 0;

	{
		struct spi_transfer r = {
			.rx_buf = rx_data,
			.len = *rx_len,
		};
		spi_message_init_with_transfers(&m, &r, 1);
		return spi_sync(priv->spi, &m);
	}
}

__maybe_unused static int uart_flush_data_app_cmd(struct gowin_priv *priv)
{
	u8 buf[3] = { CMD_UART_FLUSH_DATA, 0, 0 };
	return lp_write(priv, buf, 3);
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

static void lp_gpio_set_value(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	struct gowin_priv *priv = gpiochip_get_data(chip);

	if (value)
		priv->gpio_cached |= BIT(offset);
	else
		priv->gpio_cached &= ~BIT(offset);

	set_gpio_app_cmd(priv);
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
		dev_err(dev, "gpiochip_add_data failed: %d\n", ret);

	return ret;
}

static void gpio_exit(struct gowin_priv *priv)
{
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

static int lp_uart_write_room(struct tty_struct *tty)
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
		schedule_work(&priv->uart_rx_work);

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
		dev_err(dev, "tty_register_driver failed: %d\n", ret);
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

	dev_info(dev, "uart ttyLP0 initialized\n");
	return 0;
}

static void uart_exit(struct gowin_priv *priv)
{
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

	regs.touch_state = status[1];
	regs.touch_mask = status[2];
	regs.dbg_val[0] = status[3] | (status[4] << 8);
	regs.dbg_val[2] = status[5] | (status[6] << 8);
	regs.dbg_val[4] = status[7] | (status[8] << 8);
	regs.metric[0] = status[9];
	regs.metric[2] = status[10];
	regs.metric[4] = status[11];

	if (regs.touch_mask) {
		input_event(priv->touch_dev, EV_KEY, KEY_BACK, 1);
		input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);
		input_event(priv->touch_dev, EV_KEY, KEY_BACK, 0);
		input_event(priv->touch_dev, EV_SYN, SYN_REPORT, 0);
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
	if (priv->touch_irq_number)
		devm_free_irq(priv->dev, priv->touch_irq_number, priv);
	if (priv->touch_dev)
		input_unregister_device(priv->touch_dev);
	if (priv->touch_wq) {
		flush_work(&priv->touch_work);
		destroy_workqueue(priv->touch_wq);
	}
}

__maybe_unused static void reset_touch_state(struct gowin_priv *priv)
{
	priv->fb_open_count = 0;
}

/* ------------------------------------------------------------------ */
/* Framebuffer                                                        */
/* ------------------------------------------------------------------ */

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
	return 0;
}

static int lp_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->bits_per_pixel = 8;
	var->grayscale = 0;
	var->red.offset = 0;
	var->red.length = 8;
	var->green.offset = 0;
	var->green.length = 8;
	var->blue.offset = 0;
	var->blue.length = 8;
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
	return 0;
}

static int lp_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	return 0;
}

static int lp_fb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	return remap_vmalloc_range(vma, fbi->screen_buffer, vma->vm_pgoff);
}

static int lp_fb_sync(struct fb_info *fbi)
{
	struct gowin_priv *priv = (struct gowin_priv *)fbi->par;
	u8 *buf;
	int len;
	int ret;

	len = priv->width * priv->height;
	if (len > GOWIN_FB_WIDTH_MAX * GOWIN_FB_HEIGHT_MAX)
		return -EINVAL;

	buf = kmalloc(3 + len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = CMD_WRITE;
	buf[1] = len & 0xff;
	buf[2] = (len >> 8) & 0xff;
	memcpy(buf + 3, fbi->screen_buffer, len);

	ret = lp_write(priv, buf, 3 + len);
	kfree(buf);

	if (ret == 0)
		show_pic_app_cmd(priv);

	return ret;
}

static struct fb_ops fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = lp_fb_open,
	.fb_release = lp_fb_release,
	.fb_check_var = lp_fb_check_var,
	.fb_set_par = lp_fb_set_par,
	.fb_pan_display = lp_fb_pan_display,
	.fb_mmap = lp_fb_mmap,
	.fb_sync = lp_fb_sync,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

static int fb_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	struct fb_info *fbi;
	int fb_len;
	int ret;

	fbi = framebuffer_alloc(0, dev);
	if (!fbi)
		return -ENOMEM;

	priv->fbi = fbi;
	fbi->par = priv;

	fb_len = priv->width * priv->height;
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
	fbi->fix.visual = FB_VISUAL_PSEUDOCOLOR;
	fbi->fix.line_length = priv->width;
	fbi->fix.accel = FB_ACCEL_NONE;
	snprintf(fbi->fix.id, sizeof(fbi->fix.id), GOWIN_FB_NAME);

	fbi->var.xres = priv->width;
	fbi->var.yres = priv->height;
	fbi->var.xres_virtual = priv->width;
	fbi->var.yres_virtual = priv->height;
	fbi->var.bits_per_pixel = 8;
	fbi->var.activate = FB_ACTIVATE_NOW;
	fbi->var.red.offset = 0;
	fbi->var.red.length = 8;

	fbi->pseudo_palette = priv->pseudo_palette;
	fbi->flags = 0;
	fbi->fbops = &fb_ops;

	ret = register_framebuffer(fbi);
	if (ret) {
		dev_err(dev, "register_framebuffer failed: %d\n", ret);
		goto err_mem;
	}

	dev_info(dev, "fb registered: %dx%d\n", priv->width, priv->height);
	return 0;

err_mem:
	vfree(priv->fb_mem);
err_fb:
	framebuffer_release(fbi);
	return ret;
}

static void fb_exit(struct gowin_priv *priv)
{
	if (priv->fbi) {
		unregister_framebuffer(priv->fbi);
		vfree(priv->fb_mem);
		framebuffer_release(priv->fbi);
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

	priv->brightness = bl->props.brightness;
	set_brightness_app_cmd(priv, priv->brightness);

	return 0;
}

static const struct backlight_ops lp_bl_ops = {
	.get_brightness = lp_bl_get_brightness,
	.update_status = lp_bl_update_status,
};

static int backlight_init(struct gowin_priv *priv)
{
	struct device *dev = priv->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = 255,
		.brightness = 128,
	};

	priv->bl = backlight_device_register(GOWIN_DRV_NAME, dev, priv,
					     &lp_bl_ops, &props);
	if (IS_ERR(priv->bl))
		return PTR_ERR(priv->bl);

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

static void jtag_write(struct gowin_priv *priv, const u8 *data, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		int bit;
		for (bit = 7; bit >= 0; bit--) {
			gpiod_set_value(priv->jtag_tdi, (data[i] >> bit) & 1);
			gpiod_set_value(priv->jtag_tck, 0);
			ndelay(50);
			gpiod_set_value(priv->jtag_tck, 1);
			ndelay(50);
		}
	}
}

static void jtag_update_tms(struct gowin_priv *priv, u8 tms)
{
	int i;
	for (i = 7; i >= 0; i--) {
		gpiod_set_value(priv->jtag_tms, (tms >> i) & 1);
		gpiod_set_value(priv->jtag_tck, 0);
		ndelay(50);
		gpiod_set_value(priv->jtag_tck, 1);
		ndelay(50);
	}
}

static void jtag_tap_move(struct gowin_priv *priv, u8 state)
{
	switch (state) {
	case 0: jtag_update_tms(priv, 0x7f); break;
	case 1: jtag_update_tms(priv, 0x00); break;
	case 2: jtag_update_tms(priv, 0x03); break;
	case 3: jtag_update_tms(priv, 0x01); break;
	case 4: jtag_update_tms(priv, 0x07); break;
	default: break;
	}
}

static void jtag_write_inst(struct gowin_priv *priv, u8 inst)
{
	jtag_tap_move(priv, 3);
	jtag_write(priv, &inst, 1);
	jtag_tap_move(priv, 2);
}

static int jtag_read_code(struct gowin_priv *priv, u32 *code)
{
	u32 val = 0;
	int i, bit;

	jtag_tap_move(priv, 4);
	jtag_tap_move(priv, 1);

	for (i = 0; i < 32; i++) {
		gpiod_set_value(priv->jtag_tck, 0);
		ndelay(50);
		bit = gpiod_get_value(priv->jtag_tdo);
		val |= bit << i;
		gpiod_set_value(priv->jtag_tck, 1);
		ndelay(50);
	}

	jtag_tap_move(priv, 2);
	*code = val;
	return 0;
}

static int check_status_code(struct gowin_priv *priv)
{
	u32 idcode;
	int ret;

	ret = jtag_read_code(priv, &idcode);
	if (ret)
		return ret;

	if ((idcode & 0x0fffffff) != 0x0080181b)
		return -ENODEV;

	return 0;
}

__maybe_unused static int jtag_erase_flash(struct gowin_priv *priv)
{
	jtag_write_inst(priv, 0x05);
	usleep_range(1000, 2000);
	jtag_write_inst(priv, 0x04);
	usleep_range(1000, 2000);

	jtag_tap_move(priv, 4);
	usleep_range(1000, 2000);

	return 0;
}

static int jtag_prog_fpga(struct gowin_priv *priv, const struct firmware *fw)
{
	const u8 *data = fw->data;
	int len = fw->size;
	int offset = 0;

	jtag_tap_move(priv, 0);
	msleep(10);

	jtag_write_inst(priv, 0x06);
	msleep(1);

	while (len > 0) {
		int chunk = min(len, 256);
		jtag_write(priv, data + offset, chunk);
		offset += chunk;
		len -= chunk;
	}

	jtag_write_inst(priv, 0x02);
	msleep(100);

	if (check_status_code(priv))
		return -EIO;

	jtag_tap_move(priv, 4);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Firmware update                                                     */
/* ------------------------------------------------------------------ */

static void fpga_hw_reset(struct gowin_priv *priv)
{
	if (priv->reset_gpio) {
		gpiod_set_value(priv->reset_gpio, 0);
		msleep(10);
		gpiod_set_value(priv->reset_gpio, 1);
		msleep(100);
	}
}

static int select_jtag_mode(struct gowin_priv *priv)
{
	int ret;

	if (priv->pinctrl_jtag) {
		ret = pinctrl_select_state(priv->pinctrl, priv->pinctrl_jtag);
		if (ret)
			return ret;
	}

	if (priv->jtag_sel)
		gpiod_set_value(priv->jtag_sel, 0);

	priv->jtag_mode = true;
	return 0;
}

static int select_spi_mode(struct gowin_priv *priv)
{
	if (priv->jtag_sel)
		gpiod_set_value(priv->jtag_sel, 1);

	if (priv->pinctrl_default) {
		return pinctrl_select_state(priv->pinctrl, priv->pinctrl_default);
	}

	priv->jtag_mode = false;
	return 0;
}

static int lp_update_firmware(struct gowin_priv *priv,
			      const struct firmware *fw)
{
	int ret;

	ret = select_jtag_mode(priv);
	if (ret)
		return ret;

	fpga_hw_reset(priv);
	ret = jtag_prog_fpga(priv, fw);

	select_spi_mode(priv);
	return ret;
}

static int lp_handle_firmware_update(struct gowin_priv *priv)
{
	const char *fw_name;
	const struct firmware *fw;
	int ret;

	if (of_device_is_compatible(priv->dev->of_node, "ya,led_panel")) {
		fw_name = "yandex_led_panel.bin";
	} else {
		fw_name = "yandex_led_screen_fpga.bin";
	}

	ret = request_firmware(&fw, fw_name, priv->dev);
	if (ret) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "fw not found: %s", fw_name);
		return ret;
	}

	snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
		 "flashing %s (%zu bytes)", fw_name, fw->size);

	ret = lp_update_firmware(priv, fw);
	release_firmware(fw);

	if (ret) {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "flash failed: %d", ret);
	} else {
		snprintf(priv->fw_upd_status, sizeof(priv->fw_upd_status),
			 "ok: %s programmed", fw_name);
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
		"show_debug_info      - touch debug registers\n"
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

	if (sysfs_streq(buf, "jtag"))
		ret = select_jtag_mode(priv);
	else
		ret = select_spi_mode(priv);

	return ret ? ret : len;
}

static ssize_t frame_queue_overflows_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
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
	return sysfs_emit(buf, "touch: state=%d mask=%d fqc=%u\n",
			  0, 0, priv->frame_queue_overflows);
}

static ssize_t frames_in_queue_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", priv->frames_in_queue);
}

static ssize_t press_key_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct gowin_priv *priv = dev_get_drvdata(dev);
	unsigned int key;
	int ret;

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
	int val = priv->jtag_reconfig ? gpiod_get_value(priv->jtag_reconfig) : 0;
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
		gpiod_set_value(priv->jtag_reconfig, !!val);

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
	u32 idcode = 0, usercode = 0;

	return sysfs_emit(buf, "ID=0x%08x USER=0x%08x STATUS=0\n",
			  idcode, usercode);
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
	{ "led_panel" },
	{ "led_screen" },
	{ }
};

MODULE_DEVICE_TABLE(spi, led_device_id);

static const struct of_device_id led_device_of_match[] = {
	{ .compatible = "ya,led_panel", .data = &rev2res_panel },
	{ .compatible = "ya,led_screen", .data = &rev2res_screen },
	{ }
};

MODULE_DEVICE_TABLE(of, led_device_of_match);

static int probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct of_device_id *match;
	const struct gowin_resolution *res;
	struct gowin_priv *priv;
	int ret;

	match = of_match_node(led_device_of_match, dev->of_node);
	if (!match || !match->data)
		return -EINVAL;

	res = match->data;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	priv->dev = dev;
	priv->width = res->width;
	priv->height = res->height;
	mutex_init(&priv->lock);
	spi_set_drvdata(spi, priv);

	/* GPIOs */
	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	priv->irq_gpio = devm_gpiod_get_optional(dev, "irq", GPIOD_IN);
	if (IS_ERR(priv->irq_gpio))
		return PTR_ERR(priv->irq_gpio);

	priv->jtag_tck = devm_gpiod_get_optional(dev, "jtag_tck", GPIOD_OUT_LOW);
	if (IS_ERR(priv->jtag_tck))
		return PTR_ERR(priv->jtag_tck);

	priv->jtag_tdo = devm_gpiod_get_optional(dev, "jtag_tdo", GPIOD_IN);
	if (IS_ERR(priv->jtag_tdo))
		return PTR_ERR(priv->jtag_tdo);

	priv->jtag_tdi = devm_gpiod_get_optional(dev, "jtag_tdi", GPIOD_OUT_LOW);
	if (IS_ERR(priv->jtag_tdi))
		return PTR_ERR(priv->jtag_tdi);

	priv->jtag_tms = devm_gpiod_get_optional(dev, "jtag_tms", GPIOD_OUT_LOW);
	if (IS_ERR(priv->jtag_tms))
		return PTR_ERR(priv->jtag_tms);

	priv->jtag_sel = devm_gpiod_get_optional(dev, "jtag_sel", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->jtag_sel))
		return PTR_ERR(priv->jtag_sel);

	priv->jtag_reconfig = devm_gpiod_get_optional(dev, "jtag_reconfig",
						       GPIOD_OUT_LOW);
	if (IS_ERR(priv->jtag_reconfig))
		return PTR_ERR(priv->jtag_reconfig);

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

	/* Select SPI mode by default */
	select_spi_mode(priv);

	/* Initialize subsystems */
	ret = gpio_init(priv);
	if (ret)
		return ret;

	ret = fb_init(priv);
	if (ret)
		goto err_gpio;

	ret = backlight_init(priv);
	if (ret)
		goto err_fb;

	ret = uart_init(priv);
	if (ret)
		goto err_bl;

	ret = init_touch(priv);
	if (ret)
		goto err_uart;

	ret = sysfs_create_groups(&dev->kobj, lp_attr_groups);
	if (ret)
		goto err_touch;

	/* Firmware update */
	lp_handle_firmware_update(priv);

	dev_info(dev, "led panel probed: %dx%d\n", priv->width, priv->height);
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

	sysfs_remove_groups(&priv->dev->kobj, lp_attr_groups);
	deinit_touch(priv);
	uart_exit(priv);
	backlight_exit(priv);
	fb_exit(priv);
	gpio_exit(priv);

	dev_info(priv->dev, "led panel removed\n");
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
