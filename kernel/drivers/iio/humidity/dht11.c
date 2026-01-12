/*
 * DHT11/DHT22 bit banging GPIO driver
 *
 * Copyright (c) Harald Geyer <harald@ccbib.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/timekeeping.h>

#include <linux/iio/iio.h>

#define DRIVER_NAME	"dht11"

#define DHT11_DATA_VALID_TIME	2000000000  /* 2s in ns */

#define DHT11_EDGES_PREAMBLE 2
#define DHT11_BITS_PER_READ 40
/*
 * Note that when reading the sensor actually 84 edges are detected, but
 * since the last edge is not significant, we only store 83:
 */
#define DHT11_EDGES_PER_READ (2 * DHT11_BITS_PER_READ + \
			      DHT11_EDGES_PREAMBLE + 1)

/*
 * Data transmission timing:
 * Data bits are encoded as pulse length (high time) on the data line.
 * 0-bit: 22-30uS -- typically 26uS (AM2302)
 * 1-bit: 68-75uS -- typically 70uS (AM2302)
 * The acutal timings also depend on the properties of the cable, with
 * longer cables typically making pulses shorter.
 *
 * Our decoding depends on the time resolution of the system:
 * timeres > 34uS ... don't know what a 1-tick pulse is
 * 34uS > timeres > 30uS ... no problem (30kHz and 32kHz clocks)
 * 30uS > timeres > 23uS ... don't know what a 2-tick pulse is
 * timeres < 23uS ... no problem
 *
 * Luckily clocks in the 33-44kHz range are quite uncommon, so we can
 * support most systems if the threshold for decoding a pulse as 1-bit
 * is chosen carefully. If somebody really wants to support clocks around
 * 40kHz, where this driver is most unreliable, there are two options.
 * a) select an implementation using busy loop polling on those systems
 * b) use the checksum to do some probabilistic decoding
 */
#define DHT11_START_TRANSMISSION_MIN	18000  /* us */
#define DHT11_START_TRANSMISSION_MAX	20000  /* us */
#define DHT11_MIN_TIMERES	80000  /* ns */
#define DHT11_THRESHOLD		49000  /* ns */
#define DHT11_AMBIG_LOW		28000  /* ns */
#define DHT11_AMBIG_HIGH	70000  /* ns */

struct dht11 {
	struct device			*dev;

	int				gpio;
	int				irq;

	struct completion		completion;
	/* The iio sysfs interface doesn't prevent concurrent reads: */
	struct mutex			lock;

	s64				timestamp;
	int				temperature;
	int				humidity;

	/* num_edges: -1 means "no transmission in progress" */
	int				num_edges;
	struct {s64 ts; int value; }	edges[DHT11_EDGES_PER_READ];
};

