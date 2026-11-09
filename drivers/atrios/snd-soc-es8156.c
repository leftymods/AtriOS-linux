// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/snd-soc-es8156.c
 *
 * Everest ES8156 stereo DAC ASoC driver for Yandex Station Max headphone out.
 * Reconstructed from snd-soc-es8156.ko (4.9.113) and ported to Mainline Linux 7.0.
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
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/sysfs.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define ES8156_DRV_NAME		"es8156"

static bool debug = true;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose ES8156 headphone DAC diagnostics");

static const struct reg_default es8156_reg_defaults[] = {
	{ 0x00, 0x1c }, { 0x01, 0x20 }, { 0x02, 0x00 }, { 0x03, 0x01 },
	{ 0x04, 0x00 }, { 0x05, 0x04 }, { 0x06, 0x11 }, { 0x07, 0x00 },
	{ 0x08, 0x06 }, { 0x09, 0x00 }, { 0x0a, 0x50 }, { 0x0b, 0x50 },
	{ 0x0c, 0x00 }, { 0x0d, 0x10 }, { 0x10, 0x40 }, { 0x11, 0x00 },
	{ 0x12, 0x04 }, { 0x13, 0x11 }, { 0x14, 0xbf }, { 0x15, 0x00 },
	{ 0x16, 0x00 }, { 0x17, 0xf7 }, { 0x18, 0x00 }, { 0x19, 0x20 },
	{ 0x1a, 0x00 }, { 0x20, 0x16 }, { 0x21, 0x7f }, { 0x22, 0x00 },
	{ 0x23, 0x86 }, { 0x24, 0x00 }, { 0x25, 0x07 }, { 0xfc, 0x00 },
	{ 0xfd, 0x81 }, { 0xfe, 0x55 }, { 0xff, 0x10 },
};

struct es8156_priv {
	struct i2c_client *client;
	struct regmap *regmap;
	struct clk *mclk;
	u32 mclk_mult;
	bool sclk_as_mclk;
	bool dll_enable;
	bool muted;
	bool hw_inited;
	u16 chip_id;
	u8 chip_ver;
};

static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -9550, 50, 1);

static const struct snd_kcontrol_new es8156_snd_controls[] = {
	SOC_SINGLE_TLV("DAC Playback Volume", 0x14, 0, 0xff, 0, dac_vol_tlv),
	SOC_SINGLE("Headphone Switch", 0x13, 4, 1, 1),
};

static const struct snd_soc_dapm_widget es8156_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DACL", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DACR", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
};

static const struct snd_soc_dapm_route es8156_dapm_routes[] = {
	{ "LOUT", NULL, "DACL" },
	{ "ROUT", NULL, "DACR" },
};

static int es8156_hw_init(struct es8156_priv *es)
{
	int ret;

	ret = regmap_write(es->regmap, 0x00, 0x1c);
	if (ret < 0) {
		dev_err(&es->client->dev,
			"[ES8156] hw_init failed at reg 0x00: %d\n", ret);
		return ret;
	}
	usleep_range(5000, 5500);
	regmap_write(es->regmap, 0x00, 0x03);
	regmap_write(es->regmap, 0x00, 0x3c);
	msleep(10);
	regmap_write(es->regmap, 0x00, 0x1c);

	regmap_write(es->regmap, 0x02, (es->sclk_as_mclk ? 0x80 : 0x00) | 0x04);
	regmap_write(es->regmap, 0x20, 0x2a);
	regmap_write(es->regmap, 0x21, 0x3c);
	regmap_write(es->regmap, 0x22, 0x08);
	regmap_write(es->regmap, 0x24, 0x07);
	regmap_write(es->regmap, 0x23, 0x00);
	regmap_write(es->regmap, 0x0a, 0x01);
	regmap_write(es->regmap, 0x0b, 0x01);
	regmap_write(es->regmap, 0x14, 0xbe);
	regmap_write(es->regmap, 0x01, 0x21 | ((es->mclk_mult & 0x3) << 6));
	regmap_write(es->regmap, 0x09, es->dll_enable ? 0x02 : 0x00);
	regmap_write(es->regmap, 0x0d, 0x14);
	regmap_write(es->regmap, 0x18, 0x00);
	regmap_write(es->regmap, 0x08, 0x3f);
	regmap_write(es->regmap, 0x00, 0x02);
	regmap_write(es->regmap, 0x00, 0x03);
	regmap_write(es->regmap, 0x25, 0x20);
	es->hw_inited = true;

	dev_info(&es->client->dev,
		 "[ES8156] hw_init completed (sclk_as_mclk=%d, dll_enable=%d, mclk_mult_idx=%u)\n",
		 es->sclk_as_mclk, es->dll_enable, es->mclk_mult);
	return 0;
}

