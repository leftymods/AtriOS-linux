// SPDX-License-Identifier: GPL-2.0-only
/*
 * atrios-keypad.c - polling GPIO keypad for Yandex Station devices
 *
 * The vendor 4.9.113 kernel has an Amlogic "gpio_keypad" driver built in
 * (CONFIG_AMLOGIC_GPIO_KEY=y in meson64_defconfig).  There is no keypad.ko
 * in the shipped module set and the driver sources were dropped from the
 * public Amlogic tree, so the binding is reconstructed from the vendor DTS:
 *
 *   gpio_keypad {
 *           compatible = "amlogic, gpio_keypad";
 *           status = "okay";
 *           scan_period = <20>;             // ms between samples
 *           key_num = <2>;
 *           key_name = "function", "micmute";
 *           key_code = <0x246 0xf8>;
 *           key-gpios = <&gpio GPIOA_15 GPIO_ACTIVE_LOW>,
 *                       <&gpio GPIOA_14 GPIO_ACTIVE_LOW>;
 *           detect_mode = <0>;
 *   };
 *
 * Vendor key codes:
 *
 *   0x246 = 582 = KEY_VOICECOMMAND ("function"/Alice button; Android
 *                 userspace maps 582 to VOICE_ASSIST in Generic.kl)
 *   0x0f8 = 248 = KEY_MICMUTE
 *
 * 582 is not BTN_TRIGGER_HAPPY7 - that code is 0x2c6 (710).  Codes are taken
 * verbatim from DT, so a node that asks for BTN_TRIGGER_HAPPY7 just works.
 *
 * gpio-keys cannot replace this driver: it is strictly interrupt driven (it
 * calls gpiod_to_irq() for every button and fails without an IRQ), while the
 * vendor keypad samples the lines periodically in polling mode
 * (detect_mode = 0), with no per-button interrupt and no debounce.
 *
 * This driver keeps the vendor model: one delayed work item samples all
 * lines every scan_period ms, accepts a new state only after two identical
 * samples in a row (contact bounce filter) and reports a full EV_KEY
 * press/release pair plus one EV_SYN per changed key.
 *
 * detect_mode: other Amlogic device trees document it as "0: polling mode,
 * 1: irq mode" and the Station Max uses 0.  This driver always polls, so
 * detect_mode is only used as a polarity selector, as agreed for the port:
 * 0 keeps the level implied by the DT flags (GPIO_ACTIVE_LOW -> pressed is
 * low), 1 inverts it.  With the Station Max node both readings agree.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/workqueue.h>

#define ATRIOS_KEYPAD_MAX_KEYS		8
#define ATRIOS_KEYPAD_DEFAULT_KEYS	2
#define ATRIOS_KEYPAD_DEFAULT_PERIOD_MS	20
#define ATRIOS_KEYPAD_MIN_PERIOD_MS	1
#define ATRIOS_KEYPAD_MAX_PERIOD_MS	1000

/* Used only when the keypad node has neither key-codes nor key_code. */
static const u32 atrios_keypad_default_codes[ATRIOS_KEYPAD_DEFAULT_KEYS] = {
	KEY_VOICECOMMAND,	/* 582, vendor 0x246, "function" */
	KEY_MICMUTE,		/* 248, vendor 0x0f8, "micmute" */
};

struct atrios_keypad {
	struct device *dev;
	struct gpio_descs *gpios;
	struct input_dev *input;
	unsigned int num_keys;
	u32 keycodes[ATRIOS_KEYPAD_MAX_KEYS];
	const char *names[ATRIOS_KEYPAD_MAX_KEYS];
	unsigned int scan_period_ms;
	bool invert;
	bool stopping;
	/* last raw sample and last state reported to the input layer */
	unsigned int last_raw;
	unsigned int stable;
	/* protects the sampled state and the stopping flag */
	struct mutex lock;
	struct delayed_work work;
};

/* Report every key whose state differs from the last reported one. */
static void atrios_keypad_report(struct atrios_keypad *kp, unsigned int state)
{
	unsigned int changed = state ^ kp->stable;
	unsigned int i;

	for (i = 0; i < kp->num_keys; i++) {
		if (!(changed & BIT(i)))
			continue;

		input_report_key(kp->input, kp->keycodes[i],
				 !!(state & BIT(i)));
	}

	input_sync(kp->input);

	kp->stable = state;
}

