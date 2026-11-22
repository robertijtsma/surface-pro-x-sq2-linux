// SPDX-License-Identifier: GPL-2.0-only
/*
 * ams OSRAM TCS3430 Ambient Light & XYZ Tristimulus IIO Driver
 * for Microsoft Surface Pro X (SC8180X)
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>

#define TCS_ENABLE	0x80
#define TCS_ATIME	0x81
#define TCS_CFG1	0x90
#define TCS_ID		0x92
#define TCS_STATUS	0x93
#define TCS_DATA	0x94
#define TCS_CFG2	0x9f
#define TCS_CFG3	0xab
#define TCS_INTENAB	0xdd

/* 72 cycles * 2.78 ms = ~200 ms continuous integration */
#define TCS_CYCLES	72

static const int tcs_gains[4] = { 1, 4, 16, 64 };

struct spx_tcs3430 {
	struct i2c_client *client;
	struct mutex lock;
	u8 gain_code;
	int frame[4]; /* Z, Y (photopic), IR1, X */
};

static int tcs_write(struct spx_tcs3430 *s, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(s->client, reg, value);
}

static int tcs_read_frame(struct spx_tcs3430 *s)
{
	u8 raw[8];
	int ret, i;
	int y;

	ret = i2c_smbus_read_i2c_block_data(s->client, TCS_DATA,
					    sizeof(raw), raw);
	if (ret != sizeof(raw))
		return ret < 0 ? ret : -EIO;

	for (i = 0; i < 4; i++)
		s->frame[i] = raw[i * 2] | (raw[i * 2 + 1] << 8);

	y = s->frame[1];
	if (y > 52000 && s->gain_code > 0) {
		s->gain_code--;
		tcs_write(s, TCS_CFG1, s->gain_code);
	} else if (y < 200 && s->gain_code < 3) {
		s->gain_code++;
		tcs_write(s, TCS_CFG1, s->gain_code);
	}

	return 0;
}

static int tcs_read_raw(struct iio_dev *indio,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long mask)
{
	struct spx_tcs3430 *s = iio_priv(indio);
	int ret, gain;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&s->lock);
		ret = tcs_read_frame(s);
		if (!ret) {
			gain = tcs_gains[s->gain_code & 3];
			*val = (s->frame[1] * 64) / gain;
			ret = IIO_VAL_INT;
		}
		mutex_unlock(&s->lock);
		return ret;
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&s->lock);
		ret = tcs_read_frame(s);
		if (!ret) {
			*val = s->frame[chan->address];
			ret = IIO_VAL_INT;
		}
		mutex_unlock(&s->lock);
		return ret;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		mutex_lock(&s->lock);
		*val = tcs_gains[s->gain_code & 3];
		mutex_unlock(&s->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = TCS_CYCLES * 2780;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

#define TCS_INTENSITY_CHANNEL(label, idx) { \
	.type = IIO_INTENSITY, \
	.indexed = 1, \
	.channel = (idx), \
	.extend_name = (label), \
	.address = (idx), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_HARDWAREGAIN) | \
				   BIT(IIO_CHAN_INFO_INT_TIME), \
}

static const struct iio_chan_spec tcs_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_HARDWAREGAIN) |
					   BIT(IIO_CHAN_INFO_INT_TIME),
	},
	TCS_INTENSITY_CHANNEL("z", 0),
	TCS_INTENSITY_CHANNEL("y", 1),
	TCS_INTENSITY_CHANNEL("ir", 2),
	TCS_INTENSITY_CHANNEL("x", 3),
};

static const struct iio_info tcs_info = {
	.read_raw = tcs_read_raw,
};

static void tcs_power_off(void *data)
{
	struct spx_tcs3430 *s = data;

	tcs_write(s, TCS_ENABLE, 0);
}

static int tcs_probe(struct i2c_client *client)
{
	struct spx_tcs3430 *s;
	struct iio_dev *indio;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;

	ret = i2c_smbus_read_byte_data(client, TCS_ID);
	if (ret < 0)
		return ret;
	if ((ret & 0xfc) != 0xdc)
		return dev_err_probe(&client->dev, -ENODEV,
				     "not a TCS3430 (ID=0x%02x)\n", ret);

	indio = devm_iio_device_alloc(&client->dev, sizeof(*s));
	if (!indio)
		return -ENOMEM;

	s = iio_priv(indio);
	s->client = client;
	s->gain_code = 3; /* 64x default gain */
	mutex_init(&s->lock);

	tcs_write(s, TCS_ENABLE, 0x00);
	tcs_write(s, TCS_ATIME, TCS_CYCLES - 1);
	tcs_write(s, TCS_CFG1, s->gain_code);
	tcs_write(s, TCS_CFG2, 0x04);
	tcs_write(s, TCS_CFG3, 0x0c);
	tcs_write(s, TCS_INTENAB, 0x00);
	ret = tcs_write(s, TCS_ENABLE, 0x03); /* PON | AEN continuous */
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&client->dev, tcs_power_off, s);
	if (ret)
		return ret;

	indio->name = "spx-tcs3430";
	indio->info = &tcs_info;
	indio->modes = INDIO_DIRECT_MODE;
	indio->channels = tcs_channels;
	indio->num_channels = ARRAY_SIZE(tcs_channels);
	i2c_set_clientdata(client, s);

	return devm_iio_device_register(&client->dev, indio);
}

static int tcs_suspend(struct device *dev)
{
	struct spx_tcs3430 *s = dev_get_drvdata(dev);

	if (!s)
		return 0;
	mutex_lock(&s->lock);
	tcs_write(s, TCS_ENABLE, 0x00);
	mutex_unlock(&s->lock);
	return 0;
}

static int tcs_resume(struct device *dev)
{
	struct spx_tcs3430 *s = dev_get_drvdata(dev);

	if (!s)
		return 0;
	mutex_lock(&s->lock);
	tcs_write(s, TCS_ATIME, TCS_CYCLES - 1);
	tcs_write(s, TCS_CFG1, s->gain_code);
	tcs_write(s, TCS_ENABLE, 0x03);
	mutex_unlock(&s->lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tcs_pm_ops, tcs_suspend, tcs_resume);

static const struct of_device_id tcs_of_match[] = {
	{ .compatible = "ams,tcs3430" },
	{ .compatible = "avago,apds9960" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcs_of_match);

static const struct i2c_device_id tcs_ids[] = {
	{ "tcs3430", 0 },
	{ "apds9960", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tcs_ids);

static struct i2c_driver tcs_driver = {
	.driver = {
		.name = "spx-tcs3430",
		.of_match_table = tcs_of_match,
		.pm = pm_sleep_ptr(&tcs_pm_ops),
	},
	.probe = tcs_probe,
	.id_table = tcs_ids,
};
module_i2c_driver(tcs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Surface Pro X ams TCS3430 Ambient Light & XYZ IIO driver");
