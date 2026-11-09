// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/snd-soc-sy6045s.c
 *
 * ASoC driver for Silergy SY6045S digital speaker amplifier (Yandex Station Max
 * Tweeters @ 0x2a and Woofer @ 0x2b).
 *
 * Reconstructed from snd-soc-sy6045s.ko (4.9.113) and ported to Mainline Linux 7.0
 * (snd_soc_component_driver API) with 4.9 fallback.
 *
 * Copyright (C) Silergy Corp. / Yandex Inc.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/ctype.h>
#include <linux/crc32.h>
#include <linux/sysfs.h>
#include <crypto/hash.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define SY6045S_DRV_NAME	"sy6045s"

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose SY6045S amplifier diagnostics and register logging");

/* Register byte widths (1..20 bytes) indexed by 8-bit register address */
static const u8 sy6045s_reg_size[256] = {
	/* 0x00 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x08 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x10 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x18 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x20 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x28 */ 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x01, 0x14,
	/* 0x30 */ 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	/* 0x38 */ 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	/* 0x40 */ 0x14, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x14,
	/* 0x48 */ 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	/* 0x50 */ 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x0c,
	/* 0x58 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x12, 0x12, 0x04,
	/* 0x60 */ 0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	/* 0x68 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
	/* 0x70 */ 0x01, 0x04, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0x78 */ 0x03, 0x04, 0x01, 0x01, 0x04, 0x04, 0x04, 0x01,
	/* 0x80 */ 0x03, 0x03, 0x04, 0x01, 0x01, 0x04, 0x04, 0x01,
	/* 0x88 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03,
	/* 0x90 */ 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x06,
	/* 0x98 */ 0x03, 0x03, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03,
	/* 0xa0 */ 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xa8 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xb0 */ 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xb8 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xc0 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xc8 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x06,
	/* 0xd0 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xd8 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xe0 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xe8 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xf0 */ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	/* 0xf8 */ 0x04, 0x04, 0x01, 0x04, 0x04, 0x04, 0x04, 0x04,
};

struct sy6045s_restore_reg {
	u32 reg;
	u32 len;
	u32 mask;
	u32 val;
};

struct sy6045s_reg_switch {
	unsigned int reg;
	unsigned int mask;
	unsigned int on_val;
	unsigned int off_val;
};

struct sy6045s_priv {
	struct regmap *regmap;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct snd_soc_component *component;
#else
	struct snd_soc_codec *codec;
#endif
	struct i2c_client *client;
	unsigned int dai_fmt;
	const char *fw_name;
	char settings_file[256];
	u8 settings_md5[16];
	u32 settings_crc32;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *fault_gpio;
	struct work_struct spk_off_work;
	struct work_struct spk_on_work;
	struct regulator_bulk_data supplies[2];
	struct mutex io_lock;
	bool pbtl_mode;
	bool playing;
	unsigned long i2c_writes;
	unsigned long i2c_errors;
	struct sy6045s_restore_reg *restore_regs;
	int num_restore_regs;
};

