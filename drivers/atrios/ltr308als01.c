// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/yandex/ltr308als01.c
 *
 * Lite-On LTR308ALS01 ambient light sensor IIO driver for Yandex Station Max.
 * Reconstructed from ltr308als01.ko (4.9.113) and ported to Mainline Linux 7.0.
 *
 * Copyright (C) 2019 Fingerling
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

#define DRV_NAME			"ltr308als01"

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Enable verbose LTR-308ALS sensor read and lux calculation logging");

#define LTR308_REG_MAIN_CTRL		0x00
#define LTR308_REG_ALS_MEAS_RATE	0x04
#define LTR308_REG_ALS_GAIN		0x05
#define LTR308_REG_PART_ID		0x06
#define LTR308_REG_MAIN_STATUS		0x07
#define LTR308_REG_ALS_DATA_0		0x0D
#define LTR308_REG_ALS_DATA_1		0x0E
#define LTR308_REG_ALS_DATA_2		0x0F
#define LTR308_REG_INT_CFG		0x19
#define LTR308_REG_INT_PST		0x1A
#define LTR308_REG_ALS_THRES_UP_0	0x21
#define LTR308_REG_ALS_THRES_UP_1	0x22
#define LTR308_REG_ALS_THRES_UP_2	0x23
#define LTR308_REG_ALS_THRES_LOW_0	0x24
#define LTR308_REG_ALS_THRES_LOW_1	0x25
#define LTR308_REG_ALS_THRES_LOW_2	0x26
#define LTR308_MAX_REG			0x26

#define LTR308_PART_NUMBER_ID		0x0B

enum ltr308_regfields {
	F_SW_RESET = 0,
	F_ENABLE,
	F_RESOLUTION,
	F_MEAS_RATE,
	F_GAIN_RANGE,
	F_PART_ID,
	F_REV_ID,
	F_POWER_ON,
	F_INT_STATUS,
	F_DATA_STATUS,
	F_DATA_LOW,
	F_DATA_MID,
	F_DATA_HIGH,
	F_INT_SEL,
	F_INT_PIN_EN,
	F_PERSIST,
	F_UP_THRES_LOW,
	F_UP_THRES_MID,
	F_UP_THRES_HIGH,
	F_LOW_THRES_LOW,
	F_LOW_THRES_MID,
	F_LOW_THRES_HIGH,
	F_MAX_FIELDS
};

static const struct reg_field ltr308_reg_fields[F_MAX_FIELDS] = {
	[F_SW_RESET]       = REG_FIELD(LTR308_REG_MAIN_CTRL, 4, 4),
	[F_ENABLE]         = REG_FIELD(LTR308_REG_MAIN_CTRL, 1, 1),
	[F_RESOLUTION]     = REG_FIELD(LTR308_REG_ALS_MEAS_RATE, 4, 6),
	[F_MEAS_RATE]      = REG_FIELD(LTR308_REG_ALS_MEAS_RATE, 0, 2),
	[F_GAIN_RANGE]     = REG_FIELD(LTR308_REG_ALS_GAIN, 0, 2),
	[F_PART_ID]        = REG_FIELD(LTR308_REG_PART_ID, 4, 7),
	[F_REV_ID]         = REG_FIELD(LTR308_REG_PART_ID, 0, 3),
	[F_POWER_ON]       = REG_FIELD(LTR308_REG_MAIN_STATUS, 5, 5),
	[F_INT_STATUS]     = REG_FIELD(LTR308_REG_MAIN_STATUS, 4, 4),
	[F_DATA_STATUS]    = REG_FIELD(LTR308_REG_MAIN_STATUS, 3, 3),
	[F_DATA_LOW]       = REG_FIELD(LTR308_REG_ALS_DATA_0, 0, 7),
	[F_DATA_MID]       = REG_FIELD(LTR308_REG_ALS_DATA_1, 0, 7),
	[F_DATA_HIGH]      = REG_FIELD(LTR308_REG_ALS_DATA_2, 0, 3),
	[F_INT_SEL]        = REG_FIELD(LTR308_REG_INT_CFG, 4, 5),
	[F_INT_PIN_EN]     = REG_FIELD(LTR308_REG_INT_CFG, 2, 2),
	[F_PERSIST]        = REG_FIELD(LTR308_REG_INT_PST, 4, 7),
	[F_UP_THRES_LOW]   = REG_FIELD(LTR308_REG_ALS_THRES_UP_0, 0, 7),
	[F_UP_THRES_MID]   = REG_FIELD(LTR308_REG_ALS_THRES_UP_1, 0, 7),
	[F_UP_THRES_HIGH]  = REG_FIELD(LTR308_REG_ALS_THRES_UP_2, 0, 3),
	[F_LOW_THRES_LOW]  = REG_FIELD(LTR308_REG_ALS_THRES_LOW_0, 0, 7),
	[F_LOW_THRES_MID]  = REG_FIELD(LTR308_REG_ALS_THRES_LOW_1, 0, 7),
	[F_LOW_THRES_HIGH] = REG_FIELD(LTR308_REG_ALS_THRES_LOW_2, 0, 3),
};

