// SPDX-License-Identifier: GPL-2.0
/*
 * quasar_pcba_ids.c - Quasar PCBA ID eeproms parsing driver
 *
 * Copyright (C) 2025 AtriOS Team
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/sysfs.h>
#include <linux/crc-itu-t.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define PCBA_IDS_DRV_NAME	"pcba_ids"

#define MAX_EEPROM_SIZE		512
#define MAX_EEPROM_COUNT	2
#define MAX_UNIFYKEY_COUNT	4
#define MAX_FIELD_VALUE_LEN	256

struct pcba_field {
	char name[32];
	char value[MAX_FIELD_VALUE_LEN];
};

struct pcba_data {
	struct pcba_field fields[16];
	int num_fields;
};

struct eeprom_info {
	struct i2c_client *client;
	int size;
	char name[32];
};

struct pcba_priv {
	struct device *dev;
	struct eeprom_info eeproms[MAX_EEPROM_COUNT];
	int num_eeproms;
	char unifykey_names[MAX_UNIFYKEY_COUNT][32];
	int num_unifykeys;
	struct pcba_data data;
	struct mutex lock;
};

static int secure_storage_read(const char *name, char *buf, int len)
{
	return -ENODEV;
}

static int parse_sv2(const u8 *data, int len, struct pcba_data *pdata)
{
	char fmt[] = "01Y%4u%2s%2u%6u%6u%5u%c";
	u32 revision_hi, revision_lo, serial_prefix;
	u16 revision_mid;
	char model[8], revision_add;
	int ret;
	int pos = 0;

	if (len < 24)
		return -EINVAL;

	memset(model, 0, sizeof(model));
	ret = sscanf(data, fmt, &revision_hi, model, &revision_mid,
		     &revision_lo, &serial_prefix, &revision_add);
	if (ret < 6)
		return -EINVAL;

	snprintf(pdata->fields[pos].name, sizeof(pdata->fields[pos].name),
		 "product_model");
	snprintf(pdata->fields[pos].value, sizeof(pdata->fields[pos].value),
		 "%s", model);
	pos++;

	snprintf(pdata->fields[pos].name, sizeof(pdata->fields[pos].name),
		 "revision");
	snprintf(pdata->fields[pos].value, sizeof(pdata->fields[pos].value),
		 "%u.%u.%u", revision_hi, revision_mid, revision_lo);
	pos++;

	snprintf(pdata->fields[pos].name, sizeof(pdata->fields[pos].name),
		 "serial");
	snprintf(pdata->fields[pos].value, sizeof(pdata->fields[pos].value),
		 "%06u", serial_prefix);
	pos++;

	pdata->num_fields = pos;
	return 0;
}

static int parse_eeproms(struct pcba_priv *priv)
{
	u8 buf[MAX_EEPROM_SIZE];
	int ret, i;

	for (i = 0; i < priv->num_eeproms; i++) {
		struct i2c_client *client = priv->eeproms[i].client;

		ret = i2c_master_recv(client, buf, priv->eeproms[i].size);
		if (ret < 0)
			continue;

		if (parse_sv2(buf, ret, &priv->data) == 0) {
			dev_info(priv->dev, "parsed SV2 from eeprom %s\n",
				 priv->eeproms[i].name);
			return 0;
		}
	}

	return -ENOENT;
}

struct unifykey_data {
	char name[32];
	char serial[64];
	char date[32];
	char struct_v[32];
	char assembly[32];
};

static int parse_unifykeys(struct pcba_priv *priv)
{
	char buf[256];
	struct unifykey_data uk;
	int ret, i;

	for (i = 0; i < priv->num_unifykeys; i++) {
		ret = secure_storage_read(priv->unifykey_names[i], buf, sizeof(buf));
		if (ret < 0)
			continue;

		memset(&uk, 0, sizeof(uk));

		priv->data.num_fields = 0;
		break;
	}

	return 0;
}

static ssize_t pcba_ids_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct pcba_priv *priv = dev_get_drvdata(dev);
	ssize_t len = 0;
	int i;

	mutex_lock(&priv->lock);

	len += scnprintf(buf + len, PAGE_SIZE - len, "{\n");

	for (i = 0; i < priv->data.num_fields; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "\"%s\": \"%s\"", priv->data.fields[i].name,
				 priv->data.fields[i].value);
	}
	len += scnprintf(buf + len, PAGE_SIZE - len, "}\n");

	mutex_unlock(&priv->lock);

	return len;
}

static DEVICE_ATTR_RO(pcba_ids);

static struct attribute *pcba_ids_drv_attrs[] = {
	&dev_attr_pcba_ids.attr,
	NULL,
};

static const struct attribute_group pcba_ids_drv_group = {
	.attrs = pcba_ids_drv_attrs,
};

static int pcba_ids_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pcba_priv *priv;
	struct device_node *eeprom_np;
	struct i2c_client *eeprom_client;
	const char *eeprom_name;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	mutex_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	for (i = 0; ; i++) {
		eeprom_np = of_parse_phandle(np, "eeproms", i);
		if (!eeprom_np)
			break;

		if (i >= MAX_EEPROM_COUNT) {
			of_node_put(eeprom_np);
			break;
		}

		eeprom_client = of_find_i2c_device_by_node(eeprom_np);
		of_node_put(eeprom_np);
		if (!eeprom_client) {
			dev_err(dev, "[pcba_id err] eeprom %d: i2c device not found\n", i);
			continue;
		}

		priv->eeproms[priv->num_eeproms].client = eeprom_client;
		priv->eeproms[priv->num_eeproms].size = MAX_EEPROM_SIZE;

		if (of_property_read_string(np, "eeprom-names", &eeprom_name) == 0) {
			snprintf(priv->eeproms[priv->num_eeproms].name,
				 sizeof(priv->eeproms[priv->num_eeproms].name),
				 "%s", eeprom_name);
		} else {
			snprintf(priv->eeproms[priv->num_eeproms].name,
				 sizeof(priv->eeproms[priv->num_eeproms].name),
				 "eeprom%d", priv->num_eeproms);
		}

		priv->num_eeproms++;
	}

	if (priv->num_eeproms == 0) {
		dev_err(dev, "[pcba_id err] no eeproms found\n");
		return -ENODEV;
	}

	of_property_read_string_array(np, "unifykey-names",
				     (const char **)&priv->unifykey_names,
				     MAX_UNIFYKEY_COUNT);

	parse_eeproms(priv);
	parse_unifykeys(priv);

	ret = sysfs_create_group(&dev->kobj, &pcba_ids_drv_group);
	if (ret) {
		dev_err(dev, "[pcba_id err] sysfs creation failed\n");
		return ret;
	}

	dev_info(dev, "pcba_ids driver initialized\n");
	return 0;
}

static void pcba_ids_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	sysfs_remove_group(&dev->kobj, &pcba_ids_drv_group);

	dev_info(dev, "pcba_ids driver removed\n");
}

static const struct of_device_id pcba_ids_of_match[] = {
	{ .compatible = "pcba-ids" },
	{ }
};

MODULE_DEVICE_TABLE(of, pcba_ids_of_match);

static struct platform_driver pcba_ids_driver = {
	.driver = {
		.name = PCBA_IDS_DRV_NAME,
		.of_match_table = pcba_ids_of_match,
	},
	.probe = pcba_ids_probe,
	.remove = pcba_ids_remove,
};

module_platform_driver(pcba_ids_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("AtriOS Team");
MODULE_DESCRIPTION("Quasar PCBA ID eeproms parsing driver");