static void atrios_keypad_scan(struct work_struct *work)
{
	struct atrios_keypad *kp = container_of(work, struct atrios_keypad,
						work.work);
	unsigned int raw = 0;
	unsigned int i;
	int level;

	mutex_lock(&kp->lock);

	if (kp->stopping)
		goto out;

	for (i = 0; i < kp->num_keys; i++) {
		level = gpiod_get_value_cansleep(kp->gpios->desc[i]);
		if (level < 0) {
			dev_warn_ratelimited(kp->dev,
					     "failed to read key gpio %u: %d\n",
					     i, level);
			goto resched;
		}

		/* detect_mode = 1 inverts the level from the DT flags. */
		if (kp->invert)
			level = !level;

		raw |= (unsigned int)level << i;
	}

	/*
	 * A new state is only accepted after two consecutive identical
	 * samples, which filters the bounce of the mechanical buttons.
	 */
	if (raw != kp->last_raw)
		kp->last_raw = raw;
	else if (raw != kp->stable)
		atrios_keypad_report(kp, raw);

resched:
	mod_delayed_work(system_wq, &kp->work,
			 msecs_to_jiffies(kp->scan_period_ms));
out:
	mutex_unlock(&kp->lock);
}

static int atrios_keypad_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atrios_keypad *kp;
	const char *prop;
	unsigned int raw = 0;
	unsigned int i;
	int count, level, ret;
	u32 val;

	kp = devm_kzalloc(dev, sizeof(*kp), GFP_KERNEL);
	if (!kp)
		return -ENOMEM;

	kp->dev = dev;
	mutex_init(&kp->lock);
	INIT_DELAYED_WORK(&kp->work, atrios_keypad_scan);

	kp->gpios = devm_gpiod_get_array(dev, "key", GPIOD_IN);
	if (IS_ERR(kp->gpios))
		return dev_err_probe(dev, PTR_ERR(kp->gpios),
				     "unable to get key-gpios\n");

	if (!kp->gpios->ndescs)
		return dev_err_probe(dev, -EINVAL, "no key-gpios\n");

	if (kp->gpios->ndescs > ATRIOS_KEYPAD_MAX_KEYS)
		return dev_err_probe(dev, -EINVAL,
				     "too many key-gpios: %u, max %u\n",
				     kp->gpios->ndescs,
				     ATRIOS_KEYPAD_MAX_KEYS);

	kp->num_keys = kp->gpios->ndescs;

	ret = device_property_read_u32(dev, "key_num", &val);
	if (!ret && val != kp->num_keys)
		return dev_err_probe(dev, -EINVAL,
				     "key_num %u does not match the %u key-gpios\n",
				     val, kp->num_keys);

	if (device_property_present(dev, "key-codes"))
		prop = "key-codes";
	else if (device_property_present(dev, "key_code"))
		prop = "key_code";
	else
		prop = NULL;

	if (prop) {
		count = device_property_count_u32(dev, prop);
		if (count != (int)kp->num_keys)
			return dev_err_probe(dev, count < 0 ? count : -EINVAL,
					     "%s has %d entries, %u keys expected\n",
					     prop, count, kp->num_keys);

		ret = device_property_read_u32_array(dev, prop, kp->keycodes,
						     count);
		if (ret)
			return dev_err_probe(dev, ret, "failed to read %s\n",
					     prop);
	} else {
		if (kp->num_keys > ATRIOS_KEYPAD_DEFAULT_KEYS)
			return dev_err_probe(dev, -EINVAL,
					     "no key_code for %u keys\n",
					     kp->num_keys);

		for (i = 0; i < kp->num_keys; i++)
			kp->keycodes[i] = atrios_keypad_default_codes[i];
	}

	for (i = 0; i < kp->num_keys; i++) {
		if (!kp->keycodes[i] || kp->keycodes[i] >= KEY_CNT)
			return dev_err_probe(dev, -EINVAL,
					     "invalid key code %u for key %u\n",
					     kp->keycodes[i], i);
	}

	/* Names are cosmetic, they only end up in log messages. */
	prop = "key-names";
	count = device_property_string_array_count(dev, prop);
	if (count < 0) {
		prop = "key_name";
		count = device_property_string_array_count(dev, prop);
	}

	if (count >= 0) {
		if (count != (int)kp->num_keys)
			dev_warn(dev, "%s has %d entries, ignoring\n",
				 prop, count);
		else if (device_property_read_string_array(dev, prop,
							   kp->names,
							   count) < 0)
			dev_warn(dev, "failed to read %s\n", prop);
	}

	kp->scan_period_ms = ATRIOS_KEYPAD_DEFAULT_PERIOD_MS;
	ret = device_property_read_u32(dev, "scan_period", &val);
	if (!ret) {
		if (val < ATRIOS_KEYPAD_MIN_PERIOD_MS ||
		    val > ATRIOS_KEYPAD_MAX_PERIOD_MS)
			dev_warn(dev,
				 "scan_period %u ms out of range, using %u ms\n",
				 val, kp->scan_period_ms);
		else
			kp->scan_period_ms = val;
	}

	ret = device_property_read_u32(dev, "detect_mode", &val);
	if (!ret && val) {
		if (val != 1)
			dev_warn(dev, "unknown detect_mode %u, inverting\n",
				 val);
		kp->invert = true;
	}

	kp->input = devm_input_allocate_device(dev);
	if (!kp->input)
		return -ENOMEM;

	kp->input->name = "atrios-keypad";
	kp->input->phys = "atrios-keypad/input0";
	kp->input->id.bustype = BUS_HOST;

	for (i = 0; i < kp->num_keys; i++)
		input_set_capability(kp->input, EV_KEY, kp->keycodes[i]);
	/* EV_SYN/SYN_REPORT is enabled by the input core for every device. */

	/*
	 * Sample the rest level before registering: a button that is already
	 * held at probe time must not generate an event.
	 */
	for (i = 0; i < kp->num_keys; i++) {
		level = gpiod_get_value_cansleep(kp->gpios->desc[i]);
		if (level < 0)
			return dev_err_probe(dev, level,
					     "failed to read key gpio %u\n", i);

		if (kp->invert)
			level = !level;

		raw |= (unsigned int)level << i;
	}
	kp->last_raw = raw;
	kp->stable = raw;

	ret = input_register_device(kp->input);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register input device\n");

	platform_set_drvdata(pdev, kp);

	for (i = 0; i < kp->num_keys; i++)
		dev_info(dev, "key %u: %s, code %u\n", i,
			 kp->names[i] ? kp->names[i] : "?", kp->keycodes[i]);

	dev_info(dev, "scanning every %u ms%s\n", kp->scan_period_ms,
		 kp->invert ? ", inverted levels" : "");

	mod_delayed_work(system_wq, &kp->work,
			 msecs_to_jiffies(kp->scan_period_ms));

	return 0;
}

