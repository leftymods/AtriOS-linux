// SPDX-License-Identifier: GPL-2.0-only
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define XRES		24
#define YRES		24
#define BPP		8
#define FB_SIZE		(XRES * YRES * BPP / 8)

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose LED ring framebuffer sync logging");

struct led_ring_node {
	struct list_head	list;
	struct led_classdev	*led;
	int			pixel_offset;
};

static struct fb_info *ring_fb;
static struct backlight_device *ring_bl;
static struct led_ring_priv {
	int			brightness;
	unsigned int		attached_leds;
	unsigned long		sync_count;
	struct mutex		list_lock;
	struct list_head	led_list;
} ring_priv;

static int ring_fb_sync(struct fb_info *info);

/* ---- Backlight ---- */

static int bl_get_brightness(struct backlight_device *bd)
{
	return ring_priv.brightness;
}

static int bl_update_status(struct backlight_device *bd)
{
	ring_priv.brightness = clamp_t(int, bd->props.brightness, 0, 255);
	pr_info("[LED_RING] set backlight brightness=%d (attached_leds=%u)\n",
		ring_priv.brightness, ring_priv.attached_leds);
	if (ring_fb)
		ring_fb_sync(ring_fb);
	return 0;
}

static const struct backlight_ops ring_bl_ops = {
	.get_brightness	= bl_get_brightness,
	.update_status	= bl_update_status,
};

/* ---- Framebuffer ---- */

static int ring_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	return remap_vmalloc_range(vma, info->screen_buffer, vma->vm_pgoff);
}

static int ring_fb_sync(struct fb_info *info)
{
	struct led_ring_node *node;
	int scale = clamp_t(int, ring_priv.brightness, 0, 255);

	mutex_lock(&ring_priv.list_lock);
	ring_priv.sync_count++;
	if (debug && (ring_priv.sync_count % 25 == 1))
		pr_info("[LED_RING] fb_sync #%lu (attached_leds=%u, scale=%d/255, rgb0=[%u,%u,%u])\n",
			ring_priv.sync_count, ring_priv.attached_leds, scale,
			((u8 *)info->screen_buffer)[0],
			((u8 *)info->screen_buffer)[1],
			((u8 *)info->screen_buffer)[2]);

	list_for_each_entry(node, &ring_priv.led_list, list) {
		u8 pixel = ((u8 *)info->screen_buffer)[node->pixel_offset];
		int brightness = (pixel * scale) / 255;
		int ret;

		ret = led_set_brightness_sync(node->led, brightness);
		if (ret == -ENOTSUPP)
			led_set_brightness(node->led, brightness);
	}
	mutex_unlock(&ring_priv.list_lock);
	return 0;
}

static int ring_fb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	return ring_fb_sync(info);
}

static int ring_fb_release(struct fb_info *info, int user)
{
	return ring_fb_sync(info);
}

static void ring_fb_destroy(struct fb_info *info)
{
	if (info->screen_buffer)
		vfree(info->screen_buffer);
}

static int ring_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (var->xres != info->var.xres || var->yres != info->var.yres ||
	    var->xres_virtual != info->var.xres_virtual ||
	    var->yres_virtual != info->var.yres_virtual ||
	    var->bits_per_pixel != info->var.bits_per_pixel)
		return -EINVAL;
	return 0;
}

static ssize_t ring_fb_write(struct fb_info *info, const char __user *buf,
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
	ring_fb_sync(info);
	return count;
}

static int ring_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	return 0;
}

static struct fb_ops ring_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ring_fb_check_var,
	.fb_destroy	= ring_fb_destroy,
	.fb_mmap	= ring_fb_mmap,
	.fb_write	= ring_fb_write,
	.fb_pan_display	= ring_fb_pan_display,
	.fb_release	= ring_fb_release,
	.fb_sync	= ring_fb_sync,
	.fb_cursor	= ring_fb_cursor,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
};

