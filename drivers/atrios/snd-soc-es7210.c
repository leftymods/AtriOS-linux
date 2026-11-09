// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/snd-soc-es7210.c
 *
 * Everest ES7210 2/4-channel audio ADC ASoC driver for Yandex Station Max
 * loopback / reference capture (@ 0x40 on i2c2).
 *
 * Reconstructed from snd-soc-es7210.ko (4.9.113) and ported to Mainline Linux 7.0.
 *
 * Copyright (C) Everest Semiconductor Co., Ltd
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define ES7210_DRV_NAME		"es7210"

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose ES7210 ADC diagnostics");

static const u8 es7210_reg_init_seq[][2] = {
	{ 0x00, 0x32 }, { 0x01, 0x20 }, { 0x02, 0x02 }, { 0x03, 0x04 },
	{ 0x04, 0x01 }, { 0x05, 0x00 }, { 0x06, 0x00 }, { 0x07, 0x20 },
	{ 0x08, 0x10 }, { 0x09, 0x40 }, { 0x0a, 0x40 }, { 0x0b, 0x00 },
	{ 0x0c, 0x00 }, { 0x0d, 0x09 }, { 0x0e, 0x00 }, { 0x0f, 0x00 },
	{ 0x10, 0x00 }, { 0x11, 0x00 }, { 0x12, 0x00 }, { 0x13, 0x00 },
	{ 0x14, 0x00 }, { 0x15, 0x00 }, { 0x16, 0x00 }, { 0x17, 0x00 },
	{ 0x18, 0xf7 }, { 0x19, 0xf7 }, { 0x1a, 0x00 }, { 0x1b, 0xbf },
	{ 0x1c, 0xbf }, { 0x1d, 0xbf }, { 0x1e, 0xbf }, { 0x1f, 0x00 },
	{ 0x20, 0x26 }, { 0x21, 0x26 }, { 0x22, 0x06 }, { 0x23, 0x26 },
	{ 0x3d, 0x72 }, { 0x3e, 0x10 }, { 0x3f, 0x00 }, { 0x40, 0x80 },
	{ 0x41, 0x71 }, { 0x42, 0x71 }, { 0x43, 0x00 }, { 0x44, 0x00 },
	{ 0x45, 0x00 }, { 0x46, 0x00 }, { 0x47, 0x00 }, { 0x48, 0x00 },
	{ 0x49, 0x00 }, { 0x4a, 0x00 }, { 0x4b, 0xff }, { 0x4c, 0xff },
};

static const u8 es7210_tdm_reg_common_cfg[][2] = {
	{ 0x00, 0x41 }, { 0x02, 0xc1 }, { 0x06, 0x04 }, { 0x07, 0x20 },
	{ 0x09, 0x30 }, { 0x0a, 0x30 }, { 0x23, 0x26 }, { 0x22, 0x06 },
	{ 0x21, 0x26 }, { 0x20, 0x06 }, { 0x40, 0xc3 }, { 0x41, 0x70 },
	{ 0x42, 0x70 }, { 0x43, 0x10 }, { 0x44, 0x10 }, { 0x45, 0x10 },
	{ 0x46, 0x10 }, { 0x47, 0x08 }, { 0x48, 0x08 }, { 0x49, 0x08 },
	{ 0x4a, 0x08 },
};

struct es7210_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	u8 channels;
	u16 chip_id;
	bool hw_inited;
	bool muted;
	struct delayed_work pcm_pop_work;
};

static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, 0, 300, 0);
static const DECLARE_TLV_DB_SCALE(adc_gain_tlv, -9550, 50, 0);