static const char *sy6045s_role_name(struct sy6045s_priv *sy)
{
	if (!sy || !sy->client)
		return "unknown";
	if (sy->client->addr == 0x2a)
		return "Tweeters";
	if (sy->client->addr == 0x2b)
		return "Woofer";
	return sy->pbtl_mode ? "Woofer(PBTL)" : "Stereo(BTL)";
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static inline int sy6045s_soc_write(struct sy6045s_priv *sy,
				    unsigned int reg, unsigned int val)
{
	return regmap_write(sy->regmap, reg, val);
}

static inline int sy6045s_soc_update_bits(struct sy6045s_priv *sy,
					  unsigned int reg,
					  unsigned int mask, unsigned int val)
{
	return regmap_update_bits(sy->regmap, reg, mask, val);
}
#else
static inline int sy6045s_soc_write(struct sy6045s_priv *sy,
				    unsigned int reg, unsigned int val)
{
	return snd_soc_write(sy->codec, reg, val);
}

static inline int sy6045s_soc_update_bits(struct sy6045s_priv *sy,
					  unsigned int reg,
					  unsigned int mask, unsigned int val)
{
	return snd_soc_update_bits(sy->codec, reg, mask, val);
}
#endif

static bool sy6045s_reg_volatile(struct device *dev, unsigned int reg)
{
	return (reg <= 0x02 || reg == 0x0f);
}

static void sy6045s_reg_lock(void *context)
{
	struct sy6045s_priv *sy = context;

	mutex_lock(&sy->io_lock);
}

static void sy6045s_reg_unlock(void *context)
{
	struct sy6045s_priv *sy = context;

	mutex_unlock(&sy->io_lock);
}

static int sy6045s_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	struct sy6045s_priv *sy = i2c_get_clientdata(client);
	u8 buf[5];
	u8 sz;
	int i, shift, ret;

	if (reg > 0xff)
		return -EINVAL;

	sz = sy6045s_reg_size[reg];
	if (!sz)
		return -EINVAL;
	if (sz > 4)
		sz = 4;

	buf[0] = (u8)reg;
	shift = (sz - 1) * 8;
	for (i = 0; i < sz; i++, shift -= 8)
		buf[1 + i] = (val >> shift) & 0xff;

	ret = i2c_master_send(client, buf, sz + 1);
	if (ret == sz + 1) {
		if (sy)
			sy->i2c_writes++;
		return 0;
	}
	if (sy)
		sy->i2c_errors++;
	dev_err(&client->dev,
		"[SY6045S:%s] I2C write failed reg=0x%02x val=0x%x (sz=%u): ret=%d\n",
		sy6045s_role_name(sy), reg, val, sz, ret);
	if (ret < 0)
		return ret;
	return -EIO;
}

static int sy6045s_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	struct sy6045s_priv *sy = i2c_get_clientdata(client);
	struct i2c_msg msgs[2];
	u8 reg_addr = (u8)reg;
	u8 data[4] = { 0 };
	u8 sz;
	unsigned int v = 0;
	int i, shift, ret;

	if (reg > 0xff)
		return -EINVAL;

	sz = sy6045s_reg_size[reg];
	if (!sz)
		return -EINVAL;
	if (sz > 4)
		sz = 4;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg_addr;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sz;
	msgs[1].buf = data;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		if (sy)
			sy->i2c_errors++;
		dev_err(&client->dev,
			"[SY6045S:%s] I2C read failed reg=0x%02x (sz=%u): ret=%d\n",
			sy6045s_role_name(sy), reg, sz, ret);
		return ret < 0 ? ret : -EIO;
	}

	shift = (sz - 1) * 8;
	for (i = 0; i < sz; i++, shift -= 8)
		v |= (unsigned int)data[i] << shift;

	*val = v;
	return 0;
}

/* Custom DRC switch control (4-byte register 0x60) */
static const struct sy6045s_reg_switch drc_switch_params = {
	.reg = 0x60,
	.mask = 0x0f,
	.on_val = 0x0f,
	.off_val = 0x00,
};

static int sy6045s_reg_switch_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int sy6045s_reg_switch_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(comp);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(codec);
#endif
	const struct sy6045s_reg_switch *sw =
		(const struct sy6045s_reg_switch *)kcontrol->private_value;
	unsigned int val = 0;

	regmap_read(sy->regmap, sw->reg, &val);
	ucontrol->value.integer.value[0] = ((val & sw->mask) != sw->off_val);
	return 0;
}

static int sy6045s_reg_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(comp);
#else
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(codec);
#endif
	const struct sy6045s_reg_switch *sw =
		(const struct sy6045s_reg_switch *)kcontrol->private_value;
	unsigned int val = ucontrol->value.integer.value[0] ?
			   sw->on_val : sw->off_val;

	return sy6045s_soc_update_bits(sy, sw->reg, sw->mask, val);
}

