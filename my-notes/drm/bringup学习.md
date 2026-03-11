# 屏幕 Bringup 一个月逆袭计划

> **前提**：有 Linux 驱动开发基础，了解内核基础子系统和驱动框架，但对显示领域零基础。
> **目标**：一个月后能独立完成一块新屏（MIPI DSI / LVDS / eDP）在 RK3568 Linux 平台上的 bringup。
> **平台**：RK3568 (Topeet 开发板)，内核基于 Rockchip BSP (Linux 4.19/5.10)。

---

## 前置准备（第 0 天，周末搞定）

### 硬件准备
- [ ] 确认开发板型号、已有的屏幕模组（MIPI/LVDS/eDP/HDMI 各有哪些）
- [ ] 准备串口调试线（必须），USB-OTG 线（烧写固件用）
- [ ] 确认电源适配器功率足够（带屏至少 12V/2A）
- [ ] 准备万用表（后面排查电源、背光时要用）
- [ ] 如有逻辑分析仪或示波器更好（排查信号时用，没有也行）

### 软件环境准备
- [ ] 能编译整个 SDK 并成功烧写到板子（参考 `my-notes/drm/0.环境搭建笔记.md`）
- [ ] 能单独编译内核并替换烧写（节省迭代时间）
- [ ] 串口终端能正常连接，能看 dmesg
- [ ] 确认当前板子默认能点亮一块屏（当前配置是 eDP VGA，见 `topeet_screen_choose.dtsi`）

---

## 第一周：地基 —— 显示硬件原理 + DRM 框架认知

> **本周目标**：搞清楚「屏幕是怎么被点亮的」这个完整链路，从物理信号到软件框架。

### Day 1（周一）：显示系统的物理本质

**上午 — 搞懂一块屏到底是什么**

一块 LCD 屏的本质就是一个像素矩阵，SoC 需要做的事情：
1. 按照固定节奏（时钟）把每个像素的颜色数据发过去
2. 告诉屏幕「这一行完了」「这一帧完了」（同步信号）

需要理解的核心概念：
- **像素时钟（pixel clock / DCLK）**：每秒钟发送多少个像素，决定了刷新率
- **行同步（HSYNC）与场同步（VSYNC）**：分别标记一行和一帧的边界
- **消隐区（blanking）**：HBP、HFP、VBP、VFP —— 屏幕需要的「喘息时间」
- **公式**：`DCLK = (Hactive + HFP + HSYNC + HBP) × (Vactive + VFP + VSYNC + VBP) × FPS`

**实操**：
- 打开 `topeet_rk3568_lcds.dtsi`，找到 MIPI 屏的 `display-timings` 节点
- 用上面的公式手算一遍 pixel clock，和设备树里配的值对比
- 把这些参数画一张时序图（纸上画就行）

**下午 — 四大显示接口横向对比**

| 特性 | MIPI DSI | LVDS | eDP | HDMI |
|------|----------|------|-----|------|
| 典型场景 | 手机/平板/小尺寸 | 车载/工控 | 笔记本/高分辨率 | 显示器/电视 |
| 信号类型 | 差分串行 | 差分并行 | 差分串行 | TMDS 差分串行 |
| Lane 数 | 1-4 data lanes | 1-2 links | 1-4 lanes | 3 TMDS + 1 clock |
| 时钟 | 嵌入数据流 | 独立 clock pair | 嵌入数据流 | 独立 clock |
| 是否需要初始化命令 | 是（DCS 命令） | 否 | 协商式 | 协商式（EDID） |
| 典型带宽/lane | 1-1.5 Gbps | ~1 Gbps/link | 2.7/5.4 Gbps | ~3.4 Gbps |
| Bringup 难度 | ★★★ | ★★ | ★★★★ | ★（即插即用） |

**阅读材料**：
- MIPI DSI 规范概述（搜索 "MIPI DSI specification overview"，不需要买原版规范，看博客总结即可）
- 重点理解 Video Mode vs Command Mode 的区别

**今日产出**：在笔记里画出「SoC → 接口 → 屏」的信号流向图，标注每个接口的关键区别。

---

### Day 2（周二）：DRM/KMS 框架全貌

**上午 — DRM 子系统的设计哲学**

DRM (Direct Rendering Manager) / KMS (Kernel Mode Setting) 是 Linux 内核的显示子系统框架。
它把显示硬件抽象成 5 个核心对象：

