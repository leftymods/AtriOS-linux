// SPDX-License-Identifier: GPL-2.0-only
/*
 * rotary-volume.c - GPIO rotary volume encoder for Yandex Station devices
 *
 * The vendor 4.9 driver (rotary-volume.ko) requests a falling-edge IRQ on
 * both encoder GPIOs, queues a work item and samples both lines there.  The
 * two-bit state is (bit 0 = first GPIO, bit 1 = second GPIO):
 *
 *   state 01 (first high, second low)  -> key-codes[0]
 *   state 10 (first low, second high)  -> key-codes[1]
 *   state 00 / 11                      -> no event
 *
 * Every accepted event is reported as a complete key press and release, so
 * one mechanical detent produces one KEY_PAGEUP/KEY_PAGEDOWN click.  The
 * vendor driver has no debounce and no autorepeat.
 *
 * This driver keeps the same event mapping but reads the lines from a
 * threaded IRQ handler (GPIO controllers behind I2C/SPI may sleep), tracks
 * the previous state to decode the quadrature sequence, and adds a short
 * timestamp based debounce.  IRQs are requested on both edges so the whole
 * Gray-code sequence is visible; only a 1 -> 0 transition of one line while
 * the other line is high is treated as a detent, which is equivalent to the
 * vendor's falling-edge-only IRQ but also works when a rising edge is the
 * first edge seen after probe.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define ROTARY_VOLUME_KEYS		2
#define ROTARY_VOLUME_DEBOUNCE_MS	5

struct rotary_volume {
	struct device *dev;
	struct gpio_descs *gpios;
	struct input_dev *input;
	u32 keycodes[ROTARY_VOLUME_KEYS];
	unsigned int state;
	ktime_t last_event;
	struct mutex lock;
};

static void rotary_volume_input_unregister(void *data)
{
	input_unregister_device(data);
}

static irqreturn_t rotary_volume_irq(int irq, void *data)
{
	struct rotary_volume *rv = data;
	unsigned int prev, state = 0, changed;
	unsigned int code;
	ktime_t now;
	int i, val;

	mutex_lock(&rv->lock);

	for (i = 0; i < ROTARY_VOLUME_KEYS; i++) {
		val = gpiod_get_value_cansleep(rv->gpios->desc[i]);
		if (val < 0) {
			dev_warn_ratelimited(rv->dev,
					     "failed to read gpio %d: %d\n", i, val);
			mutex_unlock(&rv->lock);
			return IRQ_HANDLED;
		}
		state |= (unsigned int)val << i;
	}

	prev = rv->state;
	if (state == prev)
		goto out;

	rv->state = state;
	changed = prev ^ state;

	/*
	 * A detent is the falling edge of one line while the other line is
	 * still high.  The other two edges of the cycle (00 and back to 11)
	 * are ignored, so one detent gives exactly one key event even though
	 * both edges are requested.
	 */
	/*
	 * The vendor packs state as (gpio0 << 1) | gpio1 and reports
	 * keycodes[0] when the first line falls while the second stays high.
	 * Keep that mapping so the direction matches the stock driver.
	 */
	if (changed == BIT(0)) {
		if (!(prev & BIT(0)) || !(state & BIT(1)))
			goto out;
		code = rv->keycodes[0];
	} else if (changed == BIT(1)) {
		if (!(prev & BIT(1)) || !(state & BIT(0)))
			goto out;
		code = rv->keycodes[1];
	} else {
		goto out;
	}

	now = ktime_get();
	if (rv->last_event &&
	    !ktime_after(now, ktime_add_ms(rv->last_event,
					  ROTARY_VOLUME_DEBOUNCE_MS)))
		goto out;

	rv->last_event = now;
	input_report_key(rv->input, code, 1);
	input_report_key(rv->input, code, 0);
	input_sync(rv->input);

out:
	mutex_unlock(&rv->lock);
	return IRQ_HANDLED;
}