static void atrios_keypad_remove(struct platform_device *pdev)
{
	struct atrios_keypad *kp = platform_get_drvdata(pdev);

	mutex_lock(&kp->lock);
	kp->stopping = true;
	mutex_unlock(&kp->lock);

	cancel_delayed_work_sync(&kp->work);

	/* The input device itself is released by devres. */
}

static const struct of_device_id atrios_keypad_of_match[] = {
	{ .compatible = "yandex,gpio-keypad" },
	{ .compatible = "amlogic,gpio_keypad" },
	/*
	 * The vendor DTS spells the string with a space; keep it verbatim so
	 * a stock Station DTB binds as well.
	 */
	{ .compatible = "amlogic, gpio_keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, atrios_keypad_of_match);

static struct platform_driver atrios_keypad_driver = {
	.probe = atrios_keypad_probe,
	.remove = atrios_keypad_remove,
	.driver = {
		.name = "atrios-keypad",
		.of_match_table = atrios_keypad_of_match,
	},
};
module_platform_driver(atrios_keypad_driver);

MODULE_AUTHOR("Yandex Inc");
MODULE_AUTHOR("AtriOS mainline port");
MODULE_DESCRIPTION("Polling GPIO keypad for Yandex Station devices");
MODULE_ALIAS("platform:atrios-keypad");
MODULE_LICENSE("GPL");