```
用户空间 (Wayland/Xorg/直接 ioctl)
        │
        ▼
┌─────────────────────────────────────────┐
│              DRM Core                    │
│  ┌─────────────────────────────────┐    │
│  │     KMS (Kernel Mode Setting)    │    │
│  │                                  │    │
│  │  Framebuffer ──→ Plane ──→ CRTC ──→ Encoder ──→ Connector ──→ 屏幕
│  │  (像素数据)    (图层)   (扫描输出)  (信号转换)   (物理接口)        │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
```

**核心对象与 RK3568 硬件的对应关系**：

| DRM 抽象 | RK3568 硬件 | 说明 |
|----------|-------------|------|
| **CRTC** | VOP2 的 Video Port (VP0/VP1/VP2) | 负责扫描输出，混合图层 |
| **Plane** | VOP2 的 Layer (Cluster/Esmart/Smart) | 硬件图层，可叠加 |
| **Encoder** | DSI Host / HDMI TX / LVDS TX / eDP TX | 把像素数据转成特定协议的信号 |
| **Connector** | Panel / Monitor（屏幕本身） | 代表物理连接的显示设备 |
| **Framebuffer** | GEM Buffer (在 DDR 中) | 实际的像素数据存储 |

**实操**：阅读已有笔记 `my-notes/drm/驱动关系梳理.md`（你之前整理的很好），结合上面的表格，巩固理解。

**下午 — 在运行的系统上观察 DRM**

在板子上执行以下命令（当前是 eDP 或 HDMI 已点亮的情况）：

```bash
# 查看 DRM 设备
ls /dev/dri/

# 查看所有 KMS 对象（CRTC、Encoder、Connector、Plane）
cat /sys/kernel/debug/dri/0/summary     # Rockchip 特有
# 或者用 modetest 工具
modetest -M rockchip -c    # 查看 connectors
modetest -M rockchip -e    # 查看 encoders
modetest -M rockchip -p    # 查看 planes

# 查看当前分辨率和刷新率
cat /sys/class/drm/card0-*/status
cat /sys/class/drm/card0-*/modes

# 查看 VOP2 状态
cat /sys/kernel/debug/dri/0/state
```

**今日产出**：把 modetest 的输出粘贴到笔记里，标注每个对象对应的硬件是什么。

---

### Day 3（周三）：Rockchip DRM 驱动架构 + 代码走读（上）

**上午 — 主驱动框架：rockchip_drm_drv.c**

这是整个 Rockchip DRM 的入口，理解它的核心机制：**component 框架**。

```
rockchip_drm_drv.c (master device)
    ├── rockchip_drm_vop2.c   (component: 显示控制器)
    ├── dw-mipi-dsi.c         (component: MIPI DSI 控制器)
    ├── dw_hdmi-rockchip.c    (component: HDMI 控制器)
    ├── rockchip_lvds.c       (component: LVDS 控制器)
    └── analogix_dp-rockchip.c (component: eDP 控制器)
```

**阅读路线**（配合你已有的 `1-rockchip_drm_drv.c-笔记.md`）：
1. `rockchip_drm_platform_probe()` → 如何发现和注册所有 component
2. `rockchip_drm_bind()` → 所有 component 就绪后如何初始化 DRM 设备
3. 关注 `drm_mode_config_init()` / `drm_vblank_init()` / `drm_dev_register()`

**下午 — VOP2 驱动：rockchip_drm_vop2.c**

VOP2 是 RK3568 的显示控制器核心（配合你已有的 `2-rockchip_drm_vop2.c-笔记.md`）。

重点关注：
1. `vop2_bind()` → 初始化流程，创建 CRTC 和 Plane
2. `vop2_crtc_atomic_enable()` → 点亮屏幕时的关键函数
3. `vop2_crtc_atomic_flush()` → 每帧提交时做什么
4. Video Port (VP0/VP1/VP2) 与 Plane 的绑定关系

**今日产出**：画出 probe 和 bind 的时序图（函数调用顺序），标注关键函数。

---

### Day 4（周四）：Rockchip DRM 驱动架构 + 代码走读（下）

**上午 — MIPI DSI 驱动：dw-mipi-dsi.c**

这是你未来 bringup MIPI 屏最常打交道的驱动，重点理解：

1. `dw_mipi_dsi_bind()` → 创建 Encoder 和 Connector
2. `dw_mipi_dsi_encoder_enable()` → 使能显示输出的流程
3. `dw_mipi_dsi_host_transfer()` → 发送 DCS 命令给屏幕的底层实现
4. D-PHY 初始化：PLL 配置、Lane 数设置、HS/LP 切换
5. Video Mode 参数配置：如何把 display-timings 写入 DSI 寄存器