static const DECLARE_TLV_DB_SCALE(sy6045s_vol_tlv_m, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(sy6045s_vol_tlv, -7950, 50, 1);

static const struct snd_kcontrol_new sy6045s_snd_controls[] = {
	SOC_SINGLE_TLV("Master Playback Volume", 0x07, 0, 0xff, 0,
		       sy6045s_vol_tlv_m),
	SOC_SINGLE_TLV("Ch1 Playback Volume", 0x08, 0, 0xff, 0,
		       sy6045s_vol_tlv),
	SOC_SINGLE_TLV("Ch2 Playback Volume", 0x09, 0, 0xff, 0,
		       sy6045s_vol_tlv),
	SOC_SINGLE("Master Playback Switch", 0x06, 0, 3, 1),
	SOC_SINGLE("Ch1 Playback Switch", 0x06, 0, 1, 1),
	SOC_SINGLE("Ch2 Playback Switch", 0x06, 1, 1, 1),
	SOC_SINGLE("EQ Switch", 0x04, 4, 1, 0),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DRC Switch",
		.info = sy6045s_reg_switch_info,
		.get = sy6045s_reg_switch_get,
		.put = sy6045s_reg_switch_put,
		.private_value = (unsigned long)&drc_switch_params,
	},
};

static void sy6045s_spk_off_work(struct work_struct *work)
{
	struct sy6045s_priv *sy = container_of(work, struct sy6045s_priv,
					       spk_off_work);

	if (debug)
		dev_info(&sy->client->dev,
			 "[SY6045S:%s] spk_off_work -> entering standby (reg 0x22 |= 0x10)\n",
			 sy6045s_role_name(sy));
	sy6045s_soc_update_bits(sy, 0x22, 0x10, 0x10);
}

static void sy6045s_spk_on_work(struct work_struct *work)
{
	struct sy6045s_priv *sy = container_of(work, struct sy6045s_priv,
					       spk_on_work);

	if (debug)
		dev_info(&sy->client->dev,
			 "[SY6045S:%s] spk_on_work -> exiting standby (reg 0x22 &= ~0x10)\n",
			 sy6045s_role_name(sy));
	sy6045s_soc_update_bits(sy, 0x22, 0x10, 0x00);
}

static void sy6045s_set_sby_mode_on(struct sy6045s_priv *sy)
{
	if (sy->fault_gpio)
		gpiod_set_value_cansleep(sy->fault_gpio, 0);
	else
		sy6045s_soc_update_bits(sy, 0x22, 0x10, 0x10);
}