static const struct snd_kcontrol_new es7210_snd_controls[] = {
	SOC_SINGLE_TLV("PGA1_setting", 0x43, 0, 0x0e, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("PGA2_setting", 0x44, 0, 0x0e, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("PGA3_setting", 0x45, 0, 0x0e, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("PGA4_setting", 0x46, 0, 0x0e, 0, mic_boost_tlv),
	SOC_SINGLE("ADC1_MUTE", 0x15, 0, 1, 0),
	SOC_SINGLE("ADC2_MUTE", 0x15, 1, 1, 0),
	SOC_SINGLE("ADC3_MUTE", 0x14, 0, 1, 0),
	SOC_SINGLE("ADC4_MUTE", 0x14, 1, 1, 0),
	SOC_SINGLE_TLV("ADC1_MAX_GAIN", 0x1b, 0, 0xff, 0, adc_gain_tlv),
	SOC_SINGLE_TLV("ADC2_MAX_GAIN", 0x1c, 0, 0xff, 0, adc_gain_tlv),
	SOC_SINGLE_TLV("ADC3_MAX_GAIN", 0x1d, 0, 0xff, 0, adc_gain_tlv),
	SOC_SINGLE_TLV("ADC4_MAX_GAIN", 0x1e, 0, 0xff, 0, adc_gain_tlv),
};

static void pcm_pop_work_events(struct work_struct *work)
{
	struct es7210_priv *es = container_of(to_delayed_work(work),
					      struct es7210_priv,
					      pcm_pop_work);

	regmap_update_bits(es->regmap, 0x14, 0x03, 0x00);
	regmap_update_bits(es->regmap, 0x15, 0x03, 0x00);
	es->muted = false;
	if (debug)
		dev_info(&es->client->dev, "[ES7210] pcm_pop_work: unmuted ADC1..4\n");
}

static int es7210_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es7210_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es7210_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif

	if (debug)
		dev_info(&es->client->dev, "[ES7210] pcm_startup: scheduling unmute\n");
	queue_delayed_work(system_dfl_wq, &es->pcm_pop_work, 25);
	return 0;
}

static int es7210_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es7210_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es7210_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif

	if (debug)
		dev_info(&es->client->dev, "[ES7210] set_dai_fmt: fmt=0x%08x\n", fmt);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		regmap_write(es->regmap, 0x11, 0x60);
		regmap_write(es->regmap, 0x12, 0x02);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		regmap_write(es->regmap, 0x11, 0x61);
		regmap_write(es->regmap, 0x12, 0x02);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		regmap_write(es->regmap, 0x11, 0x63);
		regmap_write(es->regmap, 0x12, 0x01);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		regmap_write(es->regmap, 0x11, 0x73);
		regmap_write(es->regmap, 0x12, 0x01);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_BC_FP:
		regmap_update_bits(es->regmap, 0x08, 0x01, 0x01);
		break;
	case SND_SOC_DAIFMT_BP_FP:
	case SND_SOC_DAIFMT_BC_FC:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_IF:
		regmap_update_bits(es->regmap, 0x08, 0x08, 0x08);
		break;
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_IB_NF:
		regmap_update_bits(es->regmap, 0x08, 0x08, 0x00);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es7210_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es7210_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es7210_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif
	unsigned int val = mute ? 0x03 : 0x00;

	es->muted = !!mute;
	if (debug)
		dev_info(&es->client->dev, "[ES7210] mute_stream: mute=%d\n", mute);

	regmap_update_bits(es->regmap, 0x14, 0x03, val);
	regmap_update_bits(es->regmap, 0x15, 0x03, val);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
static int es7210_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return es7210_mute_stream(dai, mute, SNDRV_PCM_STREAM_CAPTURE);
}
#endif

static const struct snd_soc_dai_ops es7210_ops = {
	.startup = es7210_pcm_startup,
	.set_fmt = es7210_set_dai_fmt,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	.mute_stream = es7210_mute_stream,
#else
	.digital_mute = es7210_digital_mute,
#endif
};

static struct snd_soc_dai_driver es7210_dai0 = {
	.name = "ES7210 ADC 2/4CH",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S20_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &es7210_ops,
};

static int es7210_hw_init(struct es7210_priv *es)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(es7210_tdm_reg_common_cfg); i++) {
		regmap_write(es->regmap,
			     es7210_tdm_reg_common_cfg[i][0],
			     es7210_tdm_reg_common_cfg[i][1]);
	}

	if (es->channels == 4) {
		regmap_write(es->regmap, 0x4b, 0x00);
		regmap_write(es->regmap, 0x4c, 0x00);
		regmap_update_bits(es->regmap, 0x08, 0xf0, 0x20);
	} else {
		regmap_write(es->regmap, 0x4b, 0x00);
		regmap_write(es->regmap, 0x4c, 0xff);
		regmap_update_bits(es->regmap, 0x08, 0xf0, 0x10);
	}

	regmap_update_bits(es->regmap, 0x14, 0x03, 0x03);
	regmap_update_bits(es->regmap, 0x15, 0x03, 0x03);
	es->hw_inited = true;
	es->muted = true;

	dev_info(&es->client->dev,
		 "[ES7210] hw_init completed (channels=%u)\n", es->channels);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static int es7210_component_probe(struct snd_soc_component *component)
{
	struct es7210_priv *es = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, es->regmap);
	return es7210_hw_init(es);
}

static const struct snd_soc_component_driver soc_component_dev_es7210 = {
	.probe = es7210_component_probe,
	.controls = es7210_snd_controls,
	.num_controls = ARRAY_SIZE(es7210_snd_controls),
};
#else
static int es7210_codec_probe(struct snd_soc_codec *codec)
{
	struct es7210_priv *es = snd_soc_codec_get_drvdata(codec);

	return es7210_hw_init(es);
}

