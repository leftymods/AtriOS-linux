// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/rotary-volume.c
 *
 * GPIO rotary volume control driver for Yandex Station Max / Station 2.
 * Reconstructed from rotary-volume.ko (4.9.113) and ported to Mainline Linux 7.0.
 *
 * Copyright (C) Yandex Inc.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sysfs.h>

#define DRV_NAME	"rotary-volume"
#define EXPECTED_GPIOS	2

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose quadrature transition and event logging");

struct rotary_volume {
	struct device *dev;
	struct gpio_descs *gpios;
	struct input_dev *input;
	struct work_struct work;
	struct timer_list poll_timer;
	bool use_irq;
	unsigned int scan_period_ms;
	unsigned int prev_state;
	u32 key_codes[EXPECTED_GPIOS];
	unsigned long cw_events;
	unsigned long ccw_events;
	unsigned long irq_count;
};

static unsigned int rotary_read_state_atomic(struct rotary_volume *rv,
					     int *out_v0, int *out_v1)
{
	int v0 = gpiod_get_value(rv->gpios->desc[0]);
	int v1 = gpiod_get_value(rv->gpios->desc[1]);

	if (v0 < 0)
		v0 = 1;
	if (v1 < 0)
		v1 = 1;

	if (out_v0)
		*out_v0 = v0;
	if (out_v1)
		*out_v1 = v1;

	return ((v0 & 1) << 1) | (v1 & 1);
}

static unsigned int rotary_read_state_cansleep(struct rotary_volume *rv,
					       int *out_v0, int *out_v1)
{
	int v0 = gpiod_get_value_cansleep(rv->gpios->desc[0]);
	int v1 = gpiod_get_value_cansleep(rv->gpios->desc[1]);

	if (v0 < 0)
		v0 = 1;
	if (v1 < 0)
		v1 = 1;

	if (out_v0)
		*out_v0 = v0;
	if (out_v1)
		*out_v1 = v1;

	return ((v0 & 1) << 1) | (v1 & 1);
}

static void rotary_volume_report_event(struct rotary_volume *rv, u32 key,
				       const char *dir)
{
	if (key == rv->key_codes[0])
		rv->cw_events++;
	else
		rv->ccw_events++;

	input_report_key(rv->input, key, 1);
	input_report_key(rv->input, key, 0);
	input_sync(rv->input);

	/* Also emit KEY_PAGEUP/KEY_PAGEDOWN or KEY_VOLUMEUP/KEY_VOLUMEDOWN alias if different */
	if (key == KEY_VOLUMEUP) {
		input_report_key(rv->input, KEY_PAGEUP, 1);
		input_report_key(rv->input, KEY_PAGEUP, 0);
		input_sync(rv->input);
	} else if (key == KEY_VOLUMEDOWN) {
		input_report_key(rv->input, KEY_PAGEDOWN, 1);
		input_report_key(rv->input, KEY_PAGEDOWN, 0);
		input_sync(rv->input);
	}

	if (debug)
		dev_info(rv->dev, "[ROTARY] Turn %s -> key=%u (cw=%lu, ccw=%lu)\n",
			 dir, key, rv->cw_events, rv->ccw_events);
}

static void rotary_volume_work(struct work_struct *work)
{
	struct rotary_volume *rv = container_of(work, struct rotary_volume, work);
	int v0 = 0, v1 = 0;
	unsigned int state = rotary_read_state_cansleep(rv, &v0, &v1);

	if (debug)
		dev_info(rv->dev, "[ROTARY-IRQ] state=%u (gpio0=%d, gpio1=%d, prev=%u)\n",
			 state, v0, v1, rv->prev_state);

	switch (state & 0x3) {
	case 1:
		rotary_volume_report_event(rv, rv->key_codes[0], "CW");
		break;
	case 2:
		rotary_volume_report_event(rv, rv->key_codes[1], "CCW");
		break;
	default:
		break;
	}
	rv->prev_state = state;
}