**下午 — Panel 驱动：panel-simple.c**

`panel-simple.c` 是最常用的 panel 驱动，适用于「只需要配时序、不需要初始化命令」的屏（典型如 LVDS、eDP）。

阅读重点：
1. `panel_simple_probe()` → 如何匹配设备树
2. `panel_simple_enable()` / `panel_simple_prepare()` → 上电和使能序列
3. `panel_simple_get_modes()` → 如何获取屏幕分辨率信息
4. 电源序列：`supply` → `enable_gpio` → `backlight` 的顺序和延时

对于需要 DCS 初始化命令的 MIPI 屏，通常需要写专用 panel 驱动（或用 `panel-simple` 的 DSI 变种）。

**今日产出**：在笔记里总结 panel 驱动的标准骨架（probe/enable/disable），以及何时需要写专用 panel 驱动。

---

### Day 5（周五）：设备树实战 + 第一周总结

**上午 — 设备树完全解剖**

以你板子上的 `topeet_rk3568_lcds.dtsi` 为蓝本，逐节点分析：

```
设备树显示链路：
panel 节点 → dsi 节点 → vop 节点 → display-subsystem 节点

关键属性：
├── panel 节点
│   ├── compatible        → 匹配哪个 panel 驱动
│   ├── reg               → DSI 虚拟通道号
│   ├── backlight          → 指向 pwm-backlight 节点
│   ├── power-supply       → 屏幕供电
│   ├── reset-gpios        → 复位脚
│   ├── enable-gpios       → 使能脚
│   └── display-timings    → 分辨率、时序参数
│
├── dsi 节点 (&dsi0 或 &dsi1)
│   ├── status = "okay"
│   ├── rockchip,lane-rate → DSI 时钟频率
│   └── 引用 panel 作为子节点或 remote-endpoint
│
├── vop 节点 (&vp0 / &vp1 / &vp2)
│   └── 通过 port/endpoint 连接到 dsi/hdmi/lvds/edp
│
└── 辅助节点
    ├── pwm-backlight      → PWM 占空比控制亮度
    ├── regulator           → 电源管理
    └── pinctrl            → 引脚复用配置
```

**实操**：
1. 阅读 `topeet_screen_choose.dtsi`，理解屏幕切换机制（通过宏定义选择）
2. 尝试切换一种屏幕配置（例如从 eDP 切到 HDMI），重新编译设备树，烧写验证
3. 如果有 MIPI 屏或 LVDS 屏，试着切换过去

**下午 — 第一周知识梳理**

把本周学到的内容整理成一张「全景图」，包含：
1. 硬件层面：SoC → VOP2 → DSI/LVDS/HDMI/eDP → Panel
2. 软件层面：DRM Core → rockchip_drm_drv → vop2 → encoder driver → panel driver
3. 配置层面：设备树中各节点的关系
4. 调试手段：modetest、debugfs、dmesg

---

## 第二周：进阶 —— 深入 MIPI DSI + 真实 Bringup 流程

> **本周目标**：掌握 MIPI DSI 屏幕 bringup 的完整流程，能读懂屏幕规格书（datasheet）。

### Day 6（周一）：读懂一份屏幕规格书

**上午 — 规格书的核心章节**

找一份你手头有的 MIPI 屏规格书（如果没有，找同事要或网上找一份公开的，如 ILI9881C 的）。

规格书中对 bringup 最关键的信息：

| 章节 | 关键信息 | 用途 |
|------|----------|------|
| General Description | 分辨率、接口类型、lane 数 | 基本参数确认 |
| Absolute Maximum Ratings | 电压/温度极限 | 别烧屏 |
| DC Characteristics | VDD、VDDIO 电压范围 | 配置 regulator |
| Timing Parameters | DCLK、HBP/HFP/VBP/VFP/HSYNC/VSYNC | 配置 display-timings |
| Power On/Off Sequence | 上电时序图 | 配置 panel 驱动中的延时 |
| Initial Code | 寄存器初始化序列 | 写入 panel 驱动的 init_sequence |
| Interface Timing | DSI clock 范围、HS/LP 时序 | 配置 DSI lane-rate |
| Connector Pin Definition | FPC 引脚定义 | 硬件连接确认 |

**实操**：拿一份规格书，把上面每一项对应的数值都摘抄出来。

**下午 — 从规格书到设备树的翻译**

