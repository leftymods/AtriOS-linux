// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/zigbee-control.c
 *
 * Yandex Station Max ZigBee module GPIO control driver.
 * Reconstructed from zigbee-control.ko (4.9.113) and ported to Mainline Linux 7.0.
 *
 * Copyright (C) Yandex Inc.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/sysfs.h>

#define DRV_NAME	"zb_control"

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose ZigBee GPIO control logging");

struct zb_gpio_line {
	const char *name;
	bool optional;
	struct gpio_desc *gpiod;
};

struct zb_control {
	struct device *dev;
	struct gpio_desc *power_gpiod;
	struct gpio_desc *reset_gpiod;
	struct gpio_desc *boot_gpiod;
#if !IS_ENABLED(CONFIG_GPIO_SYSFS)
	struct kobject *power_kobj;
	struct kobject *reset_kobj;
	struct kobject *boot_kobj;
#endif
};

#if IS_ENABLED(CONFIG_GPIO_SYSFS)

static int zb_claim_gpio(struct device *dev, const char *name,
			 enum gpiod_flags flags, bool optional,
			 struct gpio_desc **out_gpiod)
{
	struct gpio_desc *gpiod;
	int ret;

	if (optional)
		gpiod = devm_gpiod_get_optional(dev, name, flags);
	else
		gpiod = devm_gpiod_get(dev, name, flags);

	if (IS_ERR(gpiod)) {
		ret = PTR_ERR(gpiod);
		dev_err(dev, "[ZIGBEE] ERROR: Failed to request %s GPIO: %d\n", name, ret);
		return ret;
	}

	if (!gpiod) {
		dev_info(dev, "[ZIGBEE] Optional %s GPIO not present in DT\n", name);
		return 0;
	}

	ret = gpiod_export(gpiod, false);
	if (ret < 0) {
		dev_err(dev, "[ZIGBEE] ERROR: Failed to export %s GPIO: %d\n", name, ret);
		return ret;
	}

	ret = gpiod_export_link(dev, name, gpiod);
	if (ret < 0) {
		dev_err(dev, "[ZIGBEE] ERROR: Failed to export_link %s GPIO: %d\n", name, ret);
		gpiod_unexport(gpiod);
		return ret;
	}

	if (out_gpiod)
		*out_gpiod = gpiod;

	dev_info(dev, "[ZIGBEE] Claimed %s GPIO (hw:%d, initial=%d)\n",
		 name, desc_to_gpio(gpiod), gpiod_get_value_cansleep(gpiod));
	return 0;
}

#else /* !CONFIG_GPIO_SYSFS (Mainline Linux 7.0 fallback) */

struct zb_kobj_attr {
	struct kobj_attribute attr;
	struct gpio_desc *gpiod;
	struct device *dev;
	const char *line_name;
};

static ssize_t zb_gpio_value_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct zb_kobj_attr *zattr = container_of(attr, struct zb_kobj_attr, attr);
	int val = gpiod_get_value_cansleep(zattr->gpiod);

	return scnprintf(buf, PAGE_SIZE, "%d\n", val < 0 ? 0 : val);
}

static ssize_t zb_gpio_value_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	struct zb_kobj_attr *zattr = container_of(attr, struct zb_kobj_attr, attr);
	long val;
	int ret;

	ret = kstrtol(buf, 0, &val);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(zattr->gpiod, !!val);
	if (debug && zattr->dev)
		dev_info(zattr->dev, "[ZIGBEE] Set %s (hw:%d) = %d\n",
			 zattr->line_name, desc_to_gpio(zattr->gpiod), !!val);
	return count;
}

static int zb_claim_gpio_sysfs_compat(struct device *dev, const char *name,
				      enum gpiod_flags flags, bool optional,
				      struct gpio_desc **out_gpiod,
				      struct kobject **out_kobj)
{
	struct gpio_desc *gpiod;
	struct kobject *kobj;
	struct zb_kobj_attr *zattr;
	int ret;

	if (optional)
		gpiod = devm_gpiod_get_optional(dev, name, flags);
	else
		gpiod = devm_gpiod_get(dev, name, flags);

	if (IS_ERR(gpiod)) {
		ret = PTR_ERR(gpiod);
		dev_err(dev, "[ZIGBEE] ERROR: Failed to request %s GPIO: %d\n", name, ret);
		return ret;
	}

	if (!gpiod) {
		dev_info(dev, "[ZIGBEE] Optional %s GPIO not present in DT\n", name);
		return 0;
	}

	zattr = devm_kzalloc(dev, sizeof(*zattr), GFP_KERNEL);
	if (!zattr)
		return -ENOMEM;

	kobj = kobject_create_and_add(name, &dev->kobj);
	if (!kobj)
		return -ENOMEM;

	sysfs_attr_init(&zattr->attr.attr);
	zattr->attr.attr.name = "value";
	zattr->attr.attr.mode = 0644;
	zattr->attr.show = zb_gpio_value_show;
	zattr->attr.store = zb_gpio_value_store;
	zattr->gpiod = gpiod;
	zattr->dev = dev;
	zattr->line_name = name;

	ret = sysfs_create_file(kobj, &zattr->attr.attr);
	if (ret) {
		kobject_put(kobj);
		return ret;
	}

	if (out_gpiod)
		*out_gpiod = gpiod;
	if (out_kobj)
		*out_kobj = kobj;