static irqreturn_t rotary_volume_irq(int irq, void *dev_id)
{
	struct rotary_volume *rv = dev_id;

	rv->irq_count++;
	queue_work(system_dfl_wq, &rv->work);
	return IRQ_HANDLED;
}

static void rotary_volume_timer(struct timer_list *t)
{
	struct rotary_volume *rv = container_of(t, struct rotary_volume, poll_timer);
	int v0 = 0, v1 = 0;
	unsigned int state = rotary_read_state_atomic(rv, &v0, &v1);

	if (state != rv->prev_state) {
		if (debug)
			dev_info(rv->dev,
				 "[ROTARY-POLL] transition %u -> %u (gpio0=%d, gpio1=%d)\n",
				 rv->prev_state, state, v0, v1);
		/*
		 * Rotary encoder quadrature transition:
		 * At rest with pull-ups, both lines are HIGH (state 3 = 11b).
		 * CW turn drops line 0 first (transition 3 -> 1).
		 * CCW turn drops line 1 first (transition 3 -> 2).
		 */
		if (rv->prev_state == 3) {
			if (state == 1)
				rotary_volume_report_event(rv, rv->key_codes[0], "CW");
			else if (state == 2)
				rotary_volume_report_event(rv, rv->key_codes[1], "CCW");
		}
		rv->prev_state = state;
	}

	mod_timer(&rv->poll_timer, jiffies + msecs_to_jiffies(rv->scan_period_ms));
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct rotary_volume *rv = dev_get_drvdata(dev);
	int v0 = 0, v1 = 0;
	unsigned int state = rotary_read_state_cansleep(rv, &v0, &v1);

	return scnprintf(buf, PAGE_SIZE,
			 "mode=%s\nscan_period_ms=%u\nstate=%u (gpio0=%d[hw:%d] gpio1=%d[hw:%d])\n"
			 "key_codes=%u,%u\ncw_events=%lu\nccw_events=%lu\nirq_count=%lu\ndebug=%d\n",
			 rv->use_irq ? "irq" : "polling",
			 rv->scan_period_ms, state,
			 v0, desc_to_gpio(rv->gpios->desc[0]),
			 v1, desc_to_gpio(rv->gpios->desc[1]),
			 rv->key_codes[0], rv->key_codes[1],
			 rv->cw_events, rv->ccw_events, rv->irq_count, debug);
}
static DEVICE_ATTR_RO(status);

static struct attribute *rotary_volume_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group rotary_volume_attr_group = {
	.attrs = rotary_volume_attrs,
};