/* ---- LED trigger ---- */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
static int led_ring_trigger_activate(struct led_classdev *led)
#else
static void led_ring_trigger_activate(struct led_classdev *led)
#endif
{
	struct led_ring_node *node;
	const char *name;
	const char *p;
	int color_idx;
	int led_num;
	long long pos = 0;

	if (led->max_brightness != 255) {
		dev_err(led->dev,
			"[LED_RING] cannot activate led_ring_fb trigger for led with max_brightness != %d\n",
			255);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		return -EINVAL;
#else
		return;
#endif
	}

	name = led->name ? led->name : dev_name(led->dev);
	if (!name)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		return -EINVAL;
#else
		return;
#endif

	if (strstr(name, "red") || strstr(name, "_r"))
		color_idx = 2;
	else if (strstr(name, "green") || strstr(name, "_g"))
		color_idx = 1;
	else if (strstr(name, "blue") || strstr(name, "_b"))
		color_idx = 0;
	else {
		dev_err(led->dev, "[LED_RING] failed to parse led color in '%s'\n", name);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		return -EINVAL;
#else
		return;
#endif
	}

	p = strstr(name, "ring");
	if (p) {
		p += 4;
	} else {
		p = strrchr(name, ':');
		if (p)
			p++;
		else
			p = name;
		while (*p && (*p < '0' || *p > '9'))
			p++;
	}

	if (!p || kstrtoll(p, 10, &pos) || pos < 1 || pos > 24) {
		/* Also handle names like led1_r .. led24_b where number has _r/_g/_b suffix */
		char num_buf[8] = {0};
		int idx = 0;

		while (p && *p >= '0' && *p <= '9' && idx < 6)
			num_buf[idx++] = *p++;
		if (idx == 0 || kstrtoll(num_buf, 10, &pos) || pos < 1 || pos > 24) {
			dev_err(led->dev, "[LED_RING] failed to parse led position in '%s'\n", name);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
			return -EINVAL;
#else
			return;
#endif
		}
	}
	led_num = color_idx + (int)(pos - 1) * 3;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		dev_err(led->dev, "[LED_RING] failed to allocate led_node\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		return -ENOMEM;
#else
		return;
#endif
	}

	node->led = led;
	node->pixel_offset = led_num;

	mutex_lock(&ring_priv.list_lock);
	list_add_tail(&node->list, &ring_priv.led_list);
	ring_priv.attached_leds++;
	if (debug || ring_priv.attached_leds == 72)
		pr_info("[LED_RING] bound LED '%s' -> pixel_offset=%d (total_attached=%u/72)\n",
			name, led_num, ring_priv.attached_leds);
	mutex_unlock(&ring_priv.list_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	return 0;
#endif
}

static void led_ring_trigger_deactivate(struct led_classdev *led)
{
	struct led_ring_node *node, *tmp;

	mutex_lock(&ring_priv.list_lock);
	list_for_each_entry_safe(node, tmp, &ring_priv.led_list, list) {
		if (node->led == led) {
			list_del(&node->list);
			if (ring_priv.attached_leds > 0)
				ring_priv.attached_leds--;
			kfree(node);
			break;
		}
	}
	mutex_unlock(&ring_priv.list_lock);
}

static struct led_trigger led_ring_trigger = {
	.name		= "led_ring_fb",
	.activate	= led_ring_trigger_activate,
	.deactivate	= led_ring_trigger_deactivate,
};

/* ---- Init / Exit ---- */

static int __init led_ring_init(void)
{
	struct fb_info *info;
	struct backlight_properties bl_props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = 255,
		.brightness = 255,
	};
	int ret;

	pr_info("[LED_RING] Initializing quasar_led_ring driver...\n");

	mutex_init(&ring_priv.list_lock);
	INIT_LIST_HEAD(&ring_priv.led_list);
	ring_priv.brightness = 255;

	info = framebuffer_alloc(0, NULL);
	if (!info) {
		pr_err("[LED_RING] ERROR: failed to allocate framebuffer\n");
		return -ENOMEM;
	}

	info->screen_buffer = vzalloc(FB_SIZE);
	if (!info->screen_buffer) {
		pr_err("[LED_RING] ERROR: failed to allocate video memory\n");
		ret = -ENOMEM;
		goto err_release;
	}
	info->screen_size = FB_SIZE;

	strscpy(info->fix.id, "quasar-led-ring", sizeof(info->fix.id));
	info->fix.smem_start = (unsigned long)info->screen_buffer;
	info->fix.smem_len = FB_SIZE;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = XRES;
	info->fix.accel = FB_ACCEL_NONE;

	info->var.xres = XRES;
	info->var.yres = YRES;
	info->var.xres_virtual = XRES;
	info->var.yres_virtual = YRES;
	info->var.bits_per_pixel = BPP;
	info->var.red.offset = 0;
	info->var.red.length = 8;
	info->var.green.offset = 0;
	info->var.green.length = 8;
	info->var.blue.offset = 0;
	info->var.blue.length = 8;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	info->fbops = &ring_fb_ops;

	ret = register_framebuffer(info);
	if (ret < 0) {
		pr_err("[LED_RING] ERROR: failed to register framebuffer: %d\n", ret);
		goto err_free_video;
	}

	ring_fb = info;
	pr_info("[LED_RING] Framebuffer registered as fb%d ('%s')\n",
		info->node, info->fix.id);

	ring_bl = backlight_device_register("quasar_led_ring", NULL,
					    &ring_priv, &ring_bl_ops,
					    &bl_props);
	if (IS_ERR(ring_bl)) {
		ret = PTR_ERR(ring_bl);
		pr_err("[LED_RING] ERROR: failed to register backlight: %d\n", ret);
		goto err_unreg_fb;
	}
	pr_info("[LED_RING] Backlight 'quasar_led_ring' registered\n");

	ret = led_trigger_register(&led_ring_trigger);
	if (ret) {
		pr_err("[LED_RING] ERROR: failed to register led trigger: %d\n", ret);
		goto err_unreg_bl;
	}
	pr_info("[LED_RING] Trigger 'led_ring_fb' registered OK\n");

	return 0;

err_unreg_bl:
	backlight_device_unregister(ring_bl);
err_unreg_fb:
	unregister_framebuffer(info);
err_free_video:
	vfree(info->screen_buffer);
err_release:
	framebuffer_release(info);
	return ret;
}

static void __exit led_ring_exit(void)
{
	struct led_ring_node *node, *tmp;

	led_trigger_unregister(&led_ring_trigger);
	backlight_device_unregister(ring_bl);

	mutex_lock(&ring_priv.list_lock);
	list_for_each_entry_safe(node, tmp, &ring_priv.led_list, list) {
		list_del(&node->list);
		kfree(node);
	}
	ring_priv.attached_leds = 0;
	mutex_unlock(&ring_priv.list_lock);

	unregister_framebuffer(ring_fb);
	framebuffer_release(ring_fb);
}

module_init(led_ring_init);
module_exit(led_ring_exit);

MODULE_DESCRIPTION("Quasar LED ring framebuffer and trigger driver");
MODULE_AUTHOR("leftymods");
MODULE_LICENSE("GPL v2");