以规格书的参数为输入，练习填写设备树：

```dts
/* 从规格书翻译过来的 panel 配置 */
panel {
    compatible = "simple-panel-dsi";  // 或自定义 compatible
    reg = <0>;                        // DSI virtual channel 0
    backlight = <&backlight>;
    power-supply = <&vcc3v3_lcd>;
    reset-gpios = <&gpio0 RK_PC2 GPIO_ACTIVE_LOW>;  // 看原理图确定
    enable-delay-ms = <35>;           // 规格书 Power On Sequence 中的 T2
    prepare-delay-ms = <6>;           // 规格书中的 T3
    unprepare-delay-ms = <0>;
    disable-delay-ms = <20>;
    init-delay-ms = <55>;             // 规格书中 reset 后到发命令的延时
    width-mm = <68>;
    height-mm = <121>;

    /* 以下全部来自规格书 Timing Parameters 章节 */
    display-timings {
        native-mode = <&timing0>;
        timing0: timing0 {
            clock-frequency = <65000000>;   // 手算或规格书直接给
            hactive = <800>;
            vactive = <1280>;
            hfront-porch = <40>;
            hsync-len = <10>;
            hback-porch = <40>;
            vfront-porch = <16>;
            vsync-len = <4>;
            vback-porch = <16>;
            hsync-active = <0>;
            vsync-active = <0>;
        };
    };
};
```

**今日产出**：完成一份「规格书 → 设备树参数」的对照表模板，以后每次 bringup 都能复用。

---

### Day 7（周二）：MIPI DSI 协议深入

**上午 — DSI 协议层次**

```
┌─────────────────────┐
│  Application Layer   │ ← DCS 命令 (Display Command Set)
├─────────────────────┤
│  Protocol Layer      │ ← 包格式：短包(4B) / 长包(6B+payload)
├─────────────────────┤
│  Lane Management     │ ← 数据分配到各 lane
├─────────────────────┤
│  PHY Layer           │ ← 差分信号、HS/LP 模式切换
└─────────────────────┘
```

**重点理解**：
1. **HS (High Speed) 模式**：高速传输像素数据，差分摆幅小 (~200mV)
2. **LP (Low Power) 模式**：低速传输命令，单端信号 (~1.2V)
3. **Video Mode**：SoC 持续推送像素数据（类似传统 RGB 接口），屏不需要 RAM
4. **Command Mode**：SoC 按需发送数据到屏的内置 RAM，适合低功耗场景
5. **DCS 命令**：标准化的屏幕控制命令（sleep in/out、display on/off、写参数等）

**常见 DCS 命令**：
| 命令 | 代码 | 作用 |
|------|------|------|
| Sleep Out | 0x11 | 退出睡眠 |
| Display On | 0x29 | 开启显示 |
| Sleep In | 0x10 | 进入睡眠 |
| Display Off | 0x28 | 关闭显示 |
| Set Column Address | 0x2A | 设置列范围 |
| Set Page Address | 0x2B | 设置行范围 |
| Memory Write | 0x2C | 开始写像素 |

**下午 — 在代码中追踪 DSI 命令发送流程**

从 panel 驱动出发，追踪一条 DCS 命令是如何从软件到达屏幕的：

```
panel_driver.init_sequence
  → mipi_dsi_dcs_write()           // include/drm/drm_mipi_dsi.h
    → mipi_dsi_device_transfer()   // drivers/gpu/drm/drm_mipi_dsi.c
      → host->ops->transfer()      // DSI host 回调
        → dw_mipi_dsi_host_transfer()  // dw-mipi-dsi.c
          → 写 DSI 控制器寄存器，通过 LP 模式发到屏幕
```

**今日产出**：在笔记里记录这条调用链，标注每一层做了什么。

---

### Day 8（周三）：写一个 MIPI Panel 驱动（模拟练习）

**上午 — Panel 驱动骨架**

写一个最小的 MIPI DSI panel 驱动，参考现有的 `panel-ilitek-ili9881c.c`：

核心要素：
1. **probe**：获取电源、GPIO、背光等资源
2. **prepare**：上电序列（开电源 → 延时 → 解除复位 → 延时）
3. **enable**：发送初始化命令序列 → Sleep Out → Display On
4. **disable**：Display Off → Sleep In
5. **unprepare**：断电序列（拉低复位 → 关电源）
6. **get_modes**：返回分辨率和时序信息