static int sy6045s_save_regs(struct sy6045s_priv *sy)
{
	int i, ret;

	for (i = 0; i < sy->num_restore_regs; i++) {
		struct sy6045s_restore_reg *r = &sy->restore_regs[i];

		r->val = 0;
		ret = i2c_smbus_read_i2c_block_data(sy->client, r->reg,
						    r->len, (u8 *)&r->val);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int sy6045s_restore_regs(struct sy6045s_priv *sy)
{
	int i, ret;

	for (i = 0; i < sy->num_restore_regs; i++) {
		struct sy6045s_restore_reg *r = &sy->restore_regs[i];
		u32 cur = 0;

		ret = i2c_smbus_read_i2c_block_data(sy->client, r->reg & 0xff,
						    r->len, (u8 *)&cur);
		if (ret < 0)
			return ret;

		cur = (cur & ~r->mask) | (r->val & r->mask);
		ret = i2c_smbus_write_i2c_block_data(sy->client, r->reg & 0xff,
						     r->len, (const u8 *)&cur);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int sy6045s_reset_chip(struct sy6045s_priv *sy)
{
	int ret;

	ret = i2c_smbus_write_byte_data(sy->client, 0x0f, 0x01);
	if (ret < 0) {
		sy->i2c_errors++;
		dev_err(&sy->client->dev,
			"[SY6045S:%s] soft reset write to 0x0f failed: %d\n",
			sy6045s_role_name(sy), ret);
		return ret;
	}
	msleep(20);
	return 0;
}

static int sy6045s_write_block(struct sy6045s_priv *sy, u8 reg,
			       u8 len, const u8 *data)
{
	int ret = i2c_smbus_write_i2c_block_data(sy->client, reg, len, data);

	if (ret < 0) {
		sy->i2c_errors++;
		dev_err(&sy->client->dev,
			"[SY6045S:%s] block write failed reg=0x%02x len=%u: %d\n",
			sy6045s_role_name(sy), reg, len, ret);
	} else {
		sy->i2c_writes++;
	}
	return ret;
}

static int sy6045s_apply_settings(struct sy6045s_priv *sy, const char *data,
				  size_t size, const char *name)
{
	const char *p = data;
	const char *end = data + size;
	u8 block[36];
	int block_len = 0;
	int line_num = 1;
	int blocks_written = 0;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char c = *p;

		if (c == '#' || c == 'd' || c == '\n' || c == '\r') {
			if (block_len > 0) {
				sy6045s_write_block(sy, block[0],
						    block_len - 1, &block[1]);
				blocks_written++;
				block_len = 0;
			}
		} else if (c == 'w') {
			size_t pos = 1;

			if (block_len > 0) {
				sy6045s_write_block(sy, block[0],
						    block_len - 1, &block[1]);
				blocks_written++;
				block_len = 0;
			}

			while (pos + 2 < len) {
				u8 byte_val = 0;
				char hex[3];

				if (p[pos] == '\r')
					break;
				if (p[pos] != ' ')
					return -EINVAL;
				hex[0] = p[pos + 1];
				hex[1] = p[pos + 2];
				hex[2] = '\0';
				if (!isxdigit(hex[0]) || !isxdigit(hex[1]) ||
				    block_len >= 32 ||
				    sscanf(hex, "%02hhx", &byte_val) != 1)
					return -EINVAL;
				block[block_len++] = byte_val;
				pos += 3;
			}
		} else if (c == '>') {
			u8 byte_val = 0;
			char hex[3];

			if (block_len == 0 || block_len > 32 || len < 4)
				return -EINVAL;
			hex[0] = p[2];
			hex[1] = p[3];
			hex[2] = '\0';
			if (sscanf(hex, "%02hhx", &byte_val) != 1)
				return -EINVAL;
			block[block_len++] = byte_val;
		} else {
			dev_err(&sy->client->dev,
				"[SY6045S:%s] invalid settings at line %d\n",
				sy6045s_role_name(sy), line_num);
			return -EINVAL;
		}

		p += len + (nl ? 1 : 0);
		line_num++;
	}

	if (block_len > 0) {
		sy6045s_write_block(sy, block[0],
				    block_len - 1, &block[1]);
		blocks_written++;
	}

	if (name)
		strscpy(sy->settings_file, name, sizeof(sy->settings_file));
	sy->settings_crc32 = ~crc32_le(~0, (const u8 *)data, size);

	dev_info(&sy->client->dev,
		 "[SY6045S:%s] applied settings '%s' (%zu bytes, %d blocks, crc32=0x%08x)\n",
		 sy6045s_role_name(sy), name ? name : "unnamed",
		 size, blocks_written, sy->settings_crc32);
	return 0;
}

static int sy6045s_load_firmware(struct sy6045s_priv *sy, const char *fw_name)
{
	const struct firmware *fw;
	int ret;

	if (!fw_name) {
		dev_info(&sy->client->dev,
			 "[SY6045S:%s] no firmware filename configured\n",
			 sy6045s_role_name(sy));
		return 0;
	}

	ret = request_firmware(&fw, fw_name, &sy->client->dev);
	if (ret) {
		dev_warn(&sy->client->dev,
			 "[SY6045S:%s] firmware '%s' not found (%d), using hardware defaults\n",
			 sy6045s_role_name(sy), fw_name, ret);
		return 0;
	}

	ret = sy6045s_apply_settings(sy, (const char *)fw->data, fw->size,
				     fw_name);
	release_firmware(fw);
	return ret;
}

static int sy6045s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(dai->component);
#else
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(dai->codec);
#endif

	sy->dai_fmt = fmt;
	if (debug)
		dev_info(&sy->client->dev,
			 "[SY6045S:%s] set_dai_fmt: fmt=0x%08x\n",
			 sy6045s_role_name(sy), fmt);
	return 0;
}

static int sy6045s_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(dai->component);
#else
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(dai->codec);
#endif
	unsigned int fmt = sy->dai_fmt;
	unsigned int val = 0;
	bool is_i2s = false;
	int ret;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = 0x00;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val = 0x20;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = 0x40;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		val = 0x60;
		break;
	default:
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		is_i2s = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= 0x08;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= 0x04;
		break;
	default:
		break;
	}

	switch (params_width(params)) {
	case 16:
		val |= 0x03;
		if (is_i2s)
			val |= 0x10;
		break;
	case 24:
		val |= 0x01;
		if (is_i2s)
			val |= 0x10;
		break;
	case 32:
		if (is_i2s)
			val |= 0x10;
		break;
	default:
		break;
	}

	ret = sy6045s_soc_write(sy, 0x15, val);
	if (debug)
		dev_info(&sy->client->dev,
			 "[SY6045S:%s] hw_params: rate=%u width=%d ch=%u -> reg 0x15=0x%02x (ret=%d)\n",
			 sy6045s_role_name(sy), params_rate(params),
			 params_width(params), params_channels(params),
			 val, ret);
	return ret;
}

