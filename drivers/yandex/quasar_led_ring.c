// SPDX-License-Identifier: GPL-2.0-only
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

struct led_ring_node {
	struct list_head	list;
	struct led_classdev	*led;
	int			pixel_offset;
};

static struct fb_info *ring_fb;
static struct backlight_device *ring_bl;
static struct led_ring_priv {
	int			brightness;
	struct mutex		list_lock;
	struct list_head	led_list;
} ring_priv;

/* ---- Backlight ---- */

static int bl_get_brightness(struct backlight_device *bd)
{
	return ring_priv.brightness;
}

static int bl_update_status(struct backlight_device *bd)
{
	ring_priv.brightness = bd->props.brightness;
	pr_info("set led ring brightness %u\n", ring_priv.brightness);
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

	mutex_lock(&ring_priv.list_lock);
	list_for_each_entry(node, &ring_priv.led_list, list) {
		u8 pixel = ((u8 *)info->screen_buffer)[node->pixel_offset];
		int brightness = (pixel * info->var.xres) / info->var.yres;
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

static struct fb_ops ring_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= ring_fb_check_var,
	.fb_destroy	= ring_fb_destroy,
	.fb_mmap	= ring_fb_mmap,
	.fb_write	= ring_fb_write,
	.fb_pan_display	= ring_fb_pan_display,
	.fb_release	= ring_fb_release,
	.fb_sync	= ring_fb_sync,
};

/* ---- LED trigger ---- */

static void led_ring_trigger_activate(struct led_classdev *led)
{
	struct led_ring_node *node;
	const char *name;
	const char *p;
	int color_idx;
	int led_num;
	long long pos = 0;

	if (led->max_brightness != 255) {
		dev_err(led->dev,
			"cannot activate led_ring_fb trigger for led with max_brightness != %d\n",
			255);
		return;
	}

	name = led->name ? led->name : dev_name(led->dev);
	if (!name)
		return;

	if (strstr(name, "red"))
		color_idx = 0;
	else if (strstr(name, "green"))
		color_idx = 1;
	else if (strstr(name, "blue"))
		color_idx = 2;
	else {
		dev_err(led->dev, "failed to parse led color in '%s'\n", name);
		return;
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
		dev_err(led->dev, "failed to parse led position in '%s'\n", name);
		return;
	}
	led_num = color_idx + (int)(pos - 1) * 3;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		dev_err(led->dev, "failed to allocate led_node\n");
		return;
	}

	node->led = led;
	node->pixel_offset = led_num;

	mutex_lock(&ring_priv.list_lock);
	list_add_tail(&node->list, &ring_priv.led_list);
	mutex_unlock(&ring_priv.list_lock);
}

static void led_ring_trigger_deactivate(struct led_classdev *led)
{
	struct led_ring_node *node, *tmp;

	mutex_lock(&ring_priv.list_lock);
	list_for_each_entry_safe(node, tmp, &ring_priv.led_list, list) {
		if (node->led == led) {
			list_del(&node->list);
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

	mutex_init(&ring_priv.list_lock);
	INIT_LIST_HEAD(&ring_priv.led_list);
	ring_priv.brightness = 255;

	info = framebuffer_alloc(0, NULL);
	if (!info) {
		pr_err("led_ring_fb: failed to allocate framebuffer\n");
		return -ENOMEM;
	}

	info->screen_buffer = vzalloc(FB_SIZE);
	if (!info->screen_buffer) {
		pr_err("led_ring_fb: failed to allocate video memory\n");
		ret = -ENOMEM;
		goto err_release;
	}
	info->screen_size = FB_SIZE;

	strscpy(info->fix.id, "quasar-led-ring", sizeof(info->fix.id));
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
		pr_err("led_ring_fb: failed to register framebuffer: %d\n", ret);
		goto err_free_video;
	}

	ring_fb = info;
	pr_info("led_ring_fb: framebuffer registered\n");

	ring_bl = backlight_device_register("quasar_led_ring", NULL,
					    &ring_priv, &ring_bl_ops,
					    &bl_props);
	if (IS_ERR(ring_bl)) {
		ret = PTR_ERR(ring_bl);
		pr_err("led_ring_fb: failed to register backlight: %d\n", ret);
		goto err_unreg_fb;
	}
	pr_info("led_ring_fb: backlight registered\n");

	ret = led_trigger_register(&led_ring_trigger);
	if (ret) {
		pr_err("led_ring_fb: failed to register led trigger: %d\n", ret);
		goto err_unreg_bl;
	}

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
	pr_info("led_ring_fb: unregistering trigger\n");
	led_trigger_unregister(&led_ring_trigger);

	pr_info("led_ring_fb: unregistering framebuffer\n");
	backlight_device_unregister(ring_bl);
	unregister_framebuffer(ring_fb);
	if (ring_fb->screen_buffer)
		vfree(ring_fb->screen_buffer);
	framebuffer_release(ring_fb);
	ring_fb = NULL;
}

module_init(led_ring_init);
module_exit(led_ring_exit);

MODULE_DESCRIPTION("Quasar LED Ring (framebuffer + backlight + LED trigger)");
MODULE_AUTHOR("AtriOS Team");
MODULE_LICENSE("GPL v2");
