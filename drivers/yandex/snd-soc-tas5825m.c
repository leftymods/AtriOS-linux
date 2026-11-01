// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/snd-soc-tas5825m.c
 *
 * TI TAS5825M / TAS5805M Audio Amplifier ASoC Driver (Yandex Station 2 / Max).
 * Reconstructed from snd-soc-tas5825m.ko (4.9.113) and ported to Mainline Linux 7.0.
 *
 * Copyright (C) Texas Instruments / Yandex Inc.
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
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define TAS5825M_DRV_NAME	"tas5825m"

struct tas5825m_priv {
	struct regmap *regmap;
	struct i2c_client *client;
	const char *fw_name;
	struct gpio_desc *pdn_gpio;
	struct regulator_bulk_data supplies[2];
	struct mutex io_lock;
};

static const DECLARE_TLV_DB_SCALE(tas5825m_digital_vol_tlv, -10350, 50, 1);
static const DECLARE_TLV_DB_SCALE(tas5825m_analog_gain_tlv, -1550, 50, 0);

static const struct snd_kcontrol_new tas5825m_controls[] = {
	SOC_SINGLE_TLV("Digital Playback Volume", 0x4c, 0, 0xff, 1,
		       tas5825m_digital_vol_tlv),
	SOC_SINGLE_TLV("Analog Playback Volume", 0x54, 0, 0x1f, 1,
		       tas5825m_analog_gain_tlv),
	SOC_SINGLE("Master Playback Switch", 0x03, 0, 3, 0),
	SOC_SINGLE("Master Playback Digital Mute Switch", 0x03, 3, 1, 1),
};

static int tas5825m_apply_settings(struct tas5825m_priv *tas, const char *data,
				   size_t size)
{
	const char *p = data;
	const char *end = data + size;
	u8 block[36];
	int block_len = 0;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char c = *p;

		if (c == '#' || c == '\n' || c == '\r') {
			if (block_len > 0) {
				i2c_smbus_write_i2c_block_data(tas->client,
							       block[0],
							       block_len - 1,
							       &block[1]);
				block_len = 0;
			}
		} else if (c == 'd') {
			unsigned int delay_ms = 0;

			if (block_len > 0) {
				i2c_smbus_write_i2c_block_data(tas->client,
							       block[0],
							       block_len - 1,
							       &block[1]);
				block_len = 0;
			}
			if (sscanf(p, "d %u", &delay_ms) == 1 && delay_ms > 0)
				msleep(delay_ms);
		} else if (c == 'w') {
			size_t pos = 1;

			if (block_len > 0) {
				i2c_smbus_write_i2c_block_data(tas->client,
							       block[0],
							       block_len - 1,
							       &block[1]);
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
		}

		p += len + (nl ? 1 : 0);
	}

	if (block_len > 0) {
		i2c_smbus_write_i2c_block_data(tas->client, block[0],
					       block_len - 1, &block[1]);
	}

	return 0;
}

static int tas5825m_reset_chip(struct tas5825m_priv *tas)
{
	i2c_smbus_write_byte_data(tas->client, 0x00, 0x00);
	i2c_smbus_write_byte_data(tas->client, 0x7f, 0x00);
	i2c_smbus_write_byte_data(tas->client, 0x03, 0x0a);
	msleep(5);
	i2c_smbus_write_byte_data(tas->client, 0x01, 0x11);
	msleep(20);
	return 0;
}

static int tas5825m_load_firmware(struct tas5825m_priv *tas)
{
	const struct firmware *fw;
	int ret;

	if (!tas->fw_name)
		return 0;

	ret = request_firmware(&fw, tas->fw_name, &tas->client->dev);
	if (ret)
		return 0;

	ret = tas5825m_apply_settings(tas, (const char *)fw->data, fw->size);
	release_firmware(fw);
	return ret;
}

static int tas5825m_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	return 0;
}

static int tas5825m_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	struct tas5825m_priv *tas = snd_soc_component_get_drvdata(dai->component);
#else
	struct tas5825m_priv *tas = snd_soc_codec_get_drvdata(dai->codec);