```c
/* 伪代码骨架 */
static int xxx_panel_prepare(struct drm_panel *panel)
{
    // 1. 使能电源 regulator_enable()
    // 2. 延时（规格书要求）
    // 3. 拉高 reset GPIO
    // 4. 延时
    // 5. 拉低 reset（产生复位脉冲）
    // 6. 延时
    // 7. 拉高 reset
    // 8. 延时（等待屏内部初始化完成）
    return 0;
}

static int xxx_panel_enable(struct drm_panel *panel)
{
    // 1. 发送厂商自定义初始化序列（从规格书的 Initial Code 章节抄来）
    // 2. mipi_dsi_dcs_exit_sleep_mode()    → 0x11
    // 3. msleep(120)                        → 规格书要求 sleep out 后等 120ms
    // 4. mipi_dsi_dcs_set_display_on()     → 0x29
    // 5. 使能背光 backlight_enable()
    return 0;
}
```

**下午 — 初始化序列的处理**

屏幕规格书会给出一长串初始化命令（Initial Code），格式类似：

```
{0xFF, 0x98, 0x81, 0x03},   // 切换到 CMD Page 3
{0x01, 0x00},                // GIP Setting
{0x02, 0x00},
...
{0xFF, 0x98, 0x81, 0x00},   // 切回 CMD Page 0
{0x11, 0x00},                // Sleep Out
{DELAY, 120},                // 延时 120ms
{0x29, 0x00},                // Display On
```

练习把这样的初始化序列翻译成内核驱动代码（用 `mipi_dsi_generic_write` 或 `mipi_dsi_dcs_write_buffer`）。

**今日产出**：写出一个完整的（但不需要编译通过的）panel 驱动框架代码，存到笔记目录。

---

### Day 9（周四）：电源与背光子系统

**上午 — 屏幕电源管理**

一块屏通常需要以下电源（以 MIPI 屏为例）：
- **VDD (IOVCC)**：IO 电源，通常 1.8V
- **VCC (AVDD)**：模拟/驱动电源，通常 2.8V-3.3V
- **VSP/VSN**：部分 OLED 屏需要正负偏置电压

在设备树中通过 regulator 管理：

```dts
vcc3v3_lcd: vcc3v3-lcd {
    compatible = "regulator-fixed";
    regulator-name = "vcc3v3_lcd";
    regulator-min-microvolt = <3300000>;
    regulator-max-microvolt = <3300000>;
    gpio = <&gpio0 RK_PC7 GPIO_ACTIVE_HIGH>;
    enable-active-high;
};
```

**关键**：上电顺序和延时必须严格遵循规格书，否则可能：
- 屏幕无显示（最常见）
- 白屏/花屏
- 永久损坏屏幕 IC（极端情况）

**下午 — 背光子系统**

Linux 背光子系统：
```
PWM 硬件 → pwm-backlight 驱动 → backlight class → panel 驱动引用
```

设备树配置关键参数：
```dts
backlight: backlight {
    compatible = "pwm-backlight";
    pwms = <&pwm5 0 25000 0>;   // 使用 pwm5, 周期 25000ns = 40kHz
    brightness-levels = <        // 亮度等级表
        0 4 8 16 32 64 128 255>;
    default-brightness-level = <6>;
};
```

**常见背光问题排查**：
1. 完全不亮 → 检查 PWM 引脚配置（pinctrl）、GPIO enable 是否正确
2. 亮度不可调 → PWM 极性是否正确、频率是否匹配
3. 闪烁 → PWM 频率太低（建议 > 20kHz）

**今日产出**：画出一张「电源 + 背光 + 复位」的时序图模板，标注每个延时从哪里查。

---

### Day 10（周五）：完整 Bringup 流程走一遍（纸上推演）

**上午 — Bringup 标准流程 Checklist**

完整的 MIPI DSI 屏 bringup 步骤：