static int rotary_volume_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rotary_volume *rv;
	unsigned int i;
	int count, ret, v0 = 0, v1 = 0;

	dev_info(dev, "[ROTARY] Probing rotary-volume driver...\n");

	rv = devm_kzalloc(dev, sizeof(*rv), GFP_KERNEL);
	if (!rv)
		return -ENOMEM;

	rv->dev = dev;

	rv->gpios = devm_gpiod_get_array(dev, NULL, GPIOD_IN);
	if (IS_ERR(rv->gpios)) {
		dev_err(dev, "[ROTARY] ERROR: unable to get gpios: %ld\n",
			PTR_ERR(rv->gpios));
		return PTR_ERR(rv->gpios);
	}

	if (rv->gpios->ndescs != EXPECTED_GPIOS) {
		dev_err(dev, "[ROTARY] ERROR: expected %d gpios, got %u\n",
			EXPECTED_GPIOS, rv->gpios->ndescs);
		return -EINVAL;
	}

	count = of_property_count_u32_elems(dev->of_node, "key-codes");
	if (count != (int)rv->gpios->ndescs) {
		dev_err(dev, "[ROTARY] ERROR: unexpected key-codes count %d (expected %u)\n",
			count, rv->gpios->ndescs);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev->of_node, "key-codes",
					 rv->key_codes, rv->gpios->ndescs);
	if (ret < 0) {
		dev_err(dev, "[ROTARY] ERROR: failed to read key-codes: %d\n", ret);
		return -EINVAL;
	}

	rv->input = devm_input_allocate_device(dev);
	if (!rv->input)
		return -ENOMEM;

	rv->input->name = pdev->name;
	rv->input->id.bustype = BUS_HOST;

	input_set_capability(rv->input, EV_KEY, rv->key_codes[0]);
	input_set_capability(rv->input, EV_KEY, rv->key_codes[1]);
	input_set_capability(rv->input, EV_KEY, KEY_PAGEUP);
	input_set_capability(rv->input, EV_KEY, KEY_PAGEDOWN);
	input_set_capability(rv->input, EV_KEY, KEY_VOLUMEUP);
	input_set_capability(rv->input, EV_KEY, KEY_VOLUMEDOWN);

	ret = input_register_device(rv->input);
	if (ret) {
		dev_err(dev, "[ROTARY] ERROR: failed to register input device: %d\n", ret);
		return ret;
	}

	INIT_WORK(&rv->work, rotary_volume_work);
	platform_set_drvdata(pdev, rv);

	rv->scan_period_ms = 4;
	rv->prev_state = rotary_read_state_cansleep(rv, &v0, &v1);
	dev_info(dev, "[ROTARY] GPIOs claimed: gpio0=hw:%d (val=%d), gpio1=hw:%d (val=%d), initial_state=%u, key_codes=[%u, %u]\n",
		 desc_to_gpio(rv->gpios->desc[0]), v0,
		 desc_to_gpio(rv->gpios->desc[1]), v1,
		 rv->prev_state, rv->key_codes[0], rv->key_codes[1]);

	timer_setup(&rv->poll_timer, rotary_volume_timer, 0);

	rv->use_irq = true;
	for (i = 0; i < rv->gpios->ndescs; i++) {
		int irq = gpiod_to_irq(rv->gpios->desc[i]);
		if (irq < 0) {
			dev_info(dev, "[ROTARY] gpiod_to_irq on gpio[%u] (hw:%d) returned %d -> using timer polling\n",
				 i, desc_to_gpio(rv->gpios->desc[i]), irq);
			rv->use_irq = false;
			break;
		}
		ret = devm_request_threaded_irq(dev, irq, rotary_volume_irq,
						NULL, IRQF_TRIGGER_FALLING,
						DRV_NAME, rv);
		if (ret) {
			dev_info(dev, "[ROTARY] request_irq(%d) failed (%d) -> using timer polling\n",
				 irq, ret);
			rv->use_irq = false;
			break;
		}
	}

	ret = sysfs_create_group(&dev->kobj, &rotary_volume_attr_group);
	if (ret)
		dev_warn(dev, "[ROTARY] failed to create sysfs group: %d\n", ret);

	if (!rv->use_irq) {
		dev_info(dev, "[ROTARY] Probed OK in timer polling mode (%ums)\n",
			 rv->scan_period_ms);
		mod_timer(&rv->poll_timer, jiffies + msecs_to_jiffies(rv->scan_period_ms));
	} else {
		dev_info(dev, "[ROTARY] Probed OK in hardware IRQ mode\n");
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void rotary_volume_remove(struct platform_device *pdev)
#else
static int rotary_volume_remove(struct platform_device *pdev)
#endif
{
	struct rotary_volume *rv = platform_get_drvdata(pdev);

	if (rv) {
		sysfs_remove_group(&pdev->dev.kobj, &rotary_volume_attr_group);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
		timer_delete_sync(&rv->poll_timer);
#else
		del_timer_sync(&rv->poll_timer);
#endif
		cancel_work_sync(&rv->work);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id rotary_volume_of_match[] = {
	{ .compatible = "rotary-volume" },
	{ .compatible = "yandex,rotary-volume" },
	{ .compatible = "leftymods,rotary-volume" },
	{ }
};
MODULE_DEVICE_TABLE(of, rotary_volume_of_match);

static struct platform_driver rotary_volume_driver = {
	.probe = rotary_volume_probe,
	.remove = rotary_volume_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = rotary_volume_of_match,
	},
};
module_platform_driver(rotary_volume_driver);

MODULE_AUTHOR("Yandex Inc.");
MODULE_DESCRIPTION("GPIO rotary volume control driver");
MODULE_LICENSE("GPL");
