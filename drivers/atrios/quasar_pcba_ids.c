// SPDX-License-Identifier: GPL-2.0
/*
 * quasar_pcba_ids.c - Quasar PCBA ID eeproms parsing driver
 *
 * Copyright (C) 2025 AtriOS Team
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/sysfs.h>
#include <linux/crc-itu-t.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/string.h>

#define PCBA_IDS_DRV_NAME	"pcba_ids"

#define EEPROM_READ_SIZE	128
#define MAX_EEPROM_SIZE		256
#define MAX_EEPROM_COUNT	4
#define MAX_UNIFYKEY_COUNT	4

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose EEPROM read and SV2 parsing logs");

struct eeprom_info {
	struct i2c_client *client;
	struct i2c_adapter *adap;
	u16 addr;
	int size;
	char name[32];
	int last_read_ret;
	u8 raw_preview[64];

	bool parsed;
	char error[64];
	u32 struct_version;
	char str[96];
	char hex[256];
	char owner[16];
	char manufacturer[16];
	u32 model;
	u32 board_code;
	u32 rev_hi;
	u32 rev_mid;
	u32 rev_lo;
	char rev_add[16];
	char assembly[32];
	char date[32];
	u32 serial;
};

struct pcba_priv {
	struct device *dev;
	struct eeprom_info eeproms[MAX_EEPROM_COUNT];
	int num_eeproms;
	int parsed_count;
	char unifykey_names[MAX_UNIFYKEY_COUNT][32];
	int num_unifykeys;
	struct mutex lock;
};

static int eeprom_read_bytes(struct eeprom_info *ep, u8 *buf, int len)
{
	struct i2c_adapter *adap = ep->adap;
	u16 addr = ep->addr;
	u8 offset = 0;
	struct i2c_msg msgs[2];
	int ret;

	if (ep->client) {
		adap = ep->client->adapter;
		addr = ep->client->addr;
	}
	if (!adap)
		return -ENODEV;

	/* First write 1-byte address offset 0x00, then read len bytes */
	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &offset;

	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = buf;

	ret = i2c_transfer(adap, msgs, 2);
	if (ret == 2)
		return len;

	/* Fallback to raw recv if repeated-start fails */
	if (ep->client) {
		ret = i2c_master_recv(ep->client, buf, len);
		if (ret >= 0)
			return ret;
	}

	return ret < 0 ? ret : -EIO;
}

static void build_hex_str(const u8 *data, int ascii_len, char *out, size_t out_sz)
{
	size_t pos = 0;
	int i;

	out[0] = '\0';
	for (i = 0; i < ascii_len && pos + 6 < out_sz; i++) {
		pos += scnprintf(out + pos, out_sz - pos, "%s0x%02x",
				 (i == 0) ? "" : " ", data[i]);
	}
}

/*
 * Parse Station Max dot-separated SV2 format:
 *   "02.Y.FD.53.04.2.3.2.A.0020.230523.025372"
 * padded with 0xff up to byte 123, followed by 4-char hex CRC-16 at bytes 124..127.
 */
static int parse_sv2_dot(struct pcba_priv *priv, struct eeprom_info *ep,
			 const u8 *data, int len)
{
	char work[EEPROM_READ_SIZE + 1];
	char *p, *tok;
	char *tokens[12];
	int i, ascii_len = 0, tok_cnt = 0;

	if (len < 32)
		return -EINVAL;

	for (i = 0; i < min(len, 124); i++) {
		if (data[i] == 0xff || data[i] == 0x00 || !isprint(data[i]))
			break;
		work[i] = (char)data[i];
		ascii_len++;
	}
	work[ascii_len] = '\0';

	if (ascii_len < 20)
		return -EINVAL;

	/* Verify CRC-16 at bytes 124..127 if full 128-byte block was read */
	if (len >= 128 && isxdigit(data[124]) && isxdigit(data[125]) &&
	    isxdigit(data[126]) && isxdigit(data[127])) {
		char crc_str[5] = { data[124], data[125], data[126], data[127], 0 };
		u16 expected_crc = 0;
		u16 calc_crc = crc_itu_t(0, data, 124);

		if (kstrtou16(crc_str, 16, &expected_crc) == 0 &&
		    calc_crc != expected_crc) {
			dev_warn(priv->dev,
				 "[PCBA_IDS] EEPROM '%s' CRC mismatch: calc=0x%04x != stored=0x%04x\n",
				 ep->name, calc_crc, expected_crc);
		}
	}

	snprintf(ep->str, sizeof(ep->str), "%s", work);
	build_hex_str(data, ascii_len, ep->hex, sizeof(ep->hex));

	p = work;
	while (tok_cnt < 12 && (tok = strsep(&p, ".")) != NULL)
		tokens[tok_cnt++] = tok;

	if (tok_cnt < 12)
		return -EINVAL;

	if (kstrtouint(tokens[0], 10, &ep->struct_version) ||
	    ep->struct_version != 2)
		return -EINVAL;

	snprintf(ep->owner, sizeof(ep->owner), "%s", tokens[1]);
	snprintf(ep->manufacturer, sizeof(ep->manufacturer), "%s", tokens[2]);
	if (kstrtouint(tokens[3], 10, &ep->model))
		return -EINVAL;
	if (kstrtouint(tokens[4], 10, &ep->board_code))
		return -EINVAL;
	if (kstrtouint(tokens[5], 10, &ep->rev_hi))
		return -EINVAL;
	if (kstrtouint(tokens[6], 10, &ep->rev_mid))
		return -EINVAL;
	if (kstrtouint(tokens[7], 10, &ep->rev_lo))
		return -EINVAL;

	snprintf(ep->rev_add, sizeof(ep->rev_add), "%s", tokens[8]);
	snprintf(ep->assembly, sizeof(ep->assembly), "%s", tokens[9]);

	if (strlen(tokens[10]) == 6) {
		snprintf(ep->date, sizeof(ep->date), "%.2s.%.2s.%.2s",
			 tokens[10], tokens[10] + 2, tokens[10] + 4);
	} else {
		snprintf(ep->date, sizeof(ep->date), "%s", tokens[10]);
	}

	if (kstrtouint(tokens[11], 10, &ep->serial))
		ep->serial = 0;

	ep->parsed = true;
	ep->error[0] = '\0';
	return 0;
}