static const int ltr308_gains[5] = { 1, 3, 6, 9, 18 };
static const int ltr308_int_time[5] = { 400000, 200000, 100000, 50000, 25000 };

struct ltr308_data {
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex lock;
	struct regmap_field *fields[F_MAX_FIELDS];
	unsigned int part_id;
	unsigned int rev_id;
	unsigned long read_count;
};

static bool ltr308_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTR308_REG_MAIN_CTRL:
	case LTR308_REG_MAIN_STATUS:
	case LTR308_REG_ALS_DATA_0:
	case LTR308_REG_ALS_DATA_1:
	case LTR308_REG_ALS_DATA_2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ltr308_regmap_config = {
	.name = DRV_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTR308_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = ltr308_is_volatile_reg,
};

static int ltr308_get_lux(struct ltr308_data *data)
{
	unsigned int status, low, mid, high;
	int tries = 10;
	int ret, val;

	while (tries--) {
		ret = regmap_field_read(data->fields[F_DATA_STATUS], &status);
		if (ret) {
			dev_err(&data->client->dev,
				"[LTR308] ERROR: failed reading DATA_STATUS: %d\n", ret);
			return ret;
		}
		if (status)
			break;
		msleep(25);
	}

	ret = regmap_field_read(data->fields[F_DATA_LOW], &low);
	if (ret)
		return ret;
	ret = regmap_field_read(data->fields[F_DATA_MID], &mid);
	if (ret)
		return ret;
	ret = regmap_field_read(data->fields[F_DATA_HIGH], &high);
	if (ret)
		return ret;

	val = (low & 0xff) | ((mid & 0xff) << 8) | ((high & 0x0f) << 16);
	data->read_count++;
	if (debug)
		dev_info(&data->client->dev,
			 "[LTR308] read #%lu: raw=%d (0x%05x)\n",
			 data->read_count, val, val);
	return val;
}