static int sy6045s_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(dai->component);
#else
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(dai->codec);
#endif

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sy->playing = true;
		if (sy->fault_gpio)
			gpiod_set_value(sy->fault_gpio, 1);
		else
			queue_work(system_highpri_wq, &sy->spk_on_work);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sy->playing = false;
		if (sy->fault_gpio)
			gpiod_set_value(sy->fault_gpio, 0);
		else
			queue_work(system_highpri_wq, &sy->spk_off_work);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops sy6045s_dai_ops = {
	.set_fmt = sy6045s_set_dai_fmt,
	.hw_params = sy6045s_hw_params,
	.trigger = sy6045s_trigger,
};

static struct snd_soc_dai_driver sy6045s_dai = {
	.name = "sy6045s-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &sy6045s_dai_ops,
};

static int sy6045s_init_chip(struct sy6045s_priv *sy)
{
	int ret;

	dev_info(&sy->client->dev,
		 "[SY6045S:%s] Initializing chip (pbtl=%d, fw='%s')...\n",
		 sy6045s_role_name(sy), sy->pbtl_mode,
		 sy->fw_name ? sy->fw_name : "none");

	mutex_lock(&sy->io_lock);
	ret = sy6045s_load_firmware(sy, sy->fw_name);
	mutex_unlock(&sy->io_lock);
	if (ret < 0)
		return ret;

	if (sy->pbtl_mode) {
		ret = sy6045s_soc_write(sy, 0x1e, 0x07);
		if (ret < 0)
			goto out;
		ret = sy6045s_soc_write(sy, 0x20, 0x80);
	} else {
		ret = sy6045s_soc_write(sy, 0x1e, 0x05);
		if (ret < 0)
			goto out;
		ret = sy6045s_soc_write(sy, 0x20, 0x00);
	}

out:
	sy6045s_set_sby_mode_on(sy);
	dev_info(&sy->client->dev,
		 "[SY6045S:%s] Chip init complete (ret=%d, i2c_writes=%lu, i2c_errors=%lu)\n",
		 sy6045s_role_name(sy), ret, sy->i2c_writes, sy->i2c_errors);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static int sy6045s_component_probe(struct snd_soc_component *component)
{
	struct sy6045s_priv *sy = snd_soc_component_get_drvdata(component);

	sy->component = component;
	snd_soc_component_init_regmap(component, sy->regmap);
	return sy6045s_init_chip(sy);
}

static void sy6045s_component_remove(struct snd_soc_component *component)
{
}

static const struct snd_soc_component_driver soc_component_dev_sy6045s = {
	.probe = sy6045s_component_probe,
	.remove = sy6045s_component_remove,
	.controls = sy6045s_snd_controls,
	.num_controls = ARRAY_SIZE(sy6045s_snd_controls),
};
#else
static int sy6045s_codec_probe(struct snd_soc_codec *codec)
{
	struct sy6045s_priv *sy = snd_soc_codec_get_drvdata(codec);

	sy->codec = codec;
	return sy6045s_init_chip(sy);
}

static int sy6045s_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static const struct snd_soc_codec_driver soc_codec_dev_sy6045s = {
	.probe = sy6045s_codec_probe,
	.remove = sy6045s_codec_remove,
	.component_driver = {
		.controls = sy6045s_snd_controls,
		.num_controls = ARRAY_SIZE(sy6045s_snd_controls),
	},
};
#endif

/* Sysfs attributes */
static ssize_t settings_file_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sy6045s_priv *sy = dev_get_drvdata(dev);

	if (!sy->settings_file[0])
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%s\n%*phN\n%08x\n",
			 sy->settings_file, 16, sy->settings_md5,
			 sy->settings_crc32);
}

