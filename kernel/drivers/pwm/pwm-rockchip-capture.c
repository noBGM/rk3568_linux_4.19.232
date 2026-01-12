#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "rockchip_pwm_capture.h"

// 打开字符设备的回调函数
static int cdev_test_open(struct inode *inode, struct file *file) {
    struct rkxx_capture_drvdata *ddata;
    struct pwm_capture_cdev *pcdev;

    printk("This is cdev_test_open\n");

    // 从 inode 中获取设备数据
    pcdev = container_of(inode->i_cdev, struct pwm_capture_cdev, cdev_test);
    ddata = container_of(pcdev, struct rkxx_capture_drvdata, pwm_cdev);

    if (!ddata) {
        printk(KERN_ERR "Failed to get device data\n");
        return -ENODEV;
    }

    // 将设备数据保存到文件私有数据中
    file->private_data = ddata;

    return 0;
}

// 读取字符设备的回调函数
static ssize_t cdev_test_read(struct file *file, char __user *buf, size_t size, loff_t *off) {
    struct rkxx_capture_drvdata *ddata;
    int val, i, ret;

    ddata = file->private_data;

    if (!ddata) {
        printk(KERN_ERR "Device data is NULL\n");
        return -EINVAL;
    }

    // 初始化捕获数据
    ddata->lpr = 0;
    ddata->hpr = 0;
    ddata->state = RMC_IDLE1;

    // 启用 PWM
    val = readl_relaxed(ddata->base + PWM_REG_CTRL);
    val = (val & 0xFFFFFFFE) | PWM_ENABLE;
    writel_relaxed(val, ddata->base + PWM_REG_CTRL);

    // 等待数据捕获完成
    for (i = 0; i < 100; i++) {
        msleep(1);
        if (ddata->state == RMC_DONE && ddata->hpr && ddata->lpr) {
            printk("capture ok!\n");
            break;
        }
    }

    // 禁用 PWM
    val = readl_relaxed(ddata->base + PWM_REG_CTRL);
    val = (val & 0xFFFFFFFE) | PWM_DISABLE;
    writel_relaxed(val, ddata->base + PWM_REG_CTRL);

    if (ddata->hpr == 0 || ddata->lpr == 0) {
        printk(KERN_ERR "Failed to capture PWM data\n");
        return -EIO;
    }

    // 计算周期和占空比
    ddata->data.period_ns = (ddata->lpr + ddata->hpr) * ddata->pwm_freq_nstime;
    ddata->data.duty_ns = ddata->hpr * ddata->pwm_freq_nstime;

    // 将数据拷贝到用户空间
    ret = copy_to_user(buf, &ddata->data, size);
    if (ret) {
        printk(KERN_ERR "Failed to copy data to user space\n");
        return -EFAULT;
    }

    printk("This is cdev_test_read\n");

    ddata->state = RMC_IDLE;

    return size;
}

// 释放字符设备的回调函数
static int cdev_test_release(struct inode *inode, struct file *file) {
    printk("This is cdev_test_release\n");
    return 0;
}

// 字符设备操作函数结构体
static struct file_operations cdev_test_ops = {
    .owner = THIS_MODULE,
    .open = cdev_test_open,
    .read = cdev_test_read,
    .release = cdev_test_release
};

// PWM 捕获中断处理函数
irqreturn_t rk_pwm_capture(int irq, void *dev_id) {
    struct rkxx_capture_drvdata *ddata = dev_id;
    unsigned int channel = ddata->pwm_channel;
    int val, lpr, hpr;

    val = readl_relaxed(ddata->base + PWM_REG_INTSTS(channel));
    if ((val & PWM_CH_INT(channel)) == 0) {
        return IRQ_NONE;
    }

    // 根据极性读取 lpr 或 hpr
    if ((val & PWM_CH_POL(channel)) == 0) {
        if (ddata->state != RMC_DONE) {
            lpr = readl_relaxed(ddata->base + PWM_REG_LPR);
            ddata->lpr = lpr;
        }
    } else {
        if (ddata->state != RMC_DONE) {
            hpr = readl_relaxed(ddata->base + PWM_REG_HPR);
            ddata->hpr = hpr;
        }
    }

    // 清除中断状态
    writel_relaxed(PWM_CH_INT(channel), ddata->base + PWM_REG_INTSTS(channel));

    // 状态机处理
    switch (ddata->state) {
    case RMC_IDLE1:
        ddata->hpr = 0;
        ddata->lpr = 0;
        ddata->state = RMC_IDLE2;
        break;
    case RMC_IDLE2:
        ddata->hpr = 0;
        ddata->lpr = 0;
        ddata->state = RMC_GETDATA;
        break;
    case RMC_GETDATA:
        printk("ddata->hpr is %d, ddata->lpr is %d\n", ddata->hpr, ddata->lpr);
        if (ddata->hpr && ddata->lpr) {
            ddata->state = RMC_DONE;
        }
        break;
    default:
        break;
    }

    return IRQ_HANDLED;
}