static int ltr308_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int regval, gain_idx, res_idx;
	u64 lux_num, lux_den;
	int raw, ret;

	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_field_read(data->fields[F_ENABLE], &regval);
		if (ret)
			break;
		if (!regval) {
			ret = -EINVAL;
			break;
		}
		raw = ltr308_get_lux(data);
		if (raw < 0) {
			ret = raw;
			break;
		}
		*val = raw;
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_PROCESSED:
		ret = regmap_field_read(data->fields[F_ENABLE], &regval);
		if (ret)
			break;
		if (!regval) {
			ret = -EINVAL;
			break;
		}
		raw = ltr308_get_lux(data);
		if (raw < 0) {
			ret = raw;
			break;
		}
		ret = regmap_field_read(data->fields[F_GAIN_RANGE], &gain_idx);
		if (ret)
			break;
		ret = regmap_field_read(data->fields[F_RESOLUTION], &res_idx);
		if (ret)
			break;
		if (gain_idx >= ARRAY_SIZE(ltr308_gains) ||
		    res_idx >= ARRAY_SIZE(ltr308_int_time)) {
			ret = -EINVAL;
			break;
		}
		/* Lux = 0.6 * raw / (gain * int_time_ms / 100) */
		lux_num = (u64)raw * 60000ULL;
		lux_den = (u64)ltr308_gains[gain_idx] *
			  ((u64)ltr308_int_time[res_idx] / 1000ULL);
		if (!lux_den) {
			ret = -EINVAL;
			break;
		}
		*val = (int)div64_u64(lux_num, lux_den);
		*val2 = 1000;
		if (debug)
			dev_info(&data->client->dev,
				 "[LTR308] processed lux=%d.%03d (raw=%d, gain=%dx, int_us=%d)\n",
				 *val / 1000, *val % 1000, raw,
				 ltr308_gains[gain_idx], ltr308_int_time[res_idx]);
		ret = IIO_VAL_FRACTIONAL;
		break;

	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = regmap_field_read(data->fields[F_GAIN_RANGE], &gain_idx);
		if (ret)
			break;
		if (gain_idx >= ARRAY_SIZE(ltr308_gains)) {
			ret = -EINVAL;
			break;
		}
		*val = ltr308_gains[gain_idx];
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_INT_TIME:
		ret = regmap_field_read(data->fields[F_RESOLUTION], &res_idx);
		if (ret)
			break;
		if (res_idx >= ARRAY_SIZE(ltr308_int_time)) {
			ret = -EINVAL;
			break;
		}
		*val = 0;
		*val2 = ltr308_int_time[res_idx];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;

	case IIO_CHAN_INFO_ENABLE:
		ret = regmap_field_read(data->fields[F_ENABLE], &regval);
		if (ret)
			break;
		*val = !!regval;
		ret = IIO_VAL_INT;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);
	return ret;
}

static int ltr308_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ltr308_data *data = iio_priv(indio_dev);
	int i, ret = -EINVAL;

	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (val2 != 0)
			break;
		for (i = 0; i < ARRAY_SIZE(ltr308_gains); i++) {
			if (val == ltr308_gains[i]) {
				ret = regmap_field_write(data->fields[F_GAIN_RANGE], i);
				break;
			}
		}
		break;

	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			break;
		for (i = 0; i < ARRAY_SIZE(ltr308_int_time); i++) {
			if (val2 == ltr308_int_time[i]) {
				ret = regmap_field_write(data->fields[F_RESOLUTION], i);
				break;
			}
		}
		break;

	case IIO_CHAN_INFO_ENABLE:
		if (val2 != 0 || (val != 0 && val != 1))
			break;
		ret = regmap_field_write(data->fields[F_ENABLE], val);
		break;

	default:
		break;
	}

	mutex_unlock(&data->lock);
	return ret;
}

static int ltr308_read_event(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir,
			     enum iio_event_info info,
			     int *val, int *val2)
{
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int low, mid, high;
	int ret;

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	mutex_lock(&data->lock);

	if (dir == IIO_EV_DIR_RISING) {
		ret = regmap_field_read(data->fields[F_UP_THRES_LOW], &low);
		if (!ret)
			ret = regmap_field_read(data->fields[F_UP_THRES_MID], &mid);
		if (!ret)
			ret = regmap_field_read(data->fields[F_UP_THRES_HIGH], &high);
	} else if (dir == IIO_EV_DIR_FALLING) {
		ret = regmap_field_read(data->fields[F_LOW_THRES_LOW], &low);
		if (!ret)
			ret = regmap_field_read(data->fields[F_LOW_THRES_MID], &mid);
		if (!ret)
			ret = regmap_field_read(data->fields[F_LOW_THRES_HIGH], &high);
	} else {
		ret = -EINVAL;
	}

	mutex_unlock(&data->lock);
	if (ret)
		return ret;

	*val = (low & 0xff) | ((mid & 0xff) << 8) | ((high & 0x0f) << 16);
	return IIO_VAL_INT;
}