static ssize_t settings_file_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sy6045s_priv *sy = dev_get_drvdata(dev);
	char fname[256];
	int ret;

	if (sscanf(buf, "%255s", fname) != 1)
		return -EINVAL;

	mutex_lock(&sy->io_lock);
	sy6045s_save_regs(sy);
	sy6045s_reset_chip(sy);
	ret = sy6045s_load_firmware(sy, fname);
	sy6045s_restore_regs(sy);
	mutex_unlock(&sy->io_lock);
	regcache_drop_region(sy->regmap, 0x00, 0xff);

	return ret < 0 ? ret : count;
}

static ssize_t settings_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct sy6045s_priv *sy = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&sy->io_lock);
	ret = sy6045s_apply_settings(sy, buf, count, "runtime");
	mutex_unlock(&sy->io_lock);

	return ret < 0 ? ret : count;
}

static ssize_t default_settings_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct sy6045s_priv *sy = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&sy->io_lock);
	sy6045s_save_regs(sy);
	sy6045s_reset_chip(sy);
	ret = sy6045s_load_firmware(sy, sy->fw_name);
	sy6045s_restore_regs(sy);
	mutex_unlock(&sy->io_lock);
	regcache_drop_region(sy->regmap, 0x00, 0xff);

	return ret < 0 ? ret : count;
}

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct sy6045s_priv *sy = dev_get_drvdata(dev);
	unsigned int r00 = 0, r06 = 0, r07 = 0, r15 = 0, r22 = 0;

	if (sy->regmap) {
		regmap_read(sy->regmap, 0x00, &r00);
		regmap_read(sy->regmap, 0x06, &r06);
		regmap_read(sy->regmap, 0x07, &r07);
		regmap_read(sy->regmap, 0x15, &r15);
		regmap_read(sy->regmap, 0x22, &r22);
	}

	return scnprintf(buf, PAGE_SIZE,
			 "role=%s addr=0x%02x pbtl=%d playing=%d fw=%s loaded=%s crc32=0x%08x reg00=0x%02x mute(06)=0x%02x mvol(07)=0x%02x i2s(15)=0x%02x sby(22)=0x%02x writes=%lu errors=%lu\n",
			 sy6045s_role_name(sy), sy->client->addr,
			 sy->pbtl_mode, sy->playing,
			 sy->fw_name ? sy->fw_name : "none",
			 sy->settings_file[0] ? sy->settings_file : "none",
			 sy->settings_crc32,
			 r00, r06, r07, r15, r22,
			 sy->i2c_writes, sy->i2c_errors);
}

