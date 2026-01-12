/*
 * pwm-fan.c - Hwmon driver for fans connected to PWM lines.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Author: Kamil Debski <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>

#define MAX_PWM 255

struct pwm_fan_ctx {
	struct mutex lock;
	struct pwm_device *pwm;
	unsigned int pwm_value;
	unsigned int pwm_fan_state;
	unsigned int pwm_fan_max_state;
	unsigned int *pwm_fan_cooling_levels;
	struct thermal_cooling_device *cdev;
};





static int pwm_fan_probe(struct platform_device *pdev)
{
	struct pwm_fan_ctx *ctx;
	int ret;
	struct pwm_state state = { };

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->pwm = devm_of_pwm_get(&pdev->dev, pdev->dev.of_node, NULL);
	if (IS_ERR(ctx->pwm)) {
		ret = PTR_ERR(ctx->pwm);

		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Could not get PWM: %d\n", ret);
			printk("[pwmfan]Could not get PWM: %d\n", ret);
		return ret;
		printk("[pwmfan] PWM: %d\n", ret);
	}

	platform_set_drvdata(pdev, ctx);
	
	printk("[pwmfan]platform_set_drvdata success");
	ctx->pwm_value = MAX_PWM;

	/* Set duty cycle to maximum allowed and enable PWM output */
	pwm_init_state(ctx->pwm, &state);
	state.duty_cycle = ctx->pwm->args.period - 1;
	state.enabled = true;

	ret = pwm_apply_state(ctx->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure PWM\n");
		printk("[pwmfan]Failed to configure PWM");
		return ret;
	}

	
	return 0;

// err_pwm_disable:
// 	state.enabled = false;
// 	pwm_apply_state(ctx->pwm, &state);

	// return ret;
}

static int pwm_fan_remove(struct platform_device *pdev)
{
	struct pwm_fan_ctx *ctx = platform_get_drvdata(pdev);

	if (ctx->pwm_value)
		pwm_disable(ctx->pwm);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwm_fan_suspend(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	struct pwm_args args;
	int ret;

	pwm_get_args(ctx->pwm, &args);

	if (ctx->pwm_value) {
		ret = pwm_config(ctx->pwm, 0, args.period);
		if (ret < 0)
			return ret;

		pwm_disable(ctx->pwm);
	}

	return 0;
}

static int pwm_fan_resume(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	struct pwm_args pargs;
	unsigned long duty;
	int ret;

	if (ctx->pwm_value == 0)
		return 0;

	pwm_get_args(ctx->pwm, &pargs);
	duty = DIV_ROUND_UP_ULL(ctx->pwm_value * (pargs.period - 1), MAX_PWM);
	ret = pwm_config(ctx->pwm, duty, pargs.period);
	if (ret)
		return ret;
	return pwm_enable(ctx->pwm);
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_fan_pm, pwm_fan_suspend, pwm_fan_resume);

static const struct of_device_id of_pwm_fan_match[] = {
	{ .compatible = "pwm-fan-fixed", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_fan_match);

static struct platform_driver pwm_fan_driver = {
	.probe		= pwm_fan_probe,
	.remove		= pwm_fan_remove,
	.driver	= {
		.name		= "pwm-fan-fixed",
		.pm		= &pwm_fan_pm,
		.of_match_table	= of_pwm_fan_match,
	},
};

module_platform_driver(pwm_fan_driver);

MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_ALIAS("platform:pwm-fan");
MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