static int rotary_volume_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rotary_volume *rv;
	struct input_dev *input;
	unsigned int state = 0;
	int i, ret, val;

	rv = devm_kzalloc(dev, sizeof(*rv), GFP_KERNEL);
	if (!rv)
		return -ENOMEM;

	rv->dev = dev;
	mutex_init(&rv->lock);

	rv->gpios = devm_gpiod_get_array(dev, NULL, GPIOD_IN);
	if (IS_ERR(rv->gpios))
		return dev_err_probe(dev, PTR_ERR(rv->gpios), "unable to get gpios\n");

	if (rv->gpios->ndescs != ROTARY_VOLUME_KEYS) {
		dev_err(dev, "exactly two gpios expected, got %u\n",
			rv->gpios->ndescs);
		return -EINVAL;
	}

	rv->keycodes[0] = KEY_PAGEUP;	/* what /usr/lib/atrios/rotaryd reads */
	rv->keycodes[1] = KEY_PAGEDOWN;

	if (device_property_present(dev, "key-codes")) {
		ret = device_property_count_u32(dev, "key-codes");
		if (ret != ROTARY_VOLUME_KEYS) {
			dev_err(dev,
				"unexpected amount of key-codes, got: %d, expect: %d\n",
				ret, ROTARY_VOLUME_KEYS);
			return ret < 0 ? ret : -EINVAL;
		}

		ret = device_property_read_u32_array(dev, "key-codes",
						     rv->keycodes,
						     ROTARY_VOLUME_KEYS);
		if (ret)
			return dev_err_probe(dev, ret, "failed to read key-codes\n");
	}

	/* Remember where the encoder rests so the first detent is not lost. */
	for (i = 0; i < ROTARY_VOLUME_KEYS; i++) {
		val = gpiod_get_value_cansleep(rv->gpios->desc[i]);
		if (val < 0)
			return dev_err_probe(dev, val, "failed to read gpio %d\n", i);
		state |= (unsigned int)val << i;
	}
	rv->state = state;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = "rotary-volume";
	input->phys = "rotary-volume/input0";
	input->id.bustype = BUS_HOST;	/* vendor value 25 = BUS_HOST */

	for (i = 0; i < ROTARY_VOLUME_KEYS; i++)
		input_set_capability(input, EV_KEY, rv->keycodes[i]);

	ret = input_register_device(input);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register input device\n");

	rv->input = input;

	/*
	 * The IRQs are requested after this action, so devres frees them
	 * before unregistering the input device on unbind.
	 */
	ret = devm_add_action_or_reset(dev, rotary_volume_input_unregister,
				       input);
	if (ret)
		return ret;

	for (i = 0; i < ROTARY_VOLUME_KEYS; i++) {
		int irq = gpiod_to_irq(rv->gpios->desc[i]);

		if (irq < 0)
			return dev_err_probe(dev, irq,
					     "unable to get IRQ for gpio %d\n", i);

		ret = devm_request_threaded_irq(dev, irq, NULL,
						rotary_volume_irq,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						dev_name(dev), rv);
		if (ret)
			return dev_err_probe(dev, ret,
					     "unable to request IRQ %d\n", irq);
	}

	platform_set_drvdata(pdev, rv);

	return 0;
}

static const struct of_device_id rotary_volume_of_match[] = {
	{ .compatible = "yandex,rotary-volume" },
	{ .compatible = "rotary-volume" },
	{ }
};
MODULE_DEVICE_TABLE(of, rotary_volume_of_match);

static struct platform_driver rotary_volume_driver = {
	.probe = rotary_volume_probe,
	.driver = {
		.name = "rotary-volume",
		.of_match_table = rotary_volume_of_match,
	},
};
module_platform_driver(rotary_volume_driver);

MODULE_AUTHOR("Yandex Inc");
MODULE_AUTHOR("AtriOS mainline port");
MODULE_DESCRIPTION("GPIO rotary volume control driver");
MODULE_ALIAS("platform:rotary-volume");
MODULE_LICENSE("GPL");