static DEVICE_ATTR_RW(settings_file);
static DEVICE_ATTR_WO(settings);
static DEVICE_ATTR_WO(default_settings);
static DEVICE_ATTR_RO(status);

static struct attribute *sy6045s_attrs[] = {
	&dev_attr_settings.attr,
	&dev_attr_settings_file.attr,
	&dev_attr_default_settings.attr,
	&dev_attr_status.attr,
	NULL,
};

static const struct attribute_group sy6045s_attrs_group = {
	.attrs = sy6045s_attrs,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int sy6045s_i2c_probe(struct i2c_client *client)
#else
static int sy6045s_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct sy6045s_priv *sy;
	struct regmap_config regmap_cfg;
	unsigned int dev_id = 0;
	int count, i, ret;

	sy = devm_kzalloc(dev, sizeof(*sy), GFP_KERNEL);
	if (!sy)
		return -ENOMEM;

	INIT_WORK(&sy->spk_off_work, sy6045s_spk_off_work);
	INIT_WORK(&sy->spk_on_work, sy6045s_spk_on_work);
	mutex_init(&sy->io_lock);

	sy->client = client;
	i2c_set_clientdata(client, sy);

	of_property_read_string(dev->of_node, "firmware", &sy->fw_name);
	sy->pbtl_mode = of_property_read_bool(dev->of_node, "pbtl-mode");

	dev_info(dev, "[SY6045S:%s] Probing at I2C 0x%02x (pbtl=%d, fw='%s')...\n",
		 sy6045s_role_name(sy), client->addr, sy->pbtl_mode,
		 sy->fw_name ? sy->fw_name : "none");

	memset(&regmap_cfg, 0, sizeof(regmap_cfg));
	regmap_cfg.reg_bits = 8;
	regmap_cfg.val_bits = 32;
	regmap_cfg.max_register = 0xff;
	regmap_cfg.reg_read = sy6045s_reg_read;
	regmap_cfg.reg_write = sy6045s_reg_write;
	regmap_cfg.volatile_reg = sy6045s_reg_volatile;
	regmap_cfg.lock = sy6045s_reg_lock;
	regmap_cfg.unlock = sy6045s_reg_unlock;
	regmap_cfg.lock_arg = sy;
	regmap_cfg.cache_type = REGCACHE_RBTREE;

	sy->regmap = devm_regmap_init(dev, NULL, client, &regmap_cfg);
	if (IS_ERR(sy->regmap))
		return PTR_ERR(sy->regmap);

	sy->supplies[0].supply = "vddio";
	sy->supplies[1].supply = "pvdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sy->supplies),
				      sy->supplies);
	if (ret == 0) {
		ret = regulator_bulk_enable(ARRAY_SIZE(sy->supplies),
					    sy->supplies);
		if (ret) {
			dev_err(dev, "[SY6045S:%s] Failed to enable regulators: %d\n",
				sy6045s_role_name(sy), ret);
			return ret;
		}
		dev_info(dev, "[SY6045S:%s] Enabled vddio and pvdd regulators\n",
			 sy6045s_role_name(sy));
	}

	sy->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sy->reset_gpio)) {
		ret = PTR_ERR(sy->reset_gpio);
		dev_err(dev, "[SY6045S:%s] Failed to get reset GPIO: %d\n",
			sy6045s_role_name(sy), ret);
		return ret;
	}
	if (sy->reset_gpio) {
		msleep(20);
		gpiod_set_value_cansleep(sy->reset_gpio, 1);
		msleep(20);
		dev_info(dev, "[SY6045S:%s] Released reset GPIO\n",
			 sy6045s_role_name(sy));
	}

	sy->fault_gpio = devm_gpiod_get_optional(dev, "fault", GPIOD_OUT_HIGH);
	if (IS_ERR(sy->fault_gpio))
		sy->fault_gpio = NULL;

	/* Verify physical I2C communication by reading reg 0x00 */
	ret = regmap_read(sy->regmap, 0x00, &dev_id);
	if (ret == 0)
		dev_info(dev, "[SY6045S:%s] I2C communication OK (reg 0x00 = 0x%02x)\n",
			 sy6045s_role_name(sy), dev_id);
	else
		dev_err(dev, "[SY6045S:%s] WARNING: I2C read reg 0x00 failed (%d)! Check 20V_AMPL / 3V3_D / reset GPIO!\n",
			sy6045s_role_name(sy), ret);

	count = of_property_count_elems_of_size(dev->of_node, "restore-regs",
						sizeof(u32));
	if (count > 0 && (count % 3) == 0) {
		u32 *tmp = kcalloc(count, sizeof(u32), GFP_KERNEL);

		if (tmp) {
			sy->num_restore_regs = count / 3;
			sy->restore_regs = devm_kcalloc(dev,
							sy->num_restore_regs,
							sizeof(*sy->restore_regs),
							GFP_KERNEL);
			if (sy->restore_regs &&
			    of_property_read_u32_array(dev->of_node,
						       "restore-regs",
						       tmp, count) == 0) {
				for (i = 0; i < sy->num_restore_regs; i++) {
					sy->restore_regs[i].reg = tmp[i * 3];
					sy->restore_regs[i].len = tmp[i * 3 + 1];
					sy->restore_regs[i].mask = tmp[i * 3 + 2];
				}
			}
			kfree(tmp);
		}
	}

	ret = sysfs_create_group(&dev->kobj, &sy6045s_attrs_group);
	if (ret)
		dev_warn(dev, "[SY6045S:%s] failed to create sysfs group: %d\n",
			 sy6045s_role_name(sy), ret);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	return devm_snd_soc_register_component(dev, &soc_component_dev_sy6045s,
					       &sy6045s_dai, 1);