static int dht11_read_raw(struct iio_dev *iio_dev,
			  const struct iio_chan_spec *chan,
			int *val, int *val2, long m)
{
	struct dht11 *dht11 = iio_priv(iio_dev);
	int ret, timeres;
	int i, j, data = 0, bit = 0;
	unsigned char temp_int, temp_dec, hum_int, hum_dec, checksum;

	mutex_lock(&dht11->lock);
	if (dht11->timestamp + DHT11_DATA_VALID_TIME < ktime_get_boot_ns()) {
		timeres = ktime_get_resolution_ns();
		dev_dbg(dht11->dev, "current timeresolution: %dns\n", timeres);
		if (timeres > DHT11_MIN_TIMERES) {
			dev_err(dht11->dev, "timeresolution %dns too low\n",
				timeres);
			/* In theory a better clock could become available
			 * at some point ... and there is no error code
			 * that really fits better.
			 */
			ret = -EAGAIN;
			goto err;
		}
		if (timeres > DHT11_AMBIG_LOW && timeres < DHT11_AMBIG_HIGH)
			dev_warn(dht11->dev,
				 "timeresolution: %dns - decoding ambiguous\n",
				 timeres);

		reinit_completion(&dht11->completion);

		dht11->num_edges = 0;
		ret = gpio_direction_output(dht11->gpio, 0);
		if (ret)
			goto err;
		usleep_range(DHT11_START_TRANSMISSION_MIN,
			     DHT11_START_TRANSMISSION_MAX);
		ret = gpio_direction_output(dht11->gpio, 1);
		if (ret)
			goto err;
		ret = gpio_direction_input(dht11->gpio);
		if (ret)
			goto err;
		udelay(30);
		if (gpio_get_value(dht11->gpio) == 0)
		{
			while(!gpio_get_value(dht11->gpio))
			{
				udelay(80);
				break;
			}
			while(gpio_get_value(dht11->gpio))
			{
				udelay(80);
				break;
			}
			for(i = 0; i < 8; i++)
			{
				j = 0;
				while(!gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				udelay(30);
				bit = 0;
				if (gpio_get_value(dht11->gpio))
				{
					bit = 1;
				}
				j = 0;
				while(gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				data <<= 1;
				data |= bit;
			}
			hum_int = data;
			//printk("hum_int = %d \n", hum_int);

			data = 0;
			for(i = 0; i < 8; i++)
			{
				j = 0;
				while(!gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				udelay(30);
				bit = 0;
				if (gpio_get_value(dht11->gpio))
				{
					bit = 1;
				}
				j = 0;
				while(gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				data <<= 1;
				data |= bit;
			}
			hum_dec = data;
			//printk("hum_dec = %d \n", hum_dec);

			data = 0;
			for(i = 0; i < 8; i++)
			{
				j = 0;
				while(!gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				udelay(30);
				bit = 0;
				if (gpio_get_value(dht11->gpio))
				{
					bit = 1;
				}
				j = 0;
				while(gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				data <<= 1;
				data |= bit;
			}
			temp_int = data;
			//printk("temp_int = %d \n", temp_int);

			data = 0;
			for(i = 0; i < 8; i++)
			{
				j = 0;
				while(!gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				udelay(30);
				bit = 0;
				if (gpio_get_value(dht11->gpio))
				{
					bit = 1;
				}
				j = 0;
				while(gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				data <<= 1;
				data |= bit;
			}
			temp_dec = data;
			//printk("temp_dec = %d \n", temp_dec);

			data = 0;
			for(i = 0; i < 8; i++)
			{
				j = 0;
				while(!gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				udelay(30);
				bit = 0;
				if (gpio_get_value(dht11->gpio))
				{
					bit = 1;
				}
				j = 0;
				while(gpio_get_value(dht11->gpio))
				{
					if (++j >= 50000)
						break;
				}
				data <<= 1;
				data |= bit;
			}
			checksum = data;
			//printk("checksum = %d \n", checksum);

		}
		if (checksum == hum_int + hum_dec + temp_int + temp_dec)
		{
			dht11->humidity = hum_int;
			//printk("humidity = %d \n", dht11->humidity);
			if (temp_dec & 0x80)
			{
				dht11->temperature = 0 - temp_int;
			}
			else
				dht11->temperature = temp_int;
			//printk("temperature = %d \n", dht11->temperature);
		}
		else
		{
			printk("checksum err !!!\n");
		}

	}

	ret = IIO_VAL_INT;
	if (chan->type == IIO_TEMP)
	{
		*val = dht11->temperature;
		//printk("IIO_TEMP \n");
	}
	else if (chan->type == IIO_HUMIDITYRELATIVE)
	{
		*val = dht11->humidity;
		//printk("IIO_HUMIDITYRELATIVE \n");
	}
	else
		ret = -EINVAL;
err:
	dht11->num_edges = -1;
	mutex_unlock(&dht11->lock);
	return ret;
}

static const struct iio_info dht11_iio_info = {
	.read_raw		= dht11_read_raw,
};

static const struct iio_chan_spec dht11_chan_spec[] = {
	{ .type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), },
	{ .type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), }
};

static const struct of_device_id dht11_dt_ids[] = {
	{ .compatible = "dht11", },
	{ }
};
MODULE_DEVICE_TABLE(of, dht11_dt_ids);

static int dht11_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct dht11 *dht11;
	struct iio_dev *iio;
	int ret;

	iio = devm_iio_device_alloc(dev, sizeof(*dht11));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	dht11 = iio_priv(iio);
	dht11->dev = dev;

	ret = of_get_gpio(node, 0);
	if (ret < 0)
		return ret;
	dht11->gpio = ret;
	ret = devm_gpio_request_one(dev, dht11->gpio, GPIOF_IN, pdev->name);
	if (ret)
		return ret;

	dht11->irq = gpio_to_irq(dht11->gpio);
	if (dht11->irq < 0) {
		dev_err(dev, "GPIO %d has no interrupt\n", dht11->gpio);
		return -EINVAL;
	}

	dht11->timestamp = ktime_get_boot_ns() - DHT11_DATA_VALID_TIME - 1;
	dht11->num_edges = -1;

	platform_set_drvdata(pdev, iio);

	init_completion(&dht11->completion);
	mutex_init(&dht11->lock);
	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &dht11_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = dht11_chan_spec;
	iio->num_channels = ARRAY_SIZE(dht11_chan_spec);

	return devm_iio_device_register(dev, iio);
}

static struct platform_driver dht11_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = dht11_dt_ids,
	},
	.probe  = dht11_probe,
};

module_platform_driver(dht11_driver);

MODULE_AUTHOR("Harald Geyer <harald@ccbib.org>");
MODULE_DESCRIPTION("DHT11 humidity/temperature sensor driver");
MODULE_LICENSE("GPL v2");