static int ltr308_write_event(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info,
			      int val, int val2)
{
	struct ltr308_data *data = iio_priv(indio_dev);
	int ret;

	if (info != IIO_EV_INFO_VALUE || val < 0 || val > 0xfffff)
		return -EINVAL;

	mutex_lock(&data->lock);

	if (dir == IIO_EV_DIR_RISING) {
		ret = regmap_field_write(data->fields[F_UP_THRES_LOW], val & 0xff);
		if (!ret)
			ret = regmap_field_write(data->fields[F_UP_THRES_MID], (val >> 8) & 0xff);
		if (!ret)
			ret = regmap_field_write(data->fields[F_UP_THRES_HIGH], (val >> 16) & 0x0f);
	} else if (dir == IIO_EV_DIR_FALLING) {
		ret = regmap_field_write(data->fields[F_LOW_THRES_LOW], val & 0xff);
		if (!ret)
			ret = regmap_field_write(data->fields[F_LOW_THRES_MID], (val >> 8) & 0xff);
		if (!ret)
			ret = regmap_field_write(data->fields[F_LOW_THRES_HIGH], (val >> 16) & 0x0f);
	} else {
		ret = -EINVAL;
	}

	mutex_unlock(&data->lock);
	return ret;
}

static int ltr308_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	ret = regmap_field_read(data->fields[F_INT_PIN_EN], &val);
	if (ret)
		return ret;

	return !!val;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
static int ltr308_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     bool state)
#else
static int ltr308_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     int state)
#endif
{
	struct ltr308_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_field_write(data->fields[F_INT_PIN_EN], !!state);
	mutex_unlock(&data->lock);
	return ret;
}

static ssize_t event_persist_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	ret = regmap_field_read(data->fields[F_PERSIST], &val);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val + 1);
}

static ssize_t event_persist_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;
	if (val < 1 || val > 16)
		return -EINVAL;

	mutex_lock(&data->lock);
	ret = regmap_field_write(data->fields[F_PERSIST], val - 1);
	mutex_unlock(&data->lock);

	return ret ? ret : len;
}

static IIO_CONST_ATTR(in_illuminance_hardwaregain_available, "1 3 6 9 18");
static IIO_CONST_ATTR_INT_TIME_AVAIL("0.025 0.05 0.1 0.2 0.4");
static IIO_DEVICE_ATTR_RW(event_persist, 0);

static struct attribute *ltr308_attributes[] = {
	&iio_const_attr_in_illuminance_hardwaregain_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_dev_attr_event_persist.dev_attr.attr,
	NULL,
};

static const struct attribute_group ltr308_attribute_group = {
	.attrs = ltr308_attributes,
};

static const struct iio_event_spec ltr308_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec ltr308_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_HARDWAREGAIN) |
				      BIT(IIO_CHAN_INFO_INT_TIME) |
				      BIT(IIO_CHAN_INFO_ENABLE),
		.event_spec = ltr308_event_spec,
		.num_event_specs = ARRAY_SIZE(ltr308_event_spec),
	},
};

static const struct iio_info ltr308_info = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	.driver_module = THIS_MODULE,
#endif
	.read_raw = ltr308_read_raw,
	.write_raw = ltr308_write_raw,
	.read_event_value = ltr308_read_event,
	.write_event_value = ltr308_write_event,
	.read_event_config = ltr308_read_event_config,
	.write_event_config = ltr308_write_event_config,
	.attrs = &ltr308_attribute_group,
};