#else
	return snd_soc_register_codec(dev, &soc_codec_dev_sy6045s,
				      &sy6045s_dai, 1);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void sy6045s_i2c_remove(struct i2c_client *client)
#else
static int sy6045s_i2c_remove(struct i2c_client *client)
#endif
{
	struct sy6045s_priv *sy = i2c_get_clientdata(client);

	if (sy) {
		cancel_work_sync(&sy->spk_off_work);
		cancel_work_sync(&sy->spk_on_work);
		sy6045s_set_sby_mode_on(sy);
		sysfs_remove_group(&client->dev.kobj, &sy6045s_attrs_group);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
		snd_soc_unregister_codec(&client->dev);
#endif
		regulator_bulk_disable(ARRAY_SIZE(sy->supplies), sy->supplies);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id sy6045s_i2c_id[] = {
	{ "sy6045s", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sy6045s_i2c_id);

static const struct of_device_id sy6045s_of_match[] = {
	{ .compatible = "silergy,sy6045s" },
	{ }
};
MODULE_DEVICE_TABLE(of, sy6045s_of_match);

static struct i2c_driver sy6045s_i2c_driver = {
	.driver = {
		.name = SY6045S_DRV_NAME,
		.of_match_table = sy6045s_of_match,
	},
	.probe = sy6045s_i2c_probe,
	.remove = sy6045s_i2c_remove,
	.id_table = sy6045s_i2c_id,
};
module_i2c_driver(sy6045s_i2c_driver);

MODULE_AUTHOR("Bin Zhang <bin.zhang@silergycorp.com>");
MODULE_DESCRIPTION("ASoC driver for SY6045S");
MODULE_LICENSE("GPL v2");