static int es8156_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es8156_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es8156_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif

	if (debug)
		dev_info(&es->client->dev, "[ES8156] set_dai_fmt: fmt=0x%08x\n", fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_BC_FP:
		regmap_update_bits(es->regmap, 0x02, 0x01, 0x01);
		break;
	case SND_SOC_DAIFMT_BP_FP:
		regmap_update_bits(es->regmap, 0x02, 0x01, 0x00);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		regmap_update_bits(es->regmap, 0x11, 0x07, 0x00);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		regmap_update_bits(es->regmap, 0x11, 0x07, 0x01);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		regmap_update_bits(es->regmap, 0x11, 0x07, 0x03);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		regmap_update_bits(es->regmap, 0x11, 0x07, 0x07);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es8156_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es8156_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es8156_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif

	if (debug)
		dev_info(&es->client->dev,
			 "[ES8156] hw_params: rate=%u width=%d channels=%u\n",
			 params_rate(params), params_width(params),
			 params_channels(params));

	switch (params_width(params)) {
	case 16:
		regmap_update_bits(es->regmap, 0x11, 0x70, 0x30);
		break;
	case 20:
		regmap_update_bits(es->regmap, 0x11, 0x70, 0x10);
		break;
	case 24:
		regmap_update_bits(es->regmap, 0x11, 0x70, 0x00);
		break;
	case 32:
		regmap_update_bits(es->regmap, 0x11, 0x70, 0x40);
		break;
	default:
		break;
	}
	return 0;
}

static int es8156_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct es8156_priv *es = snd_soc_component_get_drvdata(dai->component);
#else
	struct es8156_priv *es = snd_soc_codec_get_drvdata(dai->codec);
#endif

	es->muted = !!mute;
	if (debug)
		dev_info(&es->client->dev, "[ES8156] mute_stream: mute=%d\n", mute);

	if (mute)
		regmap_update_bits(es->regmap, 0x13, 0x08, 0x08);
	else
		regmap_update_bits(es->regmap, 0x13, 0x08, 0x00);
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
static int es8156_digital_mute(struct snd_soc_dai *dai, int mute)
{
	return es8156_mute_stream(dai, mute, SNDRV_PCM_STREAM_PLAYBACK);
}
#endif

static const struct snd_soc_dai_ops es8156_ops = {
	.set_fmt = es8156_set_dai_fmt,
	.hw_params = es8156_pcm_hw_params,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	.mute_stream = es8156_mute_stream,
#else
	.digital_mute = es8156_digital_mute,
#endif
};

static struct snd_soc_dai_driver es8156_dai = {
	.name = "ES8156 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S20_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &es8156_ops,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static int es8156_component_probe(struct snd_soc_component *component)
{
	struct es8156_priv *es = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, es->regmap);
	return es8156_hw_init(es);
}

static const struct snd_soc_component_driver soc_component_dev_es8156 = {
	.probe = es8156_component_probe,
	.controls = es8156_snd_controls,
	.num_controls = ARRAY_SIZE(es8156_snd_controls),
	.dapm_widgets = es8156_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es8156_dapm_widgets),
	.dapm_routes = es8156_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(es8156_dapm_routes),
};
#else
static int es8156_codec_probe(struct snd_soc_codec *codec)
{
	struct es8156_priv *es = snd_soc_codec_get_drvdata(codec);

	return es8156_hw_init(es);
}