/* Fallback parser for SV1 ("01Y...") format */
static int parse_sv1(struct eeprom_info *ep, const u8 *data, int len)
{
	char fmt[] = "01Y%4u%2s%2u%6u%6u%5u%c";
	u32 model = 0, board_code = 0, date_raw = 0, serial = 0;
	u16 rev_packed = 0;
	char mfg[8] = { 0 }, rev_add = 'A';
	int ret, ascii_len = 0;

	if (len < 28 || memcmp(data, "01Y", 3) != 0)
		return -EINVAL;

	ret = sscanf((const char *)data, fmt, &model, mfg, &board_code,
		     &rev_packed, &date_raw, &serial, &rev_add);
	if (ret < 6)
		return -EINVAL;

	while (ascii_len < min(len, 64) && data[ascii_len] != 0xff &&
	       isprint(data[ascii_len]))
		ascii_len++;

	snprintf(ep->str, sizeof(ep->str), "%.*s", ascii_len, (const char *)data);
	build_hex_str(data, ascii_len, ep->hex, sizeof(ep->hex));

	ep->struct_version = 1;
	snprintf(ep->owner, sizeof(ep->owner), "Y");
	snprintf(ep->manufacturer, sizeof(ep->manufacturer), "%s", mfg);
	ep->model = model;
	ep->board_code = board_code;
	ep->rev_hi = (rev_packed / 10000) % 100;
	ep->rev_mid = (rev_packed / 100) % 100;
	ep->rev_lo = rev_packed % 100;
	snprintf(ep->rev_add, sizeof(ep->rev_add), "%c", rev_add);
	ep->assembly[0] = '\0';
	snprintf(ep->date, sizeof(ep->date), "%02u.%02u.%02u",
		 (date_raw / 10000) % 100, (date_raw / 100) % 100, date_raw % 100);
	ep->serial = serial;
	ep->parsed = true;
	ep->error[0] = '\0';
	return 0;
}

static int parse_eeproms(struct pcba_priv *priv)
{
	u8 buf[MAX_EEPROM_SIZE + 1];
	int ret, i, parsed_cnt = 0;

	for (i = 0; i < priv->num_eeproms; i++) {
		struct eeprom_info *ep = &priv->eeproms[i];

		ep->parsed = false;
		memset(buf, 0, sizeof(buf));
		ret = eeprom_read_bytes(ep, buf, ep->size);
		ep->last_read_ret = ret;
		if (ret < 0) {
			snprintf(ep->error, sizeof(ep->error), "read error %d", ret);
			dev_warn(priv->dev,
				 "[PCBA_IDS] EEPROM '%s' (0x%02x) read failed: %d\n",
				 ep->name, ep->addr, ret);
			continue;
		}

		memcpy(ep->raw_preview, buf, min_t(int, ret, sizeof(ep->raw_preview) - 1));

		if (parse_sv2_dot(priv, ep, buf, ret) == 0 ||
		    parse_sv1(ep, buf, ret) == 0) {
			dev_info(priv->dev,
				 "[PCBA_IDS] Parsed SV%u EEPROM '%s' (0x%02x): model=%u board=%u rev=%u.%u.%u.%s assembly=%s date=%s serial=%06u\n",
				 ep->struct_version, ep->name, ep->addr,
				 ep->model, ep->board_code,
				 ep->rev_hi, ep->rev_mid, ep->rev_lo, ep->rev_add,
				 ep->assembly, ep->date, ep->serial);
			parsed_cnt++;
		} else {
			snprintf(ep->error, sizeof(ep->error), "unrecognized format");
			if (debug)
				dev_info(priv->dev,
					 "[PCBA_IDS] EEPROM '%s' (0x%02x) unrecognized header: %16ph\n",
					 ep->name, ep->addr, buf);
		}
	}

	priv->parsed_count = parsed_cnt;
	return parsed_cnt > 0 ? 0 : -ENOENT;
}

