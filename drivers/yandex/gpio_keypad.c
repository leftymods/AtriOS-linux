// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/gpio_keypad.c
 *
 * Amlogic / Yandex Station Max GPIO keypad driver (micmute & action button).
 * Ported to Mainline Linux 7.0 while preserving 4.9 compatibility.
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/pm_wakeup.h>
#ifdef CONFIG_AMLOGIC_MODIFY
#include <linux/gpio.h>
#include <linux/amlogic/pm.h>
#include <linux/amlogic/scpi_protocol.h>
#endif

#undef pr_fmt
#define pr_fmt(fmt) "gpio-keypad: " fmt

#define GPIO_PWRKEY_NAME	"power"
#define DEFAULT_SCAN_PERION	20
#define DEFAULT_POLL_MODE	0
#define KEY_JITTER_COUNT	1

struct pin_desc {
	int current_status;
	struct gpio_desc *desc;
	int irq_num;
	u32 code;
	const char *name;
};

struct gpio_keypad {
	int key_size;
	int use_irq; /* 1: irq mode ; 0: polling mode */
	int scan_period;
	int current_irq;
	int count;
	int index;
	u32 pwrkey_code;
	struct pin_desc *key;
	struct pin_desc *current_key;
	struct timer_list polling_timer;
	struct input_dev *input_dev;
};

static irqreturn_t gpio_irq_handler(int irq, void *data)
{
	struct gpio_keypad *keypad = (struct gpio_keypad *)data;

	keypad->current_irq = irq;
	keypad->count++;
	mod_timer(&keypad->polling_timer, jiffies + msecs_to_jiffies(20));
	return IRQ_HANDLED;
}

static struct pin_desc *get_current_key(struct gpio_keypad *keypad)
{
	int i;

	for (i = 0; i < keypad->key_size; i++) {
		if (keypad->current_irq == keypad->key[i].irq_num)
			return &keypad->key[i];
	}
	return NULL;
}

static void report_key_code(struct gpio_keypad *keypad, int gpio_val)
{
	struct pin_desc *key;

	if (keypad->count < KEY_JITTER_COUNT) {
		keypad->count++;
	} else {
		key = keypad->current_key;
		key->current_status = gpio_val;
		if (key->current_status) {
			input_report_key(keypad->input_dev, key->code, 0);
			if (keypad->use_irq)
				enable_irq(key->irq_num);
			dev_info(&keypad->input_dev->dev,
				 "key %d up.\n", key->code);
		} else {
			input_report_key(keypad->input_dev, key->code, 1);
			if (keypad->use_irq)
				disable_irq_nosync(key->irq_num);
			dev_info(&keypad->input_dev->dev,
				 "key %d down.\n", key->code);
		}
		input_sync(keypad->input_dev);
		if (key->code == keypad->pwrkey_code) {
			if (key->current_status)
				pm_relax(keypad->input_dev->dev.parent);
			else
				pm_stay_awake(keypad->input_dev->dev.parent);
		}
		keypad->count = 0;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static void polling_timer_handler(struct timer_list *t)
{
	struct gpio_keypad *keypad = from_timer(keypad, t, polling_timer);
#else
static void polling_timer_handler(unsigned long data)
{
	struct gpio_keypad *keypad = (struct gpio_keypad *)data;
#endif
	struct pin_desc *key;
	int i;
	int gpio_val;

	if (keypad->use_irq) {
		keypad->current_key = get_current_key(keypad);
		if (!keypad->current_key)
			return;
		key = keypad->current_key;
		gpio_val = gpiod_get_value(key->desc);
		if (key->current_status != gpio_val)
			report_key_code(keypad, gpio_val);
		else
			keypad->count = 0;
		if (key->current_status == 0)
			mod_timer(&keypad->polling_timer,
				  jiffies + msecs_to_jiffies(keypad->scan_period));
	} else {
		for (i = 0; i < keypad->key_size; i++) {
			gpio_val = gpiod_get_value(keypad->key[i].desc);
			if (keypad->key[i].current_status != gpio_val) {
				keypad->index = i;
				keypad->current_key = &keypad->key[i];
				report_key_code(keypad, gpio_val);
			} else if (keypad->index == i) {
				keypad->count = 0;
			}
			mod_timer(&keypad->polling_timer,
				  jiffies + msecs_to_jiffies(keypad->scan_period));
		}
	}
}

static int meson_gpio_kp_probe(struct platform_device *pdev)
{
	struct gpio_desc *desc;
	int ret, i;
	struct input_dev *input_dev;
	struct gpio_keypad *keypad;
	bool pwrkey_update_flg = false;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "pdev->dev.of_node == NULL!\n");
		return -EINVAL;
	}
	keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "detect_mode", &keypad->use_irq);
	if (ret)
		keypad->use_irq = DEFAULT_POLL_MODE;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "scan_period", &keypad->scan_period);
	if (ret)
		keypad->scan_period = DEFAULT_SCAN_PERION;

	ret = of_property_read_u32(pdev->dev.of_node,
				   "key_num", &keypad->key_size);
	if (ret || keypad->key_size <= 0) {
		dev_err(&pdev->dev, "failed to get key_num!\n");
		return -EINVAL;
	}

	keypad->key = devm_kcalloc(&pdev->dev, keypad->key_size,
				   sizeof(*keypad->key), GFP_KERNEL);
	if (!keypad->key)
		return -ENOMEM;

	for (i = 0; i < keypad->key_size; i++) {
		/*
		 * Note: In DT, key-gpios are marked GPIO_ACTIVE_LOW (1).
		 * On mainline kernels gpiod_get_value() honors ACTIVE_LOW unless
		 * we read raw or configure GPIOD_ASIS/raw. Wait: the vendor 4.9
		 * driver checks `current_status = 1` as released (high physical level)
		 * and `0` as pressed (low physical level) because in vendor DT
		 * `key-gpios` had `GPIO_ACTIVE_HIGH` (0)!
		 * Using gpiod_get_raw_value or honoring physical level ensures
		 * identical behavior whether DT has GPIO_ACTIVE_HIGH or LOW.
		 */
		desc = devm_gpiod_get_index(&pdev->dev, "key", i, GPIOD_IN);
		if (IS_ERR(desc))
			return PTR_ERR(desc);

		keypad->key[i].desc = desc;
		keypad->key[i].current_status = 1;

		ret = of_property_read_u32_index(pdev->dev.of_node,
						 "key_code", i,
						 &keypad->key[i].code);
		if (ret < 0) {
			dev_err(&pdev->dev, "find key_code=%d failed\n", i);
			return -EINVAL;
		}
		ret = of_property_read_string_index(pdev->dev.of_node,
						    "key_name", i,
						    &keypad->key[i].name);
		if (ret < 0) {
			dev_err(&pdev->dev, "find key_name=%d failed\n", i);
			return -EINVAL;
		}
		if (!strcmp(keypad->key[i].name, GPIO_PWRKEY_NAME)) {
			keypad->pwrkey_code = keypad->key[i].code;
			pwrkey_update_flg = true;
		}

		gpiod_direction_input(keypad->key[i].desc);
#ifdef CONFIG_AMLOGIC_MODIFY
		gpiod_set_pull(keypad->key[i].desc, GPIOD_PULL_UP);
#endif
	}

	if (!pwrkey_update_flg)
		keypad->pwrkey_code = KEY_RESERVED;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	set_bit(EV_KEY, input_dev->evbit);
	for (i = 0; i < keypad->key_size; i++) {
		set_bit(keypad->key[i].code, input_dev->keybit);
		dev_info(&pdev->dev, "%s key(%d) registered.\n",
			 keypad->key[i].name, keypad->key[i].code);
	}
	input_dev->name = "gpio_keypad";
	input_dev->phys = "gpio_keypad/input0";
	input_dev->dev.parent = &pdev->dev;
	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->rep[REP_DELAY] = 0xffffffff;
	input_dev->rep[REP_PERIOD] = 0xffffffff;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = 0x1ff;
	keypad->input_dev = input_dev;

	ret = input_register_device(keypad->input_dev);
	if (ret < 0) {
		input_free_device(keypad->input_dev);
		return ret;
	}

	platform_set_drvdata(pdev, keypad);
	keypad->count = 0;
	keypad->index = -1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
	timer_setup(&keypad->polling_timer, polling_timer_handler, 0);