static const struct snd_soc_codec_driver soc_codec_dev_es7210 = {
	.probe = es7210_codec_probe,
	.component_driver = {
		.controls = es7210_snd_controls,
		.num_controls = ARRAY_SIZE(es7210_snd_controls),
	},
};
#endif

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct es7210_priv *es = dev_get_drvdata(dev);
	unsigned int r08 = 0, r11 = 0, r14 = 0, r15 = 0;

	if (es->regmap) {
		regmap_read(es->regmap, 0x08, &r08);
		regmap_read(es->regmap, 0x11, &r11);
		regmap_read(es->regmap, 0x14, &r14);
		regmap_read(es->regmap, 0x15, &r15);
	}

	return sysfs_emit(buf,
			  "addr=0x%02x chip_id=0x%04x channels=%u hw_inited=%d muted=%d mode(08)=0x%02x sdp(11)=0x%02x mute34(14)=0x%02x mute12(15)=0x%02x\n",
			  es->client->addr, es->chip_id, es->channels,
			  es->hw_inited, es->muted, r08, r11, r14, r15);
}
static DEVICE_ATTR_RO(status);

static const struct regmap_config es7210_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x4c,
	.cache_type = REGCACHE_RBTREE,
};

static const struct of_device_id es7210_dt_ids[] = {
	{ .compatible = "everest,es7210_2ch", .data = (void *)2 },
	{ .compatible = "everest,es7210_4ch", .data = (void *)4 },
	{ .compatible = "everest,es7210", .data = (void *)2 },
	{ }
};
MODULE_DEVICE_TABLE(of, es7210_dt_ids);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int es7210_i2c_probe(struct i2c_client *client)
#else
static int es7210_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	const struct of_device_id *match;
	struct es7210_priv *es;
	int id_hi, id_lo;
	size_t i;
	int ret;

	es = devm_kzalloc(dev, sizeof(*es), GFP_KERNEL);
	if (!es)
		return -ENOMEM;

	es->client = client;

	match = of_match_device(es7210_dt_ids, dev);
	if (match && match->data)
		es->channels = (uintptr_t)match->data;
	else
		es->channels = 2;

	INIT_DELAYED_WORK(&es->pcm_pop_work, pcm_pop_work_events);

	id_hi = i2c_smbus_read_byte_data(client, 0x3d);
	id_lo = i2c_smbus_read_byte_data(client, 0x3e);
	if (id_hi >= 0 && id_lo >= 0) {
		es->chip_id = ((u16)id_hi << 8) | (u8)id_lo;
		dev_info(dev,
			 "[ES7210] Probed at I2C 0x%02x: chip_id=0x%04x (expected 0x7210), channels=%u\n",
			 client->addr, es->chip_id, es->channels);
	} else {
		dev_err(dev,
			"[ES7210] WARNING: Failed to read chip ID at I2C 0x%02x (ret=%d/%d)\n",
			client->addr, id_hi, id_lo);
	}

	es->regmap = devm_regmap_init_i2c(client, &es7210_regmap_config);
	if (IS_ERR(es->regmap))
		return PTR_ERR(es->regmap);

	for (i = 0; i < ARRAY_SIZE(es7210_reg_init_seq); i++) {
		ret = regmap_write(es->regmap,
				   es7210_reg_init_seq[i][0],
				   es7210_reg_init_seq[i][1]);
		if (ret < 0) {
			dev_err(dev,
				"[ES7210] Failed to write init reg 0x%02x: %d\n",
				es7210_reg_init_seq[i][0], ret);
			return ret;
		}
	}

	i2c_set_clientdata(client, es);
	device_create_file(dev, &dev_attr_status);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	return devm_snd_soc_register_component(dev, &soc_component_dev_es7210,
					       &es7210_dai0, 1);
#else
	return snd_soc_register_codec(dev, &soc_codec_dev_es7210,
				      &es7210_dai0, 1);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void es7210_i2c_remove(struct i2c_client *client)
#else
static int es7210_i2c_remove(struct i2c_client *client)
#endif
{
	struct es7210_priv *es = i2c_get_clientdata(client);

	device_remove_file(&client->dev, &dev_attr_status);
	if (es)
		cancel_delayed_work_sync(&es->pcm_pop_work);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
	snd_soc_unregister_codec(&client->dev);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id es7210_i2c_id[] = {
	{ "es7210_2ch", 2 },
	{ "es7210_4ch", 4 },
	{ "es7210", 2 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es7210_i2c_id);

static struct i2c_driver es7210_i2c_driver = {
	.driver = {
		.name = ES7210_DRV_NAME,
		.of_match_table = es7210_dt_ids,
	},
	.probe = es7210_i2c_probe,
	.remove = es7210_i2c_remove,
	.id_table = es7210_i2c_id,
};
module_i2c_driver(es7210_i2c_driver);

MODULE_DESCRIPTION("ASoC ES7210 audio adc driver");
MODULE_LICENSE("GPL v2");