```
Step 1: 硬件确认
    □ 确认屏幕接口类型（MIPI DSI / LVDS / eDP）
    □ 确认 FPC 连接正确（正反、引脚对应）
    □ 确认电源电路（VCC、IOVCC 电压是否正确）
    □ 确认 reset/enable GPIO 连接
    □ 确认背光电路（PWM 引脚、背光驱动芯片）
    □ 用万用表量供电是否正常

Step 2: 获取屏幕参数
    □ 从规格书提取：分辨率、lane 数、时序参数
    □ 从规格书提取：上电时序和延时
    □ 从规格书或屏厂获取：初始化代码 (init code)
    □ 从规格书提取：DSI clock range

Step 3: 设备树配置
    □ 配置 panel 节点（compatible、电源、GPIO、时序）
    □ 配置 DSI 节点（status=okay、lane-rate）
    □ 配置 VOP port 连接（VP → DSI → Panel）
    □ 配置 PWM backlight
    □ 配置 pinctrl（PWM引脚、DSI引脚如果需要）
    □ 配置 regulator

Step 4: 驱动代码
    □ 判断是否需要自定义 panel 驱动（有初始化命令 → 需要）
    □ 如需自定义：编写 panel 驱动，填入初始化序列
    □ 如不需要：使用 panel-simple 或 simple-panel-dsi
    □ 编译内核，确保相关驱动被编入

Step 5: 烧写验证
    □ 编译设备树和内核
    □ 烧写到板子
    □ 观察 dmesg 日志
    □ 检查屏幕是否亮起

Step 6: 问题排查（如果不亮）
    □ 检查 dmesg 有无报错
    □ 检查 connector status
    □ 检查 VOP/DSI 是否正常初始化
    □ 用示波器/逻辑分析仪检查信号
    □ 缩小问题范围（电源？时序？命令？）
```

**下午 — 复盘第二周 + 知识盲区清理**

1. 回顾本周所有笔记
2. 列出仍然不理解的概念，逐个搜索/请教解决
3. 准备下周的实操环境

---

## 第三周：实战 —— 动手 Bringup + 问题排查

> **本周目标**：在真实硬件上完成至少一块屏的 bringup，掌握常见问题的排查方法。

### Day 11（周一）：LVDS 屏实操（最简单的起步）

> 选择 LVDS 屏开始，因为它不需要初始化命令，bringup 最简单。

**全天任务**：

1. **硬件连接**：接上 LVDS 屏，确认 FPC 连接、供电
2. **设备树修改**：
   - 修改 `topeet_screen_choose.dtsi`，启用 LVDS 配置
   - 确认时序参数与你的屏规格书匹配
   - 确认 panel 的 compatible、电源、背光配置
3. **编译烧写**：编译设备树 → 烧写 → 重启
4. **验证结果**：
   - 观察屏幕是否亮起
   - 检查 `dmesg | grep -i "drm\|vop\|lvds\|panel"` 
   - 检查 `cat /sys/kernel/debug/dri/0/summary`

**如果成功**：恭喜！这是你的第一块屏。测试触摸、背光调节等。
**如果失败**：进入排查流程（详见 Day 14 的排查方法论）。

---

### Day 12（周二）：MIPI DSI 屏实操

**全天任务**：

1. **硬件连接**：接上 MIPI DSI 屏
2. **确认/编写 panel 驱动**：
   - 如果屏的 IC 已有现成驱动 → 直接用
   - 如果没有 → 参考 Day 8 的骨架，编写新的 panel 驱动
   - 填入规格书的初始化序列
3. **设备树修改**：
   - 启用 MIPI DSI 配置
   - 配置 lane 数、lane-rate
   - 配置 panel 节点
4. **编译烧写验证**

**MIPI 屏特有的注意事项**：
- lane-rate 设置不合适可能导致花屏或无显示
- 初始化命令顺序错误可能导致白屏
- reset 时序不对可能导致屏幕无响应

---

### Day 13（周三）：HDMI / eDP 实操 + 多屏配置

**上午 — HDMI 输出**

HDMI 相对简单（即插即用），但理解其工作方式很重要：
1. 修改设备树启用 HDMI
2. 连接 HDMI 显示器
3. 观察 EDID 读取过程（`dmesg | grep edid`）
4. 理解热插拔检测（HPD）机制

**下午 — 多屏同显/异显**

RK3568 支持三路显示输出（VP0 + VP1 + VP2），尝试：
1. HDMI + LVDS 双屏
2. 设备树中如何配置 VP 与 encoder 的绑定关系
3. 测试双屏异显（不同分辨率、不同内容）

---

### Day 14（周四）：问题排查方法论（最重要的一天）

**上午 — 系统化排查框架**

当屏幕不亮时，按以下顺序排查（**由硬到软**）：