static irqreturn_t ltr308_interrupt_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct ltr308_data *data = iio_priv(indio_dev);
	unsigned int status;
	int ret;

	ret = regmap_field_read(data->fields[F_INT_STATUS], &status);
	if (ret || !status)
		return IRQ_NONE;

	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_EITHER),
		       iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static int ltr308_init(struct ltr308_data *data)
{
	unsigned int part_id = 0, rev_id = 0, status = 0;
	int ret;

	ret = regmap_field_read(data->fields[F_POWER_ON], &status);
	if (ret) {
		dev_err(&data->client->dev,
			"[LTR308] ERROR: failed reading MAIN_STATUS @ 0x%02x: %d\n",
			data->client->addr, ret);
		return ret;
	}

	ret = regmap_field_read(data->fields[F_PART_ID], &part_id);
	if (ret) {
		dev_err(&data->client->dev,
			"[LTR308] ERROR: failed reading PART_ID @ 0x%02x: %d\n",
			data->client->addr, ret);
		return ret;
	}
	regmap_field_read(data->fields[F_REV_ID], &rev_id);

	data->part_id = part_id;
	data->rev_id = rev_id;

	if (part_id != LTR308_PART_NUMBER_ID)
		dev_warn(&data->client->dev,
			 "[LTR308] WARN: unexpected part ID 0x%x (expected 0x%x), rev=0x%x\n",
			 part_id, LTR308_PART_NUMBER_ID, rev_id);
	else
		dev_info(&data->client->dev,
			 "[LTR308] Chip verified @ 0x%02x: PART_ID=0x%x, REV_ID=0x%x, POWER_ON=%u\n",
			 data->client->addr, part_id, rev_id, status);

	/* Enable ALS by default so userspace can read illuminance immediately */
	return regmap_field_write(data->fields[F_ENABLE], 1);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int ltr308_probe(struct i2c_client *client)
#else
static int ltr308_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#endif
{
	struct ltr308_data *data;
	struct iio_dev *indio_dev;
	int i, ret;

	dev_info(&client->dev,
		 "[LTR308] Probing LTR-308ALS ambient light sensor @ 0x%02x...\n",
		 client->addr);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	data->regmap = devm_regmap_init_i2c(client, &ltr308_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "[LTR308] ERROR: Regmap initialization failed: %ld\n",
			PTR_ERR(data->regmap));
		return PTR_ERR(data->regmap);
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		data->fields[i] = devm_regmap_field_alloc(&client->dev,
							  data->regmap,
							  ltr308_reg_fields[i]);
		if (IS_ERR(data->fields[i]))
			return PTR_ERR(data->fields[i]);
	}

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ltr308_info;
	indio_dev->name = DRV_NAME;
	indio_dev->channels = ltr308_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltr308_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ltr308_init(data);
	if (ret < 0)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, ltr308_interrupt_handler,
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						"ltr308_thresh_event", indio_dev);
		if (ret) {
			dev_err(&client->dev, "[LTR308] ERROR: request irq (%d) failed: %d\n",
				client->irq, ret);
			return ret;
		}
	}

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret == 0)
		dev_info(&client->dev,
			 "[LTR308] Probed OK @ 0x%02x (ALS enabled)\n",
			 client->addr);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static void ltr308_remove(struct i2c_client *client)
#else
static int ltr308_remove(struct i2c_client *client)
#endif
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ltr308_data *data = iio_priv(indio_dev);

	regmap_field_write(data->fields[F_ENABLE], 0);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id ltr308_id[] = {
	{ DRV_NAME, 0 },
	{ "ltr308", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltr308_id);

static const struct of_device_id ltr308_of_match[] = {
	{ .compatible = "lite-on,ltr308als01" },
	{ .compatible = "liteon,ltr308als01" },
	{ .compatible = "liteon,ltr308" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltr308_of_match);

static struct i2c_driver ltr308_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ltr308_of_match,
	},
	.probe = ltr308_probe,
	.remove = ltr308_remove,
	.id_table = ltr308_id,
};
module_i2c_driver(ltr308_driver);

MODULE_AUTHOR("Fingerling <fingerling@yandex-team.ru>");
MODULE_DESCRIPTION("Lite-On LTR308ALS01 ambient light sensor driver");
MODULE_LICENSE("GPL v2");