	dev_info(dev, "[ZIGBEE] Claimed %s GPIO (hw:%d, initial=%d, sysfs=%s/value)\n",
		 name, desc_to_gpio(gpiod), gpiod_get_value_cansleep(gpiod), name);
	return 0;
}

#endif /* CONFIG_GPIO_SYSFS */

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct zb_control *zb = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE,
			 "zb-power: %s (hw:%d val=%d)\n"
			 "zb-reset: %s (hw:%d val=%d)\n"
			 "zb-boot:  %s (hw:%d val=%d)\n",
			 zb->power_gpiod ? "present" : "none",
			 zb->power_gpiod ? desc_to_gpio(zb->power_gpiod) : -1,
			 zb->power_gpiod ? gpiod_get_value_cansleep(zb->power_gpiod) : -1,
			 zb->reset_gpiod ? "present" : "none",
			 zb->reset_gpiod ? desc_to_gpio(zb->reset_gpiod) : -1,
			 zb->reset_gpiod ? gpiod_get_value_cansleep(zb->reset_gpiod) : -1,
			 zb->boot_gpiod ? "present" : "none",
			 zb->boot_gpiod ? desc_to_gpio(zb->boot_gpiod) : -1,
			 zb->boot_gpiod ? gpiod_get_value_cansleep(zb->boot_gpiod) : -1);
}
static DEVICE_ATTR_RO(status);

static int zb_control_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zb_control *zb;
	int ret;

	dev_info(dev, "[ZIGBEE] Probing zigbee-control driver...\n");

	zb = devm_kzalloc(dev, sizeof(*zb), GFP_KERNEL);
	if (!zb)
		return -ENOMEM;

	zb->dev = dev;
	platform_set_drvdata(pdev, zb);

#if IS_ENABLED(CONFIG_GPIO_SYSFS)
	ret = zb_claim_gpio(dev, "zb-power", GPIOD_OUT_HIGH, true, &zb->power_gpiod);
	if (ret)
		return ret;

	ret = zb_claim_gpio(dev, "zb-reset", GPIOD_OUT_HIGH, false, &zb->reset_gpiod);
	if (ret)
		goto err_power;

	ret = zb_claim_gpio(dev, "zb-boot", GPIOD_OUT_HIGH, true, &zb->boot_gpiod);
	if (ret)
		goto err_reset;

	device_create_file(dev, &dev_attr_status);
	dev_info(dev, "[ZIGBEE] Probed OK\n");
	return 0;

err_reset:
	if (zb->reset_gpiod) {
		sysfs_remove_link(&dev->kobj, "zb-reset");
		gpiod_unexport(zb->reset_gpiod);
	}
err_power:
	if (zb->power_gpiod) {
		sysfs_remove_link(&dev->kobj, "zb-power");
		gpiod_unexport(zb->power_gpiod);
	}
	return ret;
#else
	ret = zb_claim_gpio_sysfs_compat(dev, "zb-power", GPIOD_OUT_HIGH, true,
					 &zb->power_gpiod, &zb->power_kobj);
	if (ret)
		return ret;

	ret = zb_claim_gpio_sysfs_compat(dev, "zb-reset", GPIOD_OUT_HIGH, false,
					 &zb->reset_gpiod, &zb->reset_kobj);
	if (ret)
		goto err_power;

	ret = zb_claim_gpio_sysfs_compat(dev, "zb-boot", GPIOD_OUT_HIGH, true,
					 &zb->boot_gpiod, &zb->boot_kobj);
	if (ret)
		goto err_reset;

	device_create_file(dev, &dev_attr_status);
	dev_info(dev, "[ZIGBEE] Probed OK\n");
	return 0;

err_reset:
	if (zb->reset_kobj)
		kobject_put(zb->reset_kobj);
err_power:
	if (zb->power_kobj)
		kobject_put(zb->power_kobj);
	return ret;
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void zb_control_remove(struct platform_device *pdev)
#else
static int zb_control_remove(struct platform_device *pdev)
#endif
{
	struct zb_control *zb = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_status);

	if (zb) {
#if IS_ENABLED(CONFIG_GPIO_SYSFS)
		if (zb->boot_gpiod) {
			sysfs_remove_link(&pdev->dev.kobj, "zb-boot");
			gpiod_unexport(zb->boot_gpiod);
		}
		if (zb->reset_gpiod) {
			sysfs_remove_link(&pdev->dev.kobj, "zb-reset");
			gpiod_unexport(zb->reset_gpiod);
		}
		if (zb->power_gpiod) {
			sysfs_remove_link(&pdev->dev.kobj, "zb-power");
			gpiod_unexport(zb->power_gpiod);
		}
#else
		if (zb->boot_kobj)
			kobject_put(zb->boot_kobj);
		if (zb->reset_kobj)
			kobject_put(zb->reset_kobj);
		if (zb->power_kobj)
			kobject_put(zb->power_kobj);
#endif
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id zb_control_of_match[] = {
	{ .compatible = "yandex,zigbee-control" },
	{ .compatible = "leftymods,zigbee-control" },
	{ .compatible = "zigbee-control" },
	{ }
};
MODULE_DEVICE_TABLE(of, zb_control_of_match);

static struct platform_driver zb_control_driver = {
	.probe = zb_control_probe,
	.remove = zb_control_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = zb_control_of_match,
	},
};
module_platform_driver(zb_control_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yandex Inc.");
MODULE_DESCRIPTION("Driver for ZigBee module GPIO control");