```
第一层：供电检查
├── 屏幕电源是否正常（万用表量 VCC、IOVCC）
├── 背光是否亮（黑暗中贴近看，有微光说明有信号但无背光）
└── 如果供电异常 → 检查 regulator 配置、GPIO enable

第二层：信号检查
├── DSI/LVDS 信号是否有输出（示波器看 CLK lane）
├── Reset 信号是否正确（示波器看复位脉冲）
└── 如果无信号 → 检查 VOP 和 encoder 是否正确初始化

第三层：软件检查
├── dmesg 有无报错？
│   ├── "failed to get mode"     → 时序参数问题
│   ├── "panel not found"        → compatible 不匹配
│   ├── "failed to enable"       → 电源或 GPIO 问题
│   ├── "timeout"                → 通信超时，检查连接
│   └── "dsi transfer failed"    → DSI 命令发送失败
├── debugfs 状态？
│   ├── cat /sys/kernel/debug/dri/0/summary
│   └── connector 状态是 connected 还是 disconnected？
└── 内核配置对不对？
    ├── panel 驱动是否编译进去了？
    ├── DSI host 驱动是否使能？
    └── VOP2 驱动是否使能？

第四层：协议层分析
├── MIPI：用 DSI 分析仪抓包（如果有的话）
├── 简易方法：在驱动中加打印，确认初始化命令是否都发成功了
└── 确认 Video Mode 参数是否正确写入 DSI 寄存器
```

**下午 — 实际案例练习**

故意制造以下故障，然后练习排查（这是最好的学习方式）：

1. **故意改错时序参数**（如 hback-porch 改成 0）→ 观察现象，练习修复
2. **故意关闭 panel 电源**（status="disabled"）→ 观察 dmesg 报错
3. **故意改错 reset GPIO 极性** → 观察现象
4. **故意改错 lane 数** → 观察现象
5. **故意删掉初始化命令的一部分** → 观察现象

每个故障都记录：**现象 → dmesg 日志 → 排查过程 → 根因 → 修复方法**。

**今日产出**：建立自己的「故障现象 → 根因」速查表。

---

### Day 15（周五）：常见显示异常分析

**全天 — 不是不亮，而是显示异常的排查**

| 现象 | 可能原因 | 排查方向 |
|------|----------|----------|
| 白屏 | 初始化命令未发送/失败 | 检查 panel enable 流程 |
| 花屏 | 时序参数错误或 lane-rate 不匹配 | 调整时序/降低 lane-rate |
| 颜色偏移 | RGB 顺序错误、色深不匹配 | 检查 bus-format/color-format |
| 闪烁 | DCLK 不稳定或 blanking 参数太小 | 调整时序参数 |
| 显示偏移 | HBP/VBP 参数不准 | 微调 porch 参数 |
| 撕裂 | 未启用 TE 同步（Command Mode）| 检查 TE 信号配置 |
| 横竖颠倒 | Panel 驱动未设置旋转 | 设置 rotation 属性 |
| 倒置/镜像 | 初始化命令中的扫描方向设置 | 检查 0x36 (MADCTL) 命令 |
| 只有背光，无画面 | DSI 通信失败或 VOP 未输出 | 检查 debugfs + dmesg |
| 开机闪一下后灭 | enable/disable 时序问题 | 检查 panel 状态机 |

---

## 第四周：提升 —— 进阶场景 + 能力固化

> **本周目标**：处理进阶场景，形成自己的知识体系，达到可独立工作的水平。

### Day 16（周一）：触摸屏集成

**上午 — 触摸屏子系统**

触摸屏和显示屏是独立的子系统，但 bringup 时通常一起做：

```
触摸 IC → I2C/SPI 总线 → input 子系统 → 用户空间
```

常见触摸 IC：Goodix (GT911/GT9271)、FocalTech (FT5x06)、Himax 等。

设备树配置要点：
- I2C 地址（不同 IC 不同，常见 0x14、0x5D、0x38）
- 中断 GPIO（触摸事件上报）
- 复位 GPIO
- 分辨率匹配（触摸坐标范围必须与屏幕分辨率匹配）

**下午 — 触摸调试**

```bash
# 检查 I2C 设备是否识别
i2cdetect -y 1

# 查看 input 设备
cat /proc/bus/input/devices

# 实时查看触摸事件
evtest /dev/input/eventX

# 触摸校准（如果坐标不准）
ts_calibrate  # 需要 tslib
```

---

### Day 17（周二）：开机 Logo 与显示启动优化

**上午 — U-Boot 阶段的显示**

完整的显示启动链路：
```
U-Boot 显示 Logo → Kernel 接管 → DRM 初始化 → 用户空间 GUI
```

U-Boot 阶段点屏的意义：开机即亮，提升用户体验。

了解 Rockchip U-Boot 的显示框架：
- U-Boot 中的 DRM 驱动（简化版）
- 开机 Logo 的替换方法
- 无缝切换（U-Boot → Kernel 显示不黑屏）