#endif

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		regmap_update_bits(tas->regmap, 0x03, 0x03, 0x03);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_update_bits(tas->regmap, 0x03, 0x03, 0x02);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops tas5825m_dai_ops = {
	.hw_params = tas5825m_hw_params,
	.trigger = tas5825m_trigger,
};

static struct snd_soc_dai_driver tas5825m_dai = {
	.name = "tas5825m-amplifier",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &tas5825m_dai_ops,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static int tas5825m_component_probe(struct snd_soc_component *component)
{
	struct tas5825m_priv *tas = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, tas->regmap);
	mutex_lock(&tas->io_lock);
	tas5825m_reset_chip(tas);
	tas5825m_load_firmware(tas);
	mutex_unlock(&tas->io_lock);
	return 0;
}

static const struct snd_soc_component_driver soc_component_tas5825m = {
	.probe = tas5825m_component_probe,
	.controls = tas5825m_controls,
	.num_controls = ARRAY_SIZE(tas5825m_controls),
};
#else
static int tas5825m_codec_probe(struct snd_soc_codec *codec)
{
	struct tas5825m_priv *tas = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&tas->io_lock);
	tas5825m_reset_chip(tas);
	tas5825m_load_firmware(tas);
	mutex_unlock(&tas->io_lock);
	return 0;
}

static const struct snd_soc_codec_driver soc_codec_tas5825m = {
	.probe = tas5825m_codec_probe,
	.component_driver = {
		.controls = tas5825m_controls,
		.num_controls = ARRAY_SIZE(tas5825m_controls),
	},
};
#endif

static const struct regmap_config tas5825m_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int tas5825m_probe(struct i2c_client *client)
#else
static int tas5825m_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct tas5825m_priv *tas;
	int ret;

	tas = devm_kzalloc(dev, sizeof(*tas), GFP_KERNEL);
	if (!tas)
		return -ENOMEM;

	tas->client = client;
	mutex_init(&tas->io_lock);

	tas->regmap = devm_regmap_init_i2c(client, &tas5825m_regmap);
	if (IS_ERR(tas->regmap))
		return PTR_ERR(tas->regmap);

	of_property_read_string(dev->of_node, "firmware", &tas->fw_name);

	tas->supplies[0].supply = "dvdd";
	tas->supplies[1].supply = "pvdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(tas->supplies),
				      tas->supplies);
	if (ret == 0)
		regulator_bulk_enable(ARRAY_SIZE(tas->supplies), tas->supplies);

	tas->pdn_gpio = devm_gpiod_get_optional(dev, "pdn", GPIOD_OUT_HIGH);
	if (!IS_ERR_OR_NULL(tas->pdn_gpio)) {
		gpiod_set_value_cansleep(tas->pdn_gpio, 0);
		msleep(10);
		gpiod_set_value_cansleep(tas->pdn_gpio, 1);
		msleep(20);
	}

	i2c_set_clientdata(client, tas);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
	return devm_snd_soc_register_component(dev, &soc_component_tas5825m,
					       &tas5825m_dai, 1);
#else
	return snd_soc_register_codec(dev, &soc_codec_tas5825m,
				      &tas5825m_dai, 1);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void tas5825m_remove(struct i2c_client *client)
#else
static int tas5825m_remove(struct i2c_client *client)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
	snd_soc_unregister_codec(&client->dev);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id tas5825m_i2c_id[] = {
	{ "tas5825m", 0 },
	{ "tas5805m", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5825m_i2c_id);

static const struct of_device_id tas5825m_of_match[] = {
	{ .compatible = "ti,tas5825m" },
	{ .compatible = "ti,tas5805m" },
	{ }
};
MODULE_DEVICE_TABLE(of, tas5825m_of_match);

static struct i2c_driver tas5825m_i2c_driver = {
	.driver = {
		.name = TAS5825M_DRV_NAME,
		.of_match_table = tas5825m_of_match,
	},
	.probe = tas5825m_probe,
	.remove = tas5825m_remove,
	.id_table = tas5825m_i2c_id,
};
module_i2c_driver(tas5825m_i2c_driver);

MODULE_AUTHOR("Andy Liu <andy-liu@ti.com>");
MODULE_DESCRIPTION("TAS5825M Audio Amplifier Driver");
MODULE_LICENSE("GPL v2");