// 驱动探测函数
int capture_probe(struct platform_device *pdev)
{
    int ret;
    struct rkxx_capture_drvdata *ddata;
    struct resource *r;
    struct clk *clk;
    struct clk *p_clk;
    struct device_node *np = pdev->dev.of_node;
    int pwm_channel;
    int irq;
    struct pwm_capture_cdev *pcdev;
    int freq;

    // 分配驱动数据结构
    ddata = devm_kzalloc(&pdev->dev, sizeof(struct rkxx_capture_drvdata), GFP_KERNEL);
    if (!ddata) {
        dev_err(&pdev->dev, "Failed to allocate memory for driver data\n");
        return -ENOMEM;
    }
    ddata->state = RMC_IDLE;

    // 获取资源
    r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    ddata->base = devm_ioremap_resource(&pdev->dev, r);
    if (IS_ERR(ddata->base)) {
        dev_err(&pdev->dev, "Failed to map memory resource\n");
        return PTR_ERR(ddata->base);
    }
    ddata->dev = pdev->dev;

    // 获取时钟
    clk = devm_clk_get(&pdev->dev, "pwm");
    if (IS_ERR(clk)) {
        dev_err(&pdev->dev, "Failed to get PWM clock\n");
        return PTR_ERR(clk);
    }
    ddata->clk = clk;

    p_clk = devm_clk_get(&pdev->dev, "pclk");
    if (IS_ERR(p_clk)) {
        dev_err(&pdev->dev, "Failed to get peripheral clock\n");
        return PTR_ERR(p_clk);
    }
    ddata->p_clk = p_clk;

    // 从设备树中读取 PWM 通道
    ret = of_property_read_u32(np, "pwm-channel", &pwm_channel);
    if (ret) {
        dev_err(&pdev->dev, "Failed to get PWM channel from device tree\n");
        return ret;
    }
    pwm_channel %= 4;
    ddata->pwm_channel = pwm_channel;

    // 获取中断号
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        dev_err(&pdev->dev, "Failed to get IRQ\n");
        return irq;
    }
    ddata->irq = irq;

    // 设置驱动数据
    platform_set_drvdata(pdev, ddata);

    // 请求中断
    ret = devm_request_irq(&pdev->dev, irq, rk_pwm_capture, IRQF_NO_SUSPEND, "rk_pwm_capture_irq", ddata);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ\n");
        return ret;
    }

    // 启用时钟
    ret = clk_prepare_enable(ddata->clk);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PWM clock\n");
        return ret;
    }

    ret = clk_prepare_enable(ddata->p_clk);
    if (ret) {
        clk_disable_unprepare(ddata->clk);
        dev_err(&pdev->dev, "Failed to enable peripheral clock\n");
        return ret;
    }

    // 计算 PWM 频率
    freq = clk_get_rate(ddata->clk) / 64;
    ddata->pwm_freq_nstime = 1000000000 / freq;

    // 注册字符设备
    pcdev = &ddata->pwm_cdev;
    ret = alloc_chrdev_region(&pcdev->dev_num, 0, 1, "alloc_name");
    if (ret < 0) {
        dev_err(&pdev->dev, "alloc_chrdev_region error\n");
        goto err_alloc_chrdev;
    }
    printk("alloc_chrdev_region success\n");

    pcdev->cdev_test.owner = THIS_MODULE;
    cdev_init(&pcdev->cdev_test, &cdev_test_ops);
    ret = cdev_add(&pcdev->cdev_test, pcdev->dev_num, 1);
    if (ret) {
        dev_err(&pdev->dev, "Failed to add cdev\n");
        goto err_cdev_add;
    }

    // 创建设备类
    pcdev->class = class_create(THIS_MODULE, "test");
    if (IS_ERR(pcdev->class)) {
        ret = PTR_ERR(pcdev->class);
        dev_err(&pdev->dev, "Failed to create class\n");
        goto err_class_create;
    }

    // 创建设备
    pcdev->device = device_create(pcdev->class, NULL, pcdev->dev_num, NULL, "capture");
    if (IS_ERR(pcdev->device)) {
        ret = PTR_ERR(pcdev->device);
        dev_err(&pdev->dev, "Failed to create device\n");
        goto err_device_create;
    }

    rk_pwm_capture_init(ddata->base, ddata->pwm_channel);

    return 0;

err_device_create:
    class_destroy(pcdev->class);
err_class_create:
    cdev_del(&pcdev->cdev_test);
err_cdev_add:
    unregister_chrdev_region(pcdev->dev_num, 1);
err_alloc_chrdev:
    clk_disable_unprepare(ddata->p_clk);
    clk_disable_unprepare(ddata->clk);
    return ret;
}

// 驱动移除函数
int capture_remove(struct platform_device *pdev)
{
    struct rkxx_capture_drvdata *ddata = platform_get_drvdata(pdev);
    struct pwm_capture_cdev *pcdev = &ddata->pwm_cdev;

    device_destroy(pcdev->class, pcdev->dev_num);
    class_destroy(pcdev->class);
    cdev_del(&pcdev->cdev_test);
    unregister_chrdev_region(pcdev->dev_num, 1);

    clk_disable_unprepare(ddata->p_clk);
    clk_disable_unprepare(ddata->clk);

    return 0;
}

// 设备树匹配表
const struct of_device_id capture_of_device_id[] = {
    {.compatible = "pwm-capture"},
    {}
};

// 平台驱动结构体
struct platform_driver capture_platform_driver = {
    .driver = {
        .name = "pwm-capture",
        .of_match_table = capture_of_device_id,
    },
    .probe = capture_probe,
    .remove = capture_remove,
};

// 模块初始化函数
static int __init modulecdev_init(void)
{
    return platform_driver_register(&capture_platform_driver);
}

// 模块退出函数
static void __exit modulecdev_exit(void)
{
    platform_driver_unregister(&capture_platform_driver);
}

module_init(modulecdev_init);
module_exit(modulecdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("topeet");