**下午 — Kernel 启动阶段优化**

- `fbcon`（framebuffer console）的启用与配置
- 开机 splash screen 的实现方式
- `plymouth` 等用户空间方案

---

### Day 18（周三）：屏幕参数微调与画质优化

**全天 — 显示效果调优**

1. **Gamma 校正**：调整显示曲线，让灰阶过渡更自然
2. **色温调整**：通过初始化命令中的 gamma 参数或用户空间 LUT
3. **亮度曲线**：PWM backlight 的亮度 levels 调整（人眼感知非线性）
4. **Overscan/Underscan**：确保画面充满屏幕无黑边
5. **刷新率调整**：如何在 60Hz/90Hz/120Hz 之间切换（如果屏支持）

---

### Day 19（周四）：低功耗与特殊场景

**上午 — 显示相关的电源管理**

- **Runtime PM**：屏幕灭屏时关闭 DSI PHY、VOP 时钟
- **Suspend/Resume**：系统休眠时的显示关闭与恢复序列
- **PSR (Panel Self Refresh)**：静态画面时让 panel 自刷新
- 功耗测量与优化

**下午 — 特殊场景处理**

- **旋转显示**：横屏设备竖屏显示（或反之）
- **热插拔**：HDMI 热插拔事件处理
- **分辨率动态切换**
- **双屏克隆模式 vs 扩展模式**

---

### Day 20（周五）：总结与能力评估

**上午 — 知识体系整理**

建立你自己的 Bringup 手册，包含：
1. **Checklist**：每次 bringup 的标准步骤（Day 10 的升级版）
2. **故障速查表**：现象 → 根因 → 解决方案（Day 14/15 的整理）
3. **设备树模板**：MIPI DSI / LVDS / eDP 各一份
4. **Panel 驱动模板**：可复用的驱动骨架代码
5. **调试命令速查**：所有常用调试命令汇总

**下午 — 自我评估 Checklist**

完成以下自测，每一项都能做到就说明你已经具备独立 bringup 的能力了：

- [ ] 能看懂一份新屏的规格书，并提取所有 bringup 所需参数
- [ ] 能独立编写/修改设备树配置点亮一块 MIPI DSI 屏
- [ ] 能独立编写一个 MIPI panel 驱动（含初始化序列）
- [ ] 能配置 LVDS 屏的设备树并点亮
- [ ] 能排查「屏幕不亮」的问题，并缩小到具体原因
- [ ] 能识别花屏、偏色、闪烁等异常并给出调整方向
- [ ] 能配置触摸屏并完成基本调试
- [ ] 理解 DRM/KMS 框架的核心对象和数据流
- [ ] 能在 debugfs 和 dmesg 中找到有效的调试信息
- [ ] 了解双屏配置的方法

---

## 持续学习资源

### 必读代码（按优先级）
1. `kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c` — VOP2 核心
2. `kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c` — MIPI DSI Host
3. `kernel/drivers/gpu/drm/panel/panel-simple.c` — 通用 Panel
4. `kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c` — 主驱动框架
5. `kernel/drivers/gpu/drm/drm_atomic_helper.c` — Atomic commit 核心流程

### 推荐阅读
- Rockchip 官方文档：`docs/Common/DISPLAY/` 目录下的开发指南
- DRM 内核文档：`Documentation/gpu/drm-kms.rst`
- LWN.net 上的 DRM/KMS 系列文章
- 已有笔记 `my-notes/drm/introduction-chinese.md` 和 `drm-kms-chinese.md`
- 已有笔记 `my-notes/drm/基础知识.md`（PLL 原理）

### 社区与求助
- Rockchip 开发者论坛 / 开发者群
- DRM mailing list（kernel 上游社区）
- Stack Overflow + "rockchip drm" / "mipi dsi linux"

---

## 每日时间分配建议

| 时段 | 活动 | 占比 |
|------|------|------|
| 9:00-10:30 | 理论学习/代码阅读 | 20% |
| 10:30-12:00 | 动手实操/写代码 | 20% |
| 14:00-16:00 | 核心实操/调试 | 25% |
| 16:00-17:30 | 问题排查/实验 | 20% |
| 17:30-18:00 | 当日笔记整理 | 15% |

> **核心原则**：理论够用就好，尽快上手实操。遇到问题再回头补理论，这样印象最深。

---

*计划制定日期：2026-03-10*
*预计完成日期：2026-04-10*