static ssize_t pcba_ids_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct pcba_priv *priv = dev_get_drvdata(dev);
	ssize_t len = 0;
	int i;

	mutex_lock(&priv->lock);

	if (priv->parsed_count == 0 && priv->num_eeproms > 0)
		parse_eeproms(priv);

	len += scnprintf(buf + len, PAGE_SIZE - len, "[\n");

	for (i = 0; i < priv->num_eeproms; i++) {
		struct eeprom_info *ep = &priv->eeproms[i];

		if (ep->parsed) {
			len += scnprintf(buf + len, PAGE_SIZE - len,
					 "\t{\n"
					 "\t\t\"name\": \"%s\",\n"
					 "\t\t\"struct_version\": %u,\n"
					 "\t\t\"str\": \"%s\",\n"
					 "\t\t\"hex\": \"%s\",\n"
					 "\t\t\"owner\": \"%s\",\n"
					 "\t\t\"manufacturer\": \"%s\",\n"
					 "\t\t\"model\": %u,\n"
					 "\t\t\"board_code\": %u,\n"
					 "\t\t\"revision\": {\n"
					 "\t\t\t\"hi\": %u,\n"
					 "\t\t\t\"mid\": %u,\n"
					 "\t\t\t\"lo\": %u,\n"
					 "\t\t\t\"additional\": \"%s\"\n"
					 "\t\t},\n"
					 "\t\t\"assembly\": \"%s\",\n"
					 "\t\t\"date\": \"%s\",\n"
					 "\t\t\"serial\": %u\n"
					 "\t}%s\n",
					 ep->name, ep->struct_version,
					 ep->str, ep->hex,
					 ep->owner, ep->manufacturer,
					 ep->model, ep->board_code,
					 ep->rev_hi, ep->rev_mid, ep->rev_lo,
					 ep->rev_add, ep->assembly,
					 ep->date, ep->serial,
					 (i + 1 < priv->num_eeproms) ? "," : "");
		} else {
			len += scnprintf(buf + len, PAGE_SIZE - len,
					 "\t{\n"
					 "\t\t\"name\": \"%s\",\n"
					 "\t\t\"error\": \"%s\"\n"
					 "\t}%s\n",
					 ep->name,
					 ep->error[0] ? ep->error : "read failed",
					 (i + 1 < priv->num_eeproms) ? "," : "");
		}
	}

	len += scnprintf(buf + len, PAGE_SIZE - len, "]\n");

	mutex_unlock(&priv->lock);

	return len;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pcba_priv *priv = dev_get_drvdata(dev);
	ssize_t len = 0;
	int i, j;

	mutex_lock(&priv->lock);
	len += scnprintf(buf + len, PAGE_SIZE - len,
			 "num_eeproms=%d parsed_eeproms=%d\n",
			 priv->num_eeproms, priv->parsed_count);

	for (i = 0; i < priv->num_eeproms; i++) {
		struct eeprom_info *ep = &priv->eeproms[i];
		char ascii[49];

		for (j = 0; j < 48; j++) {
			u8 c = ep->raw_preview[j];
			ascii[j] = (c >= 0x20 && c <= 0x7e) ? (char)c : '.';
		}
		ascii[48] = '\0';

		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "eeprom[%d] name=%s addr=0x%02x parsed=%d last_read=%d str=\"%s\" ascii=\"%s\"\n",
				 i, ep->name, ep->addr, ep->parsed ? 1 : 0,
				 ep->last_read_ret, ep->str, ascii);
	}
	mutex_unlock(&priv->lock);
	return len;
}

static DEVICE_ATTR_RO(pcba_ids);
static DEVICE_ATTR_RO(status);

