/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __RKXX_PWM_REMOTECTL_H__
#define __RKXX_PWM_REMOTECTL_H__

#include <linux/input.h>
#include <linux/pwm.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>

/* 最大按键数 */
#define MAX_NUM_KEYS                60
/* 最大PWM捕获数 */
#define PWM_PWR_KEY_CAPURURE_MAX    10

/* PWM寄存器定义 */
#define PWM_REG_CNTR                0x00  /* 计数器寄存器 */
#define PWM_REG_HPR                 0x04  /* 周期寄存器 */
#define PWM_REG_LPR                 0x08  /* 占空比寄存器 */
#define PWM_REG_CTRL                0x0c  /* 控制寄存器 */
#define PWM3_REG_INTSTS             0x10  /* PWM3中断状态寄存器 */
#define PWM2_REG_INTSTS             0x20  /* PWM2中断状态寄存器 */
#define PWM1_REG_INTSTS             0x30  /* PWM1中断状态寄存器 */
#define PWM0_REG_INTSTS             0x40  /* PWM0中断状态寄存器 */
#define PWM3_REG_INT_EN             0x14  /* PWM3中断使能寄存器 */
#define PWM2_REG_INT_EN             0x24  /* PWM2中断使能寄存器 */
#define PWM1_REG_INT_EN             0x34  /* PWM1中断使能寄存器 */
#define PWM0_REG_INT_EN             0x44  /* PWM0中断使能寄存器 */

/* 控制寄存器位定义 */
#define PWM_ENABLE                  (1 << 0) /* PWM使能 */
#define PWM_DISABLE                 (0 << 0) /* PWM禁用 */

/* 操作模式 */
#define PWM_MODE_ONESHOT            (0x00 << 1) /* 单次模式 */
#define PWM_MODE_CONTINUMOUS        (0x01 << 1) /* 连续模式 */
#define PWM_MODE_CAPTURE            (0x02 << 1) /* 捕获模式 */

/* 占空比输出极性 */
#define PWM_DUTY_POSTIVE            (0x01 << 3) /* 正极性 */
#define PWM_DUTY_NEGATIVE           (0x00 << 3) /* 负极性 */

/* 非活动状态输出极性 */
#define PWM_INACTIVE_POSTIVE        (0x01 << 4) /* 正极性 */
#define PWM_INACTIVE_NEGATIVE       (0x00 << 4) /* 负极性 */

/* 时钟源选择 */
#define PWM_CLK_SCALE               (1 << 9) /* 时钟分频 */
#define PWM_CLK_NON_SCALE           (0 << 9) /* 无时钟分频 */

#define PWM_CH0_INT                 (1 << 0)
#define PWM_CH1_INT                 (1 << 1)
#define PWM_CH2_INT                 (1 << 2)
#define PWM_CH3_INT                 (1 << 3)
#define PWM_PWR_KEY_INT             (1 << 7)

#define PWM_CH0_POL                 (1 << 8)
#define PWM_CH1_POL                 (1 << 9)
#define PWM_CH2_POL                 (1 << 10)
#define PWM_CH3_POL                 (1 << 11)

#define PWM_CH0_INT_ENABLE          (1 << 0)
#define PWM_CH0_INT_DISABLE         (0 << 0)

#define PWM_CH1_INT_ENABLE          (1 << 1)
#define PWM_CH1_INT_DISABLE         (0 << 1)

#define PWM_CH2_INT_ENABLE          (1 << 2)
#define PWM_CH2_INT_DISABLE         (0 << 2)

#define PWM_CH3_INT_ENABLE          (1 << 3)
#define PWM_CH3_INT_DISABLE         (0 << 3)

#define PWM_INT_ENABLE              1
#define PWM_INT_DISABLE             0

/* 预分频因子 */
#define PWMCR_MIN_PRESCALE          0x00
#define PWMCR_MAX_PRESCALE          0x07

#define PWMDCR_MIN_DUTY             0x0001
#define PWMDCR_MAX_DUTY             0xFFFF

#define PWMPCR_MIN_PERIOD           0x0001
#define PWMPCR_MAX_PERIOD           0xFFFF

enum pwm_div {
    PWM_DIV1    = (0x0 << 12),
    PWM_DIV2    = (0x1 << 12),
    PWM_DIV4    = (0x2 << 12),
    PWM_DIV8    = (0x3 << 12),
    PWM_DIV16   = (0x4 << 12),
    PWM_DIV32   = (0x5 << 12),
    PWM_DIV64   = (0x6 << 12),
    PWM_DIV128  = (0x7 << 12),
};

/* NEC 协议 */
#define RK_PWM_TIME_PRE_MIN         4000
#define RK_PWM_TIME_PRE_MAX         5000

#define RK_PWM_TIME_PRE_MIN_LOW     8000
#define RK_PWM_TIME_PRE_MAX_LOW     10000

#define RK_PWM_TIME_BIT0_MIN        390
#define RK_PWM_TIME_BIT0_MAX        730

#define RK_PWM_TIME_BIT1_MIN        1300
#define RK_PWM_TIME_BIT1_MAX        2000

#define RK_PWM_TIME_BIT_MIN_LOW     390
#define RK_PWM_TIME_BIT_MAX_LOW     730

#define RK_PWM_TIME_RPT_MIN         2000
#define RK_PWM_TIME_RPT_MAX         2500

#define RK_PWM_TIME_SEQ1_MIN        95000
#define RK_PWM_TIME_SEQ1_MAX        98000

#define RK_PWM_TIME_SEQ2_MIN        30000
#define RK_PWM_TIME_SEQ2_MAX        55000

