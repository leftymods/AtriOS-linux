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

#define DRV_NAME	"rotary-volume"
#define EXPECTED_GPIOS	2

struct rotary_volume {
	struct gpio_descs *gpios;
	struct input_dev *input;
	struct work_struct work;
	u32 key_codes[EXPECTED_GPIOS];
};

static void rotary_volume_report_event(struct rotary_volume *rv, u32 key)
{
	input_report_key(rv->input, key, 1);
	input_report_key(rv->input, key, 0);
	input_sync(rv->input);
}

static void rotary_volume_work(struct work_struct *work)
{
	struct rotary_volume *rv = container_of(work, struct rotary_volume, work);
	unsigned int i;
	unsigned int state = 0;

	for (i = 0; i < rv->gpios->ndescs; i++) {
		int val = gpiod_get_value_cansleep(rv->gpios->desc[i]);

		state = (state << 1) | (val & 1);
	}

	switch (state & 0x3) {
	case 1:
		rotary_volume_report_event(rv, rv->key_codes[0]);
		break;
	case 2:
		rotary_volume_report_event(rv, rv->key_codes[1]);
		break;
	default:
		break;
	}
}

static irqreturn_t rotary_volume_irq(int irq, void *dev_id)
{
	struct rotary_volume *rv = dev_id;

	schedule_work(&rv->work);
	return IRQ_HANDLED;
}

static int rotary_volume_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rotary_volume *rv;
	unsigned int i;
	int count, ret;

	rv = devm_kzalloc(dev, sizeof(*rv), GFP_KERNEL);
	if (!rv)
		return -ENOMEM;

	rv->gpios = devm_gpiod_get_array(dev, NULL, GPIOD_IN);
	if (IS_ERR(rv->gpios)) {
		dev_err(dev, "unable to get gpios\n");
		return PTR_ERR(rv->gpios);
	}

	if (rv->gpios->ndescs != EXPECTED_GPIOS) {
		dev_err(dev, "exactly two gpios expected, got %u\n",
			rv->gpios->ndescs);
		return -EINVAL;
	}

	count = of_property_count_u32_elems(dev->of_node, "key-codes");
	if (count != (int)rv->gpios->ndescs) {
		dev_err(dev, "unexpected amount of key-codes, got: %d, expect: %d\n",
			count, rv->gpios->ndescs);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev->of_node, "key-codes",
					 rv->key_codes, rv->gpios->ndescs);
	if (ret < 0) {
		dev_err(dev, "failed to read key-codes\n");
		return -EINVAL;
	}

	rv->input = devm_input_allocate_device(dev);
	if (!rv->input)
		return -ENOMEM;

	rv->input->name = pdev->name;
	rv->input->id.bustype = BUS_HOST;

	input_set_capability(rv->input, EV_KEY, rv->key_codes[0]);
	input_set_capability(rv->input, EV_KEY, rv->key_codes[1]);

	ret = input_register_device(rv->input);
	if (ret) {
		dev_err(dev, "failed to register input device\n");
		return ret;
	}

	INIT_WORK(&rv->work, rotary_volume_work);
	platform_set_drvdata(pdev, rv);

	for (i = 0; i < rv->gpios->ndescs; i++) {
		int irq = gpiod_to_irq(rv->gpios->desc[i]);

		ret = devm_request_threaded_irq(dev, irq, rotary_volume_irq,
						NULL, IRQF_TRIGGER_FALLING,
						DRV_NAME, rv);
		if (ret) {
			dev_err(dev, "unable to request IRQ %d\n", irq);
			return ret;
		}
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

	if (rv)
		cancel_work_sync(&rv->work);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id rotary_volume_of_match[] = {
	{ .compatible = DRV_NAME },
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

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yandex Inc");
MODULE_DESCRIPTION("GPIO rotary volume control driver");
MODULE_ALIAS("platform:" DRV_NAME);