static struct attribute *pcba_ids_drv_attrs[] = {
	&dev_attr_pcba_ids.attr,
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group pcba_ids_drv_group = {
	.attrs = pcba_ids_drv_attrs,
};

static void pcba_add_bus_eeprom(struct pcba_priv *priv, u32 bus_nr,
				u32 addr, const char *name)
{
	struct i2c_adapter *adap;
	struct eeprom_info *ep;

	if (priv->num_eeproms >= MAX_EEPROM_COUNT)
		return;

	adap = i2c_get_adapter(bus_nr);
	if (!adap) {
		dev_warn(priv->dev,
			 "[PCBA_IDS] i2c-%u adapter not available for EEPROM '%s' (0x%02x)\n",
			 bus_nr, name, addr);
		return;
	}

	ep = &priv->eeproms[priv->num_eeproms++];
	ep->adap = adap;
	ep->addr = (u16)addr;
	ep->size = EEPROM_READ_SIZE;
	snprintf(ep->name, sizeof(ep->name), "%s", name);
	dev_info(priv->dev,
		 "[PCBA_IDS] Registered EEPROM '%s' on i2c-%u @ 0x%02x\n",
		 ep->name, bus_nr, ep->addr);
}

static int pcba_ids_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct pcba_priv *priv;
	struct device_node *eeprom_np;
	struct i2c_client *eeprom_client;
	const char *eeprom_name;
	u32 bus_nr = 0, addr = 0;
	int ret, i;

	dev_info(dev, "[PCBA_IDS] Probing quasar_pcba_ids driver...\n");

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

		if (priv->num_eeproms >= MAX_EEPROM_COUNT) {
			of_node_put(eeprom_np);
			break;
		}

		eeprom_client = of_find_i2c_device_by_node(eeprom_np);
		of_node_put(eeprom_np);
		if (!eeprom_client) {
			dev_warn(dev, "[PCBA_IDS] eeprom[%d] phandle i2c client not ready yet\n", i);
			continue;
		}

		priv->eeproms[priv->num_eeproms].client = eeprom_client;
		priv->eeproms[priv->num_eeproms].adap = eeprom_client->adapter;
		priv->eeproms[priv->num_eeproms].addr = eeprom_client->addr;
		priv->eeproms[priv->num_eeproms].size = EEPROM_READ_SIZE;

		if (of_property_read_string_index(np, "eeprom-names", i, &eeprom_name) == 0) {
			snprintf(priv->eeproms[priv->num_eeproms].name,
				 sizeof(priv->eeproms[priv->num_eeproms].name),
				 "%s", eeprom_name);
		} else {
			snprintf(priv->eeproms[priv->num_eeproms].name,
				 sizeof(priv->eeproms[priv->num_eeproms].name),
				 "eeprom%d", priv->num_eeproms);
		}

		dev_info(dev, "[PCBA_IDS] Linked phandle EEPROM '%s' @ 0x%02x\n",
			 priv->eeproms[priv->num_eeproms].name,
			 eeprom_client->addr);
		priv->num_eeproms++;
	}

	/* Also support i2c-bus / *-eeprom-addr DT properties */
	if (priv->num_eeproms == 0) {
		of_property_read_u32(np, "i2c-bus", &bus_nr);
		if (of_property_read_u32(np, "top-eeprom-addr", &addr) == 0)
			pcba_add_bus_eeprom(priv, bus_nr, addr, "top");
		if (of_property_read_u32(np, "main-eeprom-addr", &addr) == 0)
			pcba_add_bus_eeprom(priv, 2, addr, "main");
		if (of_property_read_u32(np, "led-eeprom-addr", &addr) == 0)
			pcba_add_bus_eeprom(priv, 2, addr, "led");
	}

	parse_eeproms(priv);

	ret = sysfs_create_group(&dev->kobj, &pcba_ids_drv_group);
	if (ret) {
		dev_err(dev, "[PCBA_IDS] ERROR: sysfs creation failed: %d\n", ret);
		return ret;
	}

	dev_info(dev, "[PCBA_IDS] Probed OK (eeproms=%d, parsed=%d)\n",
		 priv->num_eeproms, priv->parsed_count);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void pcba_ids_remove(struct platform_device *pdev)
#else
static int pcba_ids_remove(struct platform_device *pdev)
#endif
{
	struct pcba_priv *priv = platform_get_drvdata(pdev);
	int i;

	sysfs_remove_group(&pdev->dev.kobj, &pcba_ids_drv_group);
	if (priv) {
		for (i = 0; i < priv->num_eeproms; i++) {
			if (!priv->eeproms[i].client && priv->eeproms[i].adap)
				i2c_put_adapter(priv->eeproms[i].adap);
		}
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static const struct of_device_id pcba_ids_of_match[] = {
	{ .compatible = "pcba-ids" },
	{ .compatible = "yandex,pcba-ids" },
	{ }
};
MODULE_DEVICE_TABLE(of, pcba_ids_of_match);

static struct platform_driver pcba_ids_driver = {
	.probe	= pcba_ids_probe,
	.remove	= pcba_ids_remove,
	.driver	= {
		.name		= PCBA_IDS_DRV_NAME,
		.of_match_table	= pcba_ids_of_match,
	},
};
module_platform_driver(pcba_ids_driver);

MODULE_DESCRIPTION("Quasar PCBA ID eeproms parsing driver");
MODULE_LICENSE("GPL");