#define PWM_REG_INTSTS(n)           ((3 - (n)) * 0x10 + 0x10)
#define PWM_REG_INT_EN(n)           ((3 - (n)) * 0x10 + 0x14)
#define RK_PWM_VERSION_ID(n)        ((3 - (n)) * 0x10 + 0x2c)
#define PWM_REG_PWRMATCH_CTRL(n)    ((3 - (n)) * 0x10 + 0x50)
#define PWM_REG_PWRMATCH_LPRE(n)    ((3 - (n)) * 0x10 + 0x54)
#define PWM_REG_PWRMATCH_HPRE(n)    ((3 - (n)) * 0x10 + 0x58)
#define PWM_REG_PWRMATCH_LD(n)      ((3 - (n)) * 0x10 + 0x5C)
#define PWM_REG_PWRMATCH_HD_ZERO(n) ((3 - (n)) * 0x10 + 0x60)
#define PWM_REG_PWRMATCH_HD_ONE(n)  ((3 - (n)) * 0x10 + 0x64)
#define PWM_PWRMATCH_VALUE(n)       ((3 - (n)) * 0x10 + 0x68)
#define PWM_PWRCAPTURE_VALUE(n)     ((3 - (n)) * 0x10 + 0x9c)

#define PWM_CH_INT(n)               BIT(n)
#define PWM_CH_POL(n)               BIT(n + 8)

#define PWM_CH_INT_ENABLE(n)        BIT(n)
#define PWM_PWR_INT_ENABLE          BIT(7)
#define CH3_PWRKEY_ENABLE           BIT(3)

/* PWM 数据结构 */
struct pwm_data {
    int period_ns; /* 周期（纳秒） */
    int duty_ns;   /* 占空比（纳秒） */
};

/* PWM 状态枚举 */
typedef enum _RMC_STATE {
    RMC_IDLE,   /* 空闲状态 */
    RMC_IDLE1,  /* 空闲状态1 */
    RMC_IDLE2,  /* 空闲状态2 */
    RMC_GETDATA,/* 获取数据状态 */
    RMC_DONE,   /* 完成状态 */
} eRMC_STATE;

/* PWM 捕获平台数据结构 */
struct RKxx_remotectl_platform_data {
    int nbuttons; /* 按钮数 */
    int rep;      /* 重复 */
    int timer;    /* 计时器 */
    int wakeup;   /* 唤醒 */
};

/* PWM 捕获字符设备数据结构 */
struct pwm_capture_cdev {
    dev_t dev_num;            /* 设备号 */
    struct cdev cdev_test;    /* 字符设备结构体 */
    struct class *class;      /* 设备类 */
    struct device *device;    /* 设备结构体 */
    struct rkxx_capture_drvdata *ddata; /* 驱动数据 */
};

/* PWM 捕获驱动数据结构 */
struct rkxx_capture_drvdata {
    void __iomem *base;       /* 基地址 */
    int irq;                  /* 中断号 */
    struct device dev;        /* 设备结构体 */
    int pwm_freq_nstime;      /* PWM 频率（纳秒） */
    int pwm_channel;          /* PWM 通道 */
    int hpr;                  /* 高电平周期 */
    int lpr;                  /* 低电平周期 */
    eRMC_STATE state;         /* PWM 状态 */
    struct clk *clk;          /* 时钟 */
    struct clk *p_clk;        /* 父时钟 */
    struct pwm_capture_cdev pwm_cdev; /* PWM 捕获字符设备 */
    struct pwm_data data;             /* PWM 数据结构 */
};

/* PWM 中断控制 */
static void rk_pwm_int_ctrl(void __iomem *pwm_base, uint pwm_id, int ctrl)
{
    int val;

    if (pwm_id > 3)
        return; /* 如果 PWM ID 超过 3，直接返回 */
    
    val = readl_relaxed(pwm_base + PWM_REG_INT_EN(pwm_id)); /* 读取当前中断使能状态 */
    
    if (ctrl) {
        val |= PWM_CH_INT_ENABLE(pwm_id); /* 设置中断使能 */
        writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id)); /* 写入中断使能寄存器 */
    } else {
        val &= ~PWM_CH_INT_ENABLE(pwm_id); /* 清除中断使能 */
        writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id)); /* 写入中断使能寄存器 */
    }
}

/* 初始化 PWM 捕获 */
static void rk_pwm_capture_init(void __iomem *pwm_base, uint pwm_id)
{
    int val;

    /* 禁用 PWM */
    val = readl_relaxed(pwm_base + PWM_REG_CTRL);
    val = (val & 0xFFFFFFFE) | PWM_DISABLE;
    writel_relaxed(val, pwm_base + PWM_REG_CTRL);

    /* 设置为捕获模式 */
    val = readl_relaxed(pwm_base + PWM_REG_CTRL);
    val = (val & 0xFFFFFFF9) | PWM_MODE_CAPTURE;
    writel_relaxed(val, pwm_base + PWM_REG_CTRL);

    /* 设置分频值 */
    val = readl_relaxed(pwm_base + PWM_REG_CTRL);
    val = (val & 0xFF0001FF) | PWM_DIV64;
    writel_relaxed(val, pwm_base + PWM_REG_CTRL);

    /* 启用中断 */
    rk_pwm_int_ctrl(pwm_base, pwm_id, PWM_INT_ENABLE);

    /* 这里可以启用 PWM 捕获（注释掉的代码） */
    /*
    val = readl_relaxed(pwm_base + PWM_REG_CTRL);
    val = (val & 0xFFFFFFFE) | PWM_ENABLE;
    writel_relaxed(val, pwm_base + PWM_REG_CTRL);
    */
}

#endif