static const struct snd_soc_codec_driver soc_codec_dev_es8156 = {
	.probe = es8156_codec_probe,
	.component_driver = {
		.controls = es8156_snd_controls,
		.num_controls = ARRAY_SIZE(es8156_snd_controls),
		.dapm_widgets = es8156_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(es8156_dapm_widgets),
		.dapm_routes = es8156_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(es8156_dapm_routes),
	},
};
#endif

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct es8156_priv *es = dev_get_drvdata(dev);
	unsigned int r13 = 0, r14 = 0;

	if (es->regmap) {
		regmap_read(es->regmap, 0x13, &r13);
		regmap_read(es->regmap, 0x14, &r14);
	}

	return sysfs_emit(buf,
			  "addr=0x%02x chip_id=0x%04x ver=0x%02x hw_inited=%d muted=%d reg13=0x%02x vol(14)=0x%02x sclk_as_mclk=%d dll_enable=%d\n",
			  es->client->addr, es->chip_id, es->chip_ver,
			  es->hw_inited, es->muted, r13, r14,
			  es->sclk_as_mclk, es->dll_enable);
}
static DEVICE_ATTR_RO(status);

static const struct regmap_config es8156_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.reg_defaults = es8156_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(es8156_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int es8156_i2c_probe(struct i2c_client *client)
#else
static int es8156_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct es8156_priv *es;
	u32 mult = 1;
	int id_hi, id_lo, ver;

	es = devm_kzalloc(dev, sizeof(*es), GFP_KERNEL);
	if (!es)
		return -ENOMEM;

	es->client = client;

	if (device_property_read_u32(dev, "everest,mclk-multiplier", &mult) == 0) {
		switch (mult) {
		case 1: es->mclk_mult = 0; break;
		case 2: es->mclk_mult = 1; break;
		case 4: es->mclk_mult = 2; break;
		case 8: es->mclk_mult = 3; break;
		default: es->mclk_mult = 0; break;
		}
	}

	es->sclk_as_mclk = device_property_present(dev, "everest,sclk-as-mclk");
	es->dll_enable = device_property_present(dev, "everest,dll-enable");

	/* Read physical chip ID registers 0xFD, 0xFE, 0xFF over I2C */
	id_hi = i2c_smbus_read_byte_data(client, 0xfd);
	id_lo = i2c_smbus_read_byte_data(client, 0xfe);
	ver = i2c_smbus_read_byte_data(client, 0xff);
	if (id_hi >= 0 && id_lo >= 0) {
		es->chip_id = ((u16)id_hi << 8) | (u8)id_lo;
		es->chip_ver = ver >= 0 ? (u8)ver : 0;
		dev_info(dev,
			 "[ES8156] Probed at I2C 0x%02x: chip_id=0x%04x (expected 0x8155), ver=0x%02x\n",
			 client->addr, es->chip_id, es->chip_ver);
	} else {
		dev_err(dev,
			"[ES8156] WARNING: Failed to read chip ID at I2C 0x%02x (ret=%d/%d)\n",
			client->addr, id_hi, id_lo);
	}

	es->regmap = devm_regmap_init_i2c(client, &es8156_regmap_config);
	if (IS_ERR(es->regmap))
		return PTR_ERR(es->regmap);

	i2c_set_clientdata(client, es);
	device_create_file(dev, &dev_attr_status);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	return devm_snd_soc_register_component(dev, &soc_component_dev_es8156,
					       &es8156_dai, 1);
#else
	return snd_soc_register_codec(dev, &soc_codec_dev_es8156,
				      &es8156_dai, 1);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void es8156_i2c_remove(struct i2c_client *client)
#else
static int es8156_i2c_remove(struct i2c_client *client)
#endif
{
	device_remove_file(&client->dev, &dev_attr_status);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
	snd_soc_unregister_codec(&client->dev);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id es8156_i2c_id[] = {
	{ "es8156", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8156_i2c_id);

static const struct of_device_id es8156_of_match[] = {
	{ .compatible = "everest,es8156" },
	{ }
};
MODULE_DEVICE_TABLE(of, es8156_of_match);

static struct i2c_driver es8156_i2c_driver = {
	.driver = {
		.name = ES8156_DRV_NAME,
		.of_match_table = es8156_of_match,
	},
	.probe = es8156_i2c_probe,
	.remove = es8156_i2c_remove,
	.id_table = es8156_i2c_id,
};
module_i2c_driver(es8156_i2c_driver);

MODULE_AUTHOR("Will <pengxiaoxin@everset-semi.com>");
MODULE_DESCRIPTION("ASoC es8156 driver");
MODULE_LICENSE("GPL");