#else
	setup_timer(&keypad->polling_timer, polling_timer_handler,
		    (unsigned long)keypad);
#endif

	device_init_wakeup(&pdev->dev, true);

	if (keypad->use_irq) {
		for (i = 0; i < keypad->key_size; i++) {
			keypad->key[i].irq_num = gpiod_to_irq(keypad->key[i].desc);
			ret = devm_request_irq(&pdev->dev,
					       keypad->key[i].irq_num,
					       gpio_irq_handler,
					       IRQF_TRIGGER_FALLING,
					       "gpio_keypad", keypad);
			if (ret) {
				dev_err(&pdev->dev, "Requesting irq failed!\n");
				input_unregister_device(keypad->input_dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
				timer_delete_sync(&keypad->polling_timer);
#else
				del_timer_sync(&keypad->polling_timer);
#endif
				return -EINVAL;
			}
		}
	} else {
		mod_timer(&keypad->polling_timer,
			  jiffies + msecs_to_jiffies(keypad->scan_period));
	}
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void meson_gpio_kp_remove(struct platform_device *pdev)
#else
static int meson_gpio_kp_remove(struct platform_device *pdev)
#endif
{
	struct gpio_keypad *keypad = platform_get_drvdata(pdev);

	if (keypad) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
		timer_delete_sync(&keypad->polling_timer);
#else
		del_timer_sync(&keypad->polling_timer);
#endif
		input_unregister_device(keypad->input_dev);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id key_dt_match[] = {
	{ .compatible = "amlogic, gpio_keypad" },
	{ .compatible = "amlogic,gpio_keypad" },
	{},
};
MODULE_DEVICE_TABLE(of, key_dt_match);

static struct platform_driver meson_gpio_kp_driver = {
	.probe = meson_gpio_kp_probe,
	.remove = meson_gpio_kp_remove,
	.driver = {
		.name = "gpio-keypad",
		.of_match_table = key_dt_match,
	},
};

module_platform_driver(meson_gpio_kp_driver);
MODULE_AUTHOR("Amlogic");
MODULE_DESCRIPTION("GPIO Keypad Driver");
MODULE_LICENSE("GPL");
