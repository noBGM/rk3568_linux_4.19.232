# 屏幕 Bringup 一个月逆袭计划

> 背景：有 Linux 驱动开发基础（内核基础子系统 + 驱动框架），无屏幕/显示相关经验。
> 岗位：公司媒体组（相机屏幕、遥控器屏幕），**公司自研 SoC**。
> 目标：一个月后能理解完整的屏幕 bringup 流程（FPGA pre-silicon → U-Boot → Linux），
> 并能独立完成一块新屏幕（MIPI DSI / DP / eDP）在 Linux 平台上的 bringup。
>
> ⚠️ **自研 SoC 意味着什么？**
> - 你可能是**第一个**为这颗芯片的显示模块写驱动的人，没有开源社区代码可抄
> - 你的主要参考资料是 IC 团队提供的**寄存器手册**，而不是现成的 vendor driver
> - 你需要参与 **FPGA pre-silicon** 阶段的验证工作
> - 你需要同时搞定 **U-Boot 显示** + **Linux DRM 驱动**
> - 你需要和 IC 设计团队紧密协作，甚至帮他们发现硬件 Bug

---

## 整体路线图

```
第1周: 硬件基础 + 显示原理 + 接口协议（MIPI DSI / DP / eDP / LVDS）
第2周: Linux DRM/KMS 框架 + 寄存器手册阅读能力（自研 SoC 核心技能）
第3周: Bringup 实战流程（Panel 驱动 + U-Boot 显示 + 调试工具链）
第4周: FPGA Pre-silicon 验证 + 高级调试 + 自研 SoC 协作实战
```

---

## 第 1 周：硬件基础与显示原理（地基）

### Day 1-2：LCD 屏幕硬件基础

**学习目标：** 搞懂一块屏幕从物理层面到底是怎么工作的。

**学习内容：**

1. **LCD 显示原理**
   - TFT-LCD 的物理结构：偏光片、液晶层、彩色滤光片、背光模组
   - 像素的 RGB 子像素排列（Strip / Delta / PenTile）
   - 背光（Backlight）与亮度调节（PWM 调光）
   - OLED 与 LCD 的区别（自发光 vs 背光，公司产品两者都可能用到）

2. **屏幕关键参数**
   - 分辨率（Resolution）：如 720x1280, 1080x1920
   - 色深（Color Depth）：RGB888 / RGB666 / RGB565
   - 刷新率（Refresh Rate）：60Hz / 120Hz
   - 像素时钟（Pixel Clock）：= H_total × V_total × refresh_rate
   - 接口类型：MIPI DSI / LVDS / eDP / RGB(DPI) / SPI

3. **Timing 参数（最最最重要！）**
   - 必须搞懂的概念：
     - **HActive / VActive**：有效显示区域（即分辨率）
     - **HFP (Horizontal Front Porch)**：行同步前肩
     - **HBP (Horizontal Back Porch)**：行同步后肩
     - **HSW/HSYNC (Horizontal Sync Width)**：行同步脉宽
     - **VFP (Vertical Front Porch)**：场同步前肩
     - **VBP (Vertical Back Porch)**：场同步后肩
     - **VSW/VSYNC (Vertical Sync Width)**：场同步脉宽
   - **画一张 Timing 图！手画！** 把 HFP/HBP/HSW/VFP/VBP/VSW 的位置关系画清楚
   - H_total = HActive + HFP + HSW + HBP
   - V_total = VActive + VFP + VSW + VBP
   - Pixel Clock = H_total × V_total × FPS

**实践任务：**
- 找一份真实的屏幕 Datasheet（比如 ILI9881C 或 ST7701S），通读 Timing 章节
- 手动计算 Pixel Clock，与 Datasheet 标注值对比验证
- 把 Timing 图手绘到笔记本上（这个图你以后天天要用）

**推荐资源：**
- 搜索 "LCD Timing Diagram" 图解
- 搜索 "TFT-LCD 工作原理详解"
- 任意一款屏幕 IC 的 Datasheet（Timing Parameters 章节）

---

### Day 3-4：MIPI DSI 协议（重中之重）

**学习目标：** 搞懂 MIPI DSI 协议的分层、数据传输方式、初始化命令格式。

> 公司的相机屏幕、遥控器屏幕大概率使用 MIPI DSI 接口（小尺寸屏的主流方案）。

**学习内容：**

1. **MIPI DSI 物理层**
   - 差分信号传输：Data Lane（数据通道）+ Clock Lane（时钟通道）
   - Lane 数量：1/2/4 Lane，Lane 数越多带宽越大
   - LP (Low Power) 模式 vs HS (High Speed) 模式
   - 典型速率：HS 模式下单 Lane 可达 1~2.5 Gbps

2. **MIPI DSI 协议层**
   - **Video Mode vs Command Mode**
     - Video Mode：主控持续推送像素数据，屏幕无 GRAM（大部分屏幕用这个）
       - 三种子模式：Burst Mode / Non-Burst Sync Pulse / Non-Burst Sync Event
     - Command Mode：屏幕自带 GRAM，主控按需写入（OLED 屏常见）
   - **DCS (Display Command Set) 命令**
     - 短包命令（Short Packet）：如 `0x11`（Sleep Out）、`0x29`（Display On）
     - 长包命令（Long Packet）：如初始化序列中的寄存器配置
     - 通用 DCS 命令表（MIPI DCS 标准定义）

3. **MIPI DSI 初始化序列**
   - 屏幕上电 → 发送 init code → sleep out → display on
   - Init code 来源：屏厂提供（通常是一份 Excel 或 C 数组）
   - 数据格式举例：`{cmd, delay_ms, len, data[0], data[1], ...}`

4. **MIPI DSI Lane 速率计算**
   ```
   所需带宽 = width × height × bpp × fps
   单 Lane 带宽 = hs_clk × 2 (DDR)
   所需 Lane 数 = 所需带宽 / 单 Lane 带宽
   ```

**实践任务：**
- 阅读一份屏幕 Datasheet 的 MIPI DSI 章节，理解其 init code
- 手动计算一块 720x1280 RGB888 60fps 屏幕在 4-Lane 下需要的最低 HS Clock
- 梳理 Video Mode 三种子模式的区别，画出对比表

**推荐资源：**
- MIPI Alliance 官方 DSI 规范（可搜索非官方解读版本）
- 搜索 "MIPI DSI 协议详解" / "MIPI DSI tutorial"
- 搜索 "MIPI DCS command set"

---

### Day 5：DP/eDP、LVDS 与其他接口

**学习目标：** 搞懂 MIPI DSI 之外的主流显示接口，尤其是 DP/eDP。

**学习内容：**

1. **DisplayPort (DP) / eDP 协议（重要！）**

   > 公司遥控器大屏幕、外接监视器等场景可能使用 eDP 或 DP 接口。

   - **DP 与 eDP 的关系**
     - DP (DisplayPort)：外部接口标准，用于连接外置显示器
     - eDP (embedded DisplayPort)：DP 的内嵌版本，用于设备内部连接屏幕（笔记本、遥控器大屏等）
     - 两者协议层基本相同，eDP 增加了 PSR（Panel Self Refresh）等省电特性

   - **DP 物理层**
     - 差分信号传输，Main Link 有 1/2/4 Lane
     - 每 Lane 支持多种速率：
       - RBR: 1.62 Gbps/Lane
       - HBR: 2.7 Gbps/Lane
       - HBR2: 5.4 Gbps/Lane
       - HBR3: 8.1 Gbps/Lane
     - AUX Channel：辅助通道（1 Mbps），用于链路训练、DPCD 读写、EDID 读取

   - **DP 链路训练（Link Training）**
     - DP 与 DSI 最大的区别之一：DP 需要**链路训练**
     - 分两个阶段：
       1. Clock Recovery (CR)：调整电压摆幅(Vswing)和预加重(Pre-emphasis)，直到接收端锁定时钟
       2. Channel Equalization (EQ)：均衡各 Lane 的信号质量
     - 训练结果存储在 Sink 端的 DPCD (DisplayPort Configuration Data) 寄存器中
     - 如果链路训练失败 → 屏幕不亮，这是 DP bringup 最常见的坑

   - **DPCD 与 EDID**
     - DPCD：Sink 设备的能力描述（支持的最大 Lane 数、最大速率等），通过 AUX 读取
     - EDID：显示器的物理信息（分辨率、Timing、厂商信息），通过 AUX/I2C-over-AUX 读取
     - eDP 的屏幕也有 EDID，但有些 eDP 屏可能不带 EDID，需要在驱动中硬编码 Timing

   - **DP 与 MIPI DSI 的关键差异对比**

     | 特性 | MIPI DSI | DP/eDP |
     |------|---------|--------|
     | 初始化命令 | 需要发送 init code | 不需要（靠链路训练） |
     | 链路训练 | 不需要 | 必须 |
     | 热插拔 | 通常不支持 | DP 支持（HPD 信号），eDP 通常固定连接 |
     | EDID | 无 | 有（通过 AUX 读取） |
     | 典型应用 | 手机/小屏 | 显示器/笔记本/大屏 |
     | 带宽 | ~2.5 Gbps/Lane | 最高 8.1 Gbps/Lane |

   - **Linux 中的 DP/eDP 驱动架构**
     - DP 控制器驱动（如 `analogix_dp`, `cdn-dp`, 各平台自己的 DP 控制器）
     - `drm_dp_helper.c`：内核提供的 DP 辅助函数库（链路训练、DPCD 读写、EDID 解析等）
     - eDP panel 通常用 `panel-simple.c` 或 `panel-edp.c`（内核已收录大量 eDP 屏的 Timing）

2. **LVDS (Low-Voltage Differential Signaling)**
   - 与 MIPI DSI 的区别：LVDS 更老、更简单，常用于大尺寸工业屏
   - 单路/双路 LVDS（Single Link / Dual Link）
   - JEIDA / VESA 两种数据映射格式
   - 不需要发送初始化命令（纯视频流传输）

3. **其他接口快速了解**
   - RGB/DPI：并行接口，引脚多但简单直接
   - SPI：极低分辨率小屏（如 240x320 的 MCU 屏）

**实践任务：**
- 制作一张接口对比表：MIPI DSI / DP / eDP / LVDS / RGB / SPI 的特点、适用场景、带宽对比
- 搜索阅读一篇 "DisplayPort Link Training" 的图文教程，理解 CR 和 EQ 两个阶段
- 阅读内核 `include/drm/drm_dp_helper.h` 中的 DPCD 地址定义，感受 DP 协议的丰富程度

---

### Day 6-7：电源时序与背光

**学习内容：**

1. **屏幕电源管理**
   - 典型供电：VCC（逻辑供电）、IOVCC（IO 供电）、AVDD/AVEE（模拟正负压）
   - **上电时序（Power-on Sequence）**—— 必须严格按 Datasheet 要求！
     - 举例：VCC → 延时 → IOVCC → 延时 → Reset 拉高 → 延时 → 发送 Init Code
   - **下电时序（Power-off Sequence）**—— 通常是上电的逆序
   - Reset 信号：低电平复位，高电平工作

2. **背光控制**
   - PWM 调光 vs DC 调光
   - 背光 IC（如 SGM37603, LM3630A）
   - Linux 中的 Backlight 子系统（`/sys/class/backlight/`）

3. **电路原理图阅读**
   - 学会在原理图中找到：屏幕连接器、电源芯片、背光电路、Reset/TE 引脚
   - TE (Tearing Effect) 信号：用于防止画面撕裂（Command Mode 下常用）

**实践任务：**
- 找一份带屏幕的开发板原理图，找到屏幕供电电路和背光电路
- 画出上电时序图（标注每一步的延时要求）

---

## 第 2 周：Linux DRM/KMS 框架（灵魂）

### Day 8-9：DRM/KMS 框架总览

**学习目标：** 理解 DRM 子系统的整体架构和核心对象。

**学习内容：**

1. **为什么需要 DRM/KMS？**
   - 历史：Framebuffer（fbdev）→ DRM/KMS
   - KMS = Kernel Mode Setting：内核负责设置显示模式（分辨率、刷新率）
   - DRM = Direct Rendering Manager：统一管理 GPU 渲染和显示输出

2. **DRM 核心对象模型（必须烂熟于心！）**
   ```
   Framebuffer → Plane → CRTC → Encoder → Connector → Panel/Bridge
   ```
   - **Framebuffer**：一块包含像素数据的内存区域（GEM/dumb buffer）
   - **Plane**：图层，叠加在 CRTC 上（Primary / Overlay / Cursor）
   - **CRTC**：显示控制器，负责扫描输出（Timing Generator）
   - **Encoder**：将 CRTC 输出的数字信号编码为特定接口信号（MIPI/LVDS/HDMI...）
   - **Connector**：物理连接点（对应一个屏幕/显示器）
   - **Panel**：drm_panel 抽象，代表一块具体的屏幕（有初始化序列、Timing 等）
   - **Bridge**：信号转换器（如 DSI-to-LVDS 桥接芯片）

3. **对象间关系与数据流**
   ```
   用户空间 App
       ↓ (ioctl / libdrm / modetest)
   DRM Core (drm_device)
       ↓
   Framebuffer (像素数据)
       ↓
   Plane (选择图层)
       ↓
   CRTC (生成 Timing 信号，扫描 Framebuffer)
       ↓
   Encoder (编码为 DSI/LVDS/HDMI 信号)
       ↓
   Connector (物理输出口)
       ↓
   Panel / Bridge / Monitor (最终显示设备)
   ```

4. **Atomic Modesetting**
   - 新的 API，替代传统的 legacy modesetting
   - 原子性：要么全部成功，要么全部失败（避免中间状态导致花屏）
   - Property 机制：所有配置以 property 形式暴露

**实践任务：**
- 画出 DRM 对象关系图（手画或工具画，必须自己画一遍）
- 阅读内核文档：`Documentation/gpu/drm-kms.rst`
- 用自己的话写出每个对象的一句话定义

**推荐资源：**
- 内核文档 `Documentation/gpu/` 目录
- 搜索 "Linux DRM KMS 详解" / "DRM internals"
- Boris Brezillon 的 DRM/KMS 演讲（YouTube/B站搜索）
- 搜索 "The DRM/KMS subsystem from a newbie's point of view"

---

### Day 10-11：DRM Panel 驱动深入

**学习目标：** 能看懂并写一个 Panel 驱动。

> 这是你日常工作中最直接接触的代码！每块新屏幕都要写/改 panel 驱动。

**学习内容：**

1. **Panel 驱动的位置与结构**
   - 内核路径：`drivers/gpu/drm/panel/`
   - 一个 panel 驱动的核心组成：
     - `probe()`：获取资源（GPIO、Regulator、供电）、注册 `drm_panel`
     - `drm_panel_funcs`：
       - `.prepare()`：上电 + 发送初始化命令
       - `.enable()`：开背光
       - `.disable()`：关背光
       - `.unprepare()`：下电
       - `.get_modes()`：报告屏幕支持的模式（分辨率 + Timing）

2. **一个典型 MIPI DSI Panel 驱动的代码骨架**
   ```c
   struct my_panel {
       struct drm_panel panel;
       struct mipi_dsi_device *dsi;
       struct gpio_desc *reset_gpio;
       struct regulator *supply;
       // ...
   };

   static int my_panel_prepare(struct drm_panel *panel) {
       // 1. regulator_enable(supply)
       // 2. 延时
       // 3. reset_gpio 拉高→拉低→拉高（复位序列）
       // 4. 延时
       // 5. mipi_dsi_dcs_write_seq(...) 发送初始化命令
       // 6. mipi_dsi_dcs_exit_sleep_mode()
       // 7. 延时（通常 120ms）
       // 8. mipi_dsi_dcs_set_display_on()
       return 0;
   }

   static int my_panel_get_modes(struct drm_panel *panel,
                                  struct drm_connector *connector) {
       // 创建 drm_display_mode，填入 Timing 参数
       // hdisplay, vdisplay, hsync_start, hsync_end, htotal
       // vsync_start, vsync_end, vtotal, clock
       // 设置 type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED
   }
   ```

3. **MIPI DSI 命令发送 API**
   - `mipi_dsi_dcs_write_seq(dsi, cmd, data...)`
   - `mipi_dsi_dcs_exit_sleep_mode(dsi)`
   - `mipi_dsi_dcs_set_display_on(dsi)`
   - `mipi_dsi_generic_write(dsi, data, len)`

4. **阅读参考驱动**
   - `panel-simple.c`：最简单的 panel 驱动（适合 LVDS/eDP）
   - `panel-ilitek-ili9881c.c`：典型 MIPI DSI panel（有初始化序列）
   - `panel-sitronix-st7701.c`：另一个常见的 MIPI DSI panel

**实践任务：**
- 精读 `panel-ilitek-ili9881c.c`（或类似驱动），逐函数理解
- 尝试写一个 Panel 驱动的骨架代码（不需要能编译，理解结构即可）
- 总结 `prepare/enable/disable/unprepare` 的调用时机和区别

---

### Day 12-13：DSI Host / DP Controller / CRTC 驱动 + 寄存器手册阅读（自研 SoC 核心技能）

**学习目标：** 理解显示控制器驱动的工作方式；掌握从寄存器手册编写驱动的能力。

> **自研 SoC 的关键区别：** 在 Rockchip/Qualcomm 等公开平台上做 bringup，你有现成的
> vendor driver 可以参考甚至直接用。但在公司自研 SoC 上，**你拿到的是一份寄存器手册
> (Register Specification)**，需要自己从零把驱动写出来。这是你最核心的技能。

**学习内容：**

1. **如何阅读寄存器手册（Register Specification）**

   > 这是自研 SoC 驱动工程师吃饭的本事，必须练到熟练。

   - **寄存器手册的典型结构：**
     - Module Overview（模块功能概述）
     - Register Map / Register Summary（寄存器地址映射表）
     - Register Description（每个寄存器的详细描述）
       - Offset（偏移地址）
       - Reset Value（复位默认值）
       - Bit Field 描述（每个位域的名称、位宽、读写属性、含义）
     - Timing Diagram（硬件时序图）
     - Programming Guide（编程指南，如果有的话——很多时候没有）

   - **阅读技巧：**
     - 先看 Module Overview，理解这个硬件模块的整体功能
     - 重点关注 **Programming Guide**（如果有），它告诉你初始化步骤
     - 不要试图一次读完所有寄存器——先找到你需要配置的那几个
     - 标记 R/W 属性：R（只读）、W（只写）、RW（读写）、W1C（写1清零）
     - 注意 Reserved 位域：不要随便写，保持默认值

   - **举例：一个典型的显示控制器寄存器**
     ```
     Register: DISP_CTRL (Offset: 0x0000)
     Reset Value: 0x0000_0000

     Bits  | Name        | R/W | Description
     ------|-------------|-----|---------------------------
     [31]  | ENABLE      | RW  | 1: Enable display output
     [30]  | STANDBY     | RW  | 1: Standby mode
     [29:28]| OUT_FMT    | RW  | 00: RGB888, 01: RGB666...
     [27:24]| Reserved   | RO  | Must keep default
     [23:16]| LAYER_NUM  | RO  | Number of hardware layers
     [15:0] | VERSION    | RO  | IP version number
     ```
   - 对应的驱动代码写法：
     ```c
     #define DISP_CTRL           0x0000
     #define  DISP_CTRL_ENABLE   BIT(31)
     #define  DISP_CTRL_STANDBY  BIT(30)
     #define  DISP_CTRL_OUT_FMT_MASK  GENMASK(29, 28)
     #define  DISP_CTRL_OUT_FMT(x)    FIELD_PREP(DISP_CTRL_OUT_FMT_MASK, x)

     /* 启动显示输出 */
     writel(DISP_CTRL_ENABLE | DISP_CTRL_OUT_FMT(0),
            base + DISP_CTRL);
     ```

2. **显示控制器 (CRTC) 驱动——自研 SoC 视角**

   - **你的 SoC 的显示控制器可能叫什么名字：**
     - 公司内部代号（需要入职后了解）
     - 功能上等同于 Rockchip 的 VOP、高通的 MDP/DPU、海思的 VDP
     - 在 DRM 框架中注册为 `drm_crtc`

   - **显示控制器核心寄存器组（通常会有的）：**
     - **Timing 寄存器**：HACTIVE, HTOTAL, HSYNC_START, HSYNC_END, VACTIVE, VTOTAL...
     - **Plane/Layer 寄存器**：Framebuffer 地址、格式、大小、位置、Alpha
     - **输出接口选择寄存器**：选择 DSI / DP / HDMI 输出
     - **中断寄存器**：VBlank 中断、Underrun 中断等
     - **时钟寄存器**：Pixel Clock 分频配置
     - **全局控制寄存器**：使能、复位、DMA 启动

   - **从寄存器手册到驱动代码的典型工作流：**
     ```
     1. 读 Register Spec → 定义寄存器偏移和位域宏
     2. 读 Programming Guide → 实现 hw_init() 初始化函数
     3. 将 Timing 参数翻译为寄存器值 → 实现 crtc_mode_set()
     4. 实现 Plane 配置 → crtc_atomic_update()
     5. 实现中断处理 → irq_handler() 处理 VBlank
     6. 注册到 DRM 框架 → drm_crtc_init_with_planes()
     ```

3. **MIPI DSI Host 驱动**

   - **IP 核的概念：** 公司自研 SoC 中的 DSI 控制器可能是：
     - **购买的 IP 核**（如 Synopsys DesignWare MIPI DSI）→ 内核已有通用驱动 `dw-mipi-dsi.c`，你做适配即可
     - **自研 IP** → 需要完全从寄存器手册写起
     - 入职后第一时间确认！这决定了你的工作量大小

   - 职责：
     - 配置 DSI PHY（Lane 数、HS Clock、LP Clock）
     - 配置 Video Mode 参数
     - 提供发送 DSI 命令的接口（`mipi_dsi_host_ops.transfer()`）

   - **如果用的是 DesignWare DSI IP：**
     - 通用驱动：`drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c`
     - 你只需写平台适配层（时钟、PHY 配置、GPIO），参考 Rockchip 的 `dw-mipi-dsi-rockchip.c`

   - **如果是自研 DSI IP：**
     - 需要从寄存器手册实现完整的 DSI Host 驱动
     - 核心功能：PHY 初始化、LP/HS 模式切换、DCS 命令发送、Video 流控制

4. **DP Controller 驱动（类似逻辑）**

   - DP 控制器 IP 也可能是购买的（如 Cadence/Synopsys DP IP）或自研
   - 购买的 IP：内核可能有通用驱动可参考
   - 自研的 IP：从寄存器手册写起，核心是链路训练、AUX 通道、视频流配置

5. **整体调用链**
   ```
   drm_atomic_commit()
     → crtc_atomic_enable()   → 写寄存器配置 Timing, 启动扫描
     → encoder_atomic_enable() → 写寄存器配置 DSI/DP Host
     → panel->prepare()        → 屏幕上电 + 初始化
     → panel->enable()         → 开背光
   ```

**实践任务：**
- 画出从用户空间到最终点亮屏幕的完整调用链
- **精读一个开源的显示控制器驱动**（如 Rockchip VOP2），关注它是如何把寄存器操作包装成 DRM 回调的
- 练习：拿上面的 DISP_CTRL 寄存器例子，用 `BIT()` / `GENMASK()` / `FIELD_PREP()` 写出寄存器定义宏
- 阅读 `dw-mipi-dsi.c`（通用层）和 `dw-mipi-dsi-rockchip.c`（平台适配层），理解 IP 核通用驱动 + 平台适配的分层模式
- **入职后关键确认**：公司 SoC 的 DSI/DP 控制器是购买的 IP 还是自研？

---

### Day 14：设备树（DTS）中的显示配置

**学习目标：** 能写屏幕相关的设备树配置。

**学习内容：**

1. **显示通路在设备树中的表达**
   ```dts
   // 显示控制器（CRTC）
   &vop {
       status = "okay";
       // port 节点定义输出端口
   };

   // DSI Host
   &dsi0 {
       status = "okay";
       // 配置：lane 数、格式等

       panel@0 {
           compatible = "vendor,model";
           reg = <0>;
           reset-gpios = <&gpio 42 GPIO_ACTIVE_LOW>;
           vdd-supply = <&reg_lcd>;
           backlight = <&backlight>;
           port { ... };
       };
   };

   // 背光
   backlight: backlight {
       compatible = "pwm-backlight";
       pwms = <&pwm0 0 25000 0>;
       brightness-levels = <0 4 8 ... 255>;
       default-brightness-level = <200>;
   };
   ```

2. **关键属性**
   - `compatible`：匹配 panel 驱动
   - `reset-gpios`：复位引脚
   - `*-supply`：电源 Regulator
   - `backlight`：关联的背光设备
   - `port` / `ports`：OF-graph，描述显示通路连接关系

3. **Display Timing 在设备树中的表达**
   - 可以在 panel 节点中直接写 `display-timings` 子节点
   - 或在 panel 驱动中硬编码

**实践任务：**
- 写一段完整的屏幕设备树配置（DSI + Panel + Backlight）
- 理解 OF-graph 的 port/endpoint 连接机制

---

## 第 3 周：Bringup 实战（动手）

### Day 15-16：Bringup 完整流程梳理

**学习目标：** 掌握从零点亮一块新屏幕的完整步骤。

**Bringup 标准流程：**

```
Step 1: 获取屏幕资料
  ├── Datasheet（屏幕 IC 手册）
  ├── 初始化序列（Init Code，屏厂提供）
  ├── Timing 参数
  ├── 电源时序要求
  └── 接口信息（DSI Lane 数、Video/Command Mode）

Step 2: 硬件确认
  ├── 确认原理图中的连接（哪个 DSI、哪些 GPIO、哪路电源）
  ├── 确认 FPC 排线/转接板是否正确
  ├── 万用表测量供电是否正常
  └── 示波器/逻辑分析仪确认 Reset 信号

Step 3: 软件配置
  ├── 配置设备树（DSI Host + Panel + Backlight + Regulator + GPIO）
  ├── 编写/适配 Panel 驱动
  │   ├── 转换 Init Code 为内核格式
  │   ├── 填写 Timing 参数
  │   ├── 实现 prepare/enable/disable/unprepare
  │   └── 实现 get_modes
  ├── 配置 DSI Host 参数（Lane 数、HS Clock）
  └── Kernel config 检查（确保相关驱动已编译）

Step 4: 编译烧录验证
  ├── 编译内核/设备树
  ├── 烧录到目标板
  ├── 观察 dmesg 日志
  └── 检查屏幕是否亮起

Step 5: 调试（如果不亮）
  ├── 检查 dmesg 有无报错
  ├── 检查电源是否正常使能
  ├── 检查 Reset 时序
  ├── 检查 DSI 初始化命令是否发送成功
  ├── 用 modetest 测试纯色输出
  └── 逐步排查（见第 4 周调试章节）
```

**实践任务：**
- 将上述流程做成一份自己的 Checklist（以后每次 bringup 都用）
- 找组内同事要一份过去的 bringup 记录/commit，对照学习

---

### Day 17-18：Init Code 转换与 Panel 驱动编写

**学习目标：** 能把屏厂给的初始化序列转换成内核 Panel 驱动代码。

**学习内容：**

1. **屏厂 Init Code 的常见格式**
   - Excel 表格：寄存器地址 + 数据
   - C 数组：`{addr, data_len, {data...}, delay}`
   - Android 平台的 dtsi 格式

2. **转换为 Linux Panel 驱动格式**
   - 使用 `mipi_dsi_dcs_write_seq()` 宏：
     ```c
     mipi_dsi_dcs_write_seq(dsi, 0xB9, 0xFF, 0x83, 0x99);
     mipi_dsi_dcs_write_seq(dsi, 0xB1, 0x02, 0x04, 0x72, 0x92);
     msleep(10);
     ```
   - 或使用结构体数组 + 循环发送

3. **Timing 参数填写**
   ```c
   static const struct drm_display_mode default_mode = {
       .clock = 65000,          // kHz
       .hdisplay = 720,
       .hsync_start = 720 + 40, // hdisplay + hfp
       .hsync_end = 720 + 40 + 10, // + hsw
       .htotal = 720 + 40 + 10 + 20, // + hbp
       .vdisplay = 1280,
       .vsync_start = 1280 + 16, // vdisplay + vfp
       .vsync_end = 1280 + 16 + 4, // + vsw
       .vtotal = 1280 + 16 + 4 + 16, // + vbp
   };
   ```

4. **常见踩坑点**
   - Init code 中某些命令需要特定延时，不能省略
   - 某些屏的 init code 依赖发送顺序
   - `sleep out` 后通常需要 120ms 延时
   - Timing 参数与 init code 中设置的要一致

**实践任务：**
- 找一份屏厂 init code（或网上找一份），手动转换为 `mipi_dsi_dcs_write_seq` 格式
- 写一个完整的 panel 驱动文件（可参考已有驱动模板）

---

### Day 19-20：用户空间调试工具

**学习目标：** 掌握 modetest、drm_info 等工具的使用。

**学习内容：**

1. **modetest（libdrm 提供）**
   - 查看 DRM 设备信息：
     ```bash
     modetest -M rockchip    # 列出所有 connector/encoder/crtc/plane
     ```
   - 测试输出纯色画面：
     ```bash
     modetest -M rockchip -s <connector_id>@<crtc_id>:<WxH> -P <plane_id>@<crtc_id>:<WxH>+0+0@AR24
     ```
   - 这是验证屏幕是否点亮的最简单方法！

2. **常用 sysfs 节点**
   ```bash
   # 查看 DRM 设备
   ls /sys/class/drm/

   # 查看 connector 状态
   cat /sys/class/drm/card0-DSI-1/status
   cat /sys/class/drm/card0-DSI-1/modes

   # 背光控制
   cat /sys/class/backlight/*/brightness
   echo 128 > /sys/class/backlight/*/brightness

   # 查看显示通路 debug 信息
   cat /sys/kernel/debug/dri/0/summary
   ```

3. **devmem / i2ctool（硬件级调试）**
   - devmem：直接读写寄存器（谨慎使用）
   - i2ctool：对于 I2C 接口的触摸屏或桥接芯片

4. **fbdev 兼容接口**
   ```bash
   # 如果有 fbdev 兼容层
   cat /dev/urandom > /dev/fb0   # 填充随机数据（花屏 = 通路正常）
   ```

**实践任务：**
- 在开发板上执行 `modetest` 查看当前显示配置
- 用 `modetest` 输出纯色画面到屏幕上
- 查看 sysfs 中的 backlight 和 connector 信息

---

### Day 21：编译构建与烧录流程

**学习目标：** 掌握内核编译、设备树编译、烧录流程。

**学习内容：**

1. **Kernel 编译**
   - 确保 `CONFIG_DRM_PANEL_xxx=y` 或 `=m`
   - 确保 DSI Host 驱动已使能
   - 编译命令（以 ARM64 为例）：
     ```bash
     make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
     ```

2. **设备树编译**
   ```bash
   make ARCH=arm64 dtbs
   ```
   - 或者单独编译：
     ```bash
     dtc -I dts -O dtb -o output.dtb input.dts
     ```

3. **烧录方法**
   - 根据平台不同：fastboot / dd / 专用烧录工具
   - 有些平台支持单独替换 dtb/kernel image

**实践任务：**
- 走通一遍完整的编译 → 烧录 → 启动 → 看 dmesg 流程

---

### Day 22：U-Boot 显示驱动（别忘了 Bootloader！）

**学习目标：** 理解为什么 bringup 不只是 Linux 内核的事，U-Boot 阶段也需要点亮屏幕。

> 很多人以为屏幕 bringup = 只写 Linux DRM 驱动，这是不完整的。
> 实际产品中，用户按下电源键后希望**尽快看到开机 Logo**，这个 Logo 是 U-Boot 阶段显示的。
> 如果你不改 U-Boot，用户开机后会先黑屏好几秒才看到画面——这在消费电子产品中不可接受。

**学习内容：**

1. **为什么需要 U-Boot 显示？**
   - **开机 Logo / Splash Screen**：产品体验要求开机后尽快显示品牌 Logo
   - **无缝切换（Seamless Display）**：U-Boot 点亮屏幕 → 显示 Logo → Linux 内核接管 → 无闪烁过渡
   - **Recovery/充电界面**：关机充电、Recovery 模式也需要屏幕显示
   - **调试需要**：在 Linux 还没起来时就能通过屏幕看到 U-Boot 输出信息

2. **U-Boot 显示框架**
   - U-Boot 有自己的一套显示框架，与 Linux DRM **不同但有相似概念**
   - **U-Boot DM (Driver Model)** 下的 video 子系统：
     - `UCLASS_VIDEO`：视频输出设备
     - `UCLASS_VIDEO_BRIDGE`：桥接芯片
     - `UCLASS_PANEL`：屏幕面板
     - `UCLASS_BACKLIGHT`：背光
   - 设备树：U-Boot 和 Linux 通常**共用同一份设备树**（或 U-Boot 用 Linux DTS 的子集）
   - 部分 SoC 厂商（如 Rockchip）在 U-Boot 中实现了较完整的显示驱动

3. **U-Boot 中的 MIPI DSI 屏幕点亮流程**
   ```
   board_init()
     → 初始化电源（Regulator）
     → 初始化 GPIO（Reset）
     → 初始化 DSI Host
     → 发送屏幕 Init Code
     → 配置 Timing / 启动视频流
     → 将 Logo 图片写入 Framebuffer
     → 使能背光
   ```
   - 关键：Init Code 和 Timing 参数要和 Linux 端**保持一致**
   - Logo 图片通常是 BMP 格式，存放在特定分区（如 `logo` 分区）

4. **U-Boot 中的 DP/eDP 点亮流程**
   - 同样需要链路训练
   - 读取 EDID 获取 Timing（或硬编码）
   - 配置 DP 控制器输出

5. **Seamless Display（无缝显示切换）**
   - U-Boot 配置好 Timing 和 Framebuffer 后，不关闭显示控制器
   - Linux 内核启动时检测到显示已经使能，直接接管而不重新初始化
   - 需要 U-Boot 和 Linux 两端协调：
     - 共用 Framebuffer 地址（通过设备树或 cmdline 传递）
     - Linux 端的 `drm_fb_helper` 或 simpledrm 接管已有的 Framebuffer
   - 实现效果：开机全程屏幕不黑、不闪

6. **U-Boot 显示相关的关键配置（Kconfig）**
   ```
   CONFIG_VIDEO=y
   CONFIG_VIDEO_LOGO=y
   CONFIG_SPLASH_SCREEN=y
   CONFIG_DM_VIDEO=y
   CONFIG_VIDEO_MIPI_DSI=y       # MIPI DSI 支持
   CONFIG_VIDEO_BRIDGE=y          # 桥接芯片支持
   CONFIG_BACKLIGHT_PWM=y         # PWM 背光
   CONFIG_DISPLAY_PORT=y          # DP 支持（如有）
   ```

7. **常见的 U-Boot 显示代码位置**
   ```
   u-boot/
   ├── drivers/video/           # 显示驱动
   │   ├── rockchip/            # Rockchip 平台（VOP、DSI、DP等）
   │   ├── mipi_dsi.c           # MIPI DSI 通用层
   │   ├── dw_mipi_dsi.c        # DesignWare DSI Host
   │   └── bridge/              # 桥接芯片驱动
   ├── drivers/video/panel/     # Panel 驱动（或 simple_panel）
   ├── include/video/           # 头文件
   └── board/<vendor>/<board>/  # 板级初始化
   ```

**实践任务：**
- 阅读 U-Boot 源码中一个 panel 驱动（如 `simple_panel.c`），对比 Linux 端的 Panel 驱动
- 了解你所在平台的 U-Boot 是否已有显示支持，如果有，找到 Logo 显示的代码路径
- 搜索了解 "U-Boot splash screen" 的配置方法
- 向组内确认：公司的产品是否有 seamless display 的需求？如何实现？

---

## 第 4 周：高级调试与查漏补缺（进阶）

### Day 22-23：屏幕不亮的系统排查方法

**学习目标：** 建立系统化的调试思路，遇到不亮能冷静排查。

**调试决策树（超级重要！）：**

```
屏幕不亮？
├── dmesg 有 panel probe 成功吗？
│   ├── 没有 → 检查设备树 compatible、GPIO/Regulator 配置
│   └── 有 → 继续
├── dmesg 有 DSI 错误吗？
│   ├── timeout → DSI 命令发送失败，检查 Lane 数/Clock/Init Code
│   ├── PHY error → 检查 DSI PHY 配置
│   └── 没有 → 继续
├── modetest 能输出画面吗？
│   ├── 不能 → CRTC/Encoder/Connector 配置问题
│   └── 能 → 继续
├── 背光亮吗？
│   ├── 不亮 → 检查 PWM 配置、背光 IC、电源
│   └── 亮 → 继续
├── 有微弱亮光但无图像？
│   ├── 是 → Init code 可能有问题，或 Timing 不对
│   └── 全黑 → 检查电源时序
└── 花屏 / 偏色 / 闪烁？
    ├── 花屏 → Timing 参数或 Lane 配置有误
    ├── 偏色 → 色彩格式（RGB/BGR）或位序（bit order）不对
    └── 闪烁 → Pixel Clock 不准确或 TE 信号问题
```

**常见问题及解决方案：**

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 全黑无背光 | 背光电路异常、PWM 未配置 | 测量背光 IC 输出电压 |
| 有背光无图像 | Init code 错误、DSI 通信失败 | 检查 dmesg、抓 DSI 波形 |
| 花屏 | Timing 不对、Lane 数配错 | 核对 Datasheet Timing 参数 |
| 偏色（红蓝互换）| RGB/BGR 顺序错 | 修改 color format 配置 |
| 图像偏移 | HBP/VBP 值不对 | 微调 porch 参数 |
| 闪烁/撕裂 | Pixel Clock 不准、TE 未使能 | 调整时钟、使能 TE |
| 竖条纹/横条纹 | Lane 数不匹配或某 Lane 虚焊 | 检查硬件连接 |
| 只亮一半 | 双通道 DSI 只配了单通道 | 检查 DSI 配置 |

**实践任务：**
- 将调试决策树打印出来贴在工位上
- 回忆或模拟一个不亮的场景，用决策树走一遍排查流程

---

### Day 24-25：示波器与逻辑分析仪使用

**学习目标：** 能用硬件工具辅助排查显示问题。

**学习内容：**

1. **示波器排查**
   - 测量 Reset 信号时序（上升/下降沿、延时是否符合要求）
   - 测量电源上电时序（各路电源的上电顺序和延时）
   - 测量 PWM 背光信号（频率、占空比）

2. **逻辑分析仪**
   - 抓取 MIPI DSI LP 模式下的命令（如果有合适的工具）
   - 抓取 I2C 通信（触摸屏、桥接芯片）
   - 抓取 SPI 通信（SPI 屏）

3. **DSI 分析仪（了解即可）**
   - 专业的 MIPI DSI 协议分析仪（如 Tektronix、LeCroy）
   - 可以解析 DSI 包内容，但一般公司不一定有

**实践任务：**
- 用示波器测量一次屏幕的 Reset 时序，与 Datasheet 对比
- 学会示波器的基本操作（触发、时基、探头衰减）

---

### Day 26-27：FPGA Pre-silicon 验证与仿真环境

**学习目标：** 掌握在 FPGA 上做显示驱动早期 bringup 和验证的工作方法。

> 公司自研 SoC，FPGA pre-silicon 验证是你**确定会参与的工作**。
> 在芯片回片(Tape-in)之前，你写的驱动就要先在 FPGA 上跑起来并验证功能。

**学习内容：**

1. **自研 SoC 的芯片开发流程（驱动工程师视角）**
   ```
   IC 设计 RTL 编码
       ↓
   RTL 仿真验证 (IC 验证团队，SV/UVM)
       ↓
   FPGA Prototype 搭建 (RTL 综合到 FPGA 上)
       ↓
   ★ 驱动工程师介入：FPGA 上跑 Linux + 写驱动 + 功能验证 ★
       ↓
   发现 Bug → 反馈给 IC 设计 → 修 RTL → 重新综合到 FPGA → 再验证
       ↓ (循环直到稳定)
   Tape-out (流片)
       ↓
   等待 ~3 个月
       ↓
   芯片回片 (First Silicon / ES 样片)
       ↓
   ★ Silicon Bringup：在真实芯片上验证驱动 ★
       ↓
   量产
   ```
   - 你的工作横跨 **FPGA 验证** 和 **Silicon Bringup** 两个阶段
   - FPGA 阶段发现的问题修起来成本低（改 RTL 重新综合即可）
   - Silicon 阶段发现的硬件 Bug 代价极高（可能需要 ECO 甚至 respin）

2. **FPGA 验证环境的特点**
   - **速度极慢**：FPGA 主频通常只有 20-50 MHz（vs 真实芯片 1~2 GHz）
     - Linux 启动可能需要 10-30 分钟
     - 调试周期长，每次改代码→重启→验证可能耗时 1 小时+
     - **耐心是 FPGA 验证工程师的第一美德**
   - **硬件不完整**：FPGA 上通常只跑 SoC 的部分 IP（显示相关的），其他外设可能缺失或用 stub 替代
   - **接口适配**：
     - MIPI DSI PHY 可能用 FPGA 上的 FMC 子卡（如 Mixel PHY 评估板）模拟
     - DP PHY 可能用 FPGA 原生的 GTX/GTH 高速串行收发器
     - 有些情况下只能验证到 Timing 输出层面（示波器量 HSYNC/VSYNC），不一定能真正点亮屏幕
   - **调试手段**：
     - UART 是主要调试通道（串口 console + printk 大法）
     - JTAG 调试（连接 FPGA 板上的处理器核）
     - 示波器/逻辑分析仪抓信号
     - IC 团队可能提供 ILA（Integrated Logic Analyzer，FPGA 内部抓信号）支持

3. **常见 FPGA 平台（了解即可，入职后看实际用哪个）**
   - Xilinx (AMD)：ZCU102 / ZCU104 / VCU118 等
   - Intel (Altera)：Stratix 10 / Agilex 系列
   - FPGA 板 + FMC 子卡组合是常见的显示验证方案
   - 有些公司用 Palladium / Zebu 等硬件仿真加速器（比 FPGA 更快但更贵）

4. **驱动工程师在 FPGA 阶段的具体工作**
   - 搭建最小 Linux 系统（Kernel + 最小 rootfs + 设备树）
   - **从寄存器手册编写显示控制器驱动**（CRTC + Plane + Encoder）
   - **从寄存器手册编写 DSI/DP 控制器驱动**（或适配已购买 IP 的通用驱动）
   - 验证 Timing 输出（用示波器量 HSYNC/VSYNC/DE/Pixel Clock）
   - 验证 DSI 命令发送和接收
   - 验证 DP 链路训练
   - 跑 modetest 输出纯色画面，验证像素数据完整性
   - 跑不同分辨率/刷新率/色彩格式，验证各种配置组合
   - **记录每一个发现的 Bug**，用寄存器级别的证据（dump 值 + 预期值）反馈给 IC 团队
   - 可能需要写自动化测试脚本，批量跑回归测试

5. **FPGA 验证中的典型 Bug 类型**
   | Bug 类型 | 表现 | 驱动侧排查方式 |
   |---------|------|---------------|
   | 寄存器地址错误 | 读出全 0 或全 F | devmem 读取，对比 Register Spec |
   | 位域定义错误 | 配置值与预期不符 | dump 寄存器值逐位对比 |
   | 时序逻辑错误 | 偶发花屏、帧丢失 | 使能中断统计，看 underrun 计数 |
   | 复位逻辑问题 | 初始化后不工作 | 检查 reset 寄存器和时序 |
   | 时钟配置错误 | Pixel Clock 不准 | 示波器量实际输出频率 |
   | 跨时钟域问题 | 随机数据错误 | 多次重复测试，统计错误概率 |

6. **SystemVerilog (SV) 与仿真——你需要了解多少？**

   > 关键区分：**你是驱动工程师，不是 IC 验证工程师**。写 SV testbench 大概率不是你的本职，
   > 但在自研 SoC 公司里，你和 IC 团队之间的边界比外购 SoC 时要模糊得多。

   - **你肯定需要的能力：**
     - 读懂硬件寄存器手册（Register Specification）—— 这是你的圣经
     - 能看懂 IC 团队提供的 waveform（波形图），定位是驱动问题还是硬件问题
     - 能用 devmem / 自写的 debug 工具直接操作寄存器，绕过驱动层验证硬件行为
     - 理解时钟树（Clock Tree）：PLL → Divider → Pixel Clock，知道怎么配置时钟

   - **你可能需要的能力（取决于团队分工）：**
     - 跑 C-model 测试：有些公司会让驱动代码编译为 C-model，跑在 RTL 仿真环境中
       - 好处：不需要 FPGA 板，可以更早验证驱动逻辑
       - 你需要：让你的驱动代码可以脱离 Linux 内核独立编译（抽出寄存器操作层）
     - 读懂简单的 Verilog/SV 代码：有时候 IC 的文档不够详细，你可能需要直接看 RTL
     - 使用 VCS/Questa 加载波形文件（`.fsdb` / `.vcd`），对照你的寄存器操作看实际硬件行为

   - **你大概率不需要的能力：**
     - 编写完整的 SV testbench / UVM 验证环境
     - 做覆盖率（Coverage）分析
     - 跑 Formal Verification

   - **万一组里要求你接触仿真环境：**
     - 仿真工具三巨头：VCS (Synopsys)、Questa (Siemens/Mentor)、Xcelium (Cadence)
     - 波形查看工具：Verdi (Synopsys)、DVE、GTKWave（开源）
     - 搜索 "SystemVerilog tutorial for software engineers"
     - 重点学 SV 的 `task`/`function`、`interface`、`always_ff`/`always_comb` 就够日常读 RTL 了

7. **与 IC 设计团队的协作模式**
   - IC 团队提供：寄存器手册、Programming Guide、仿真波形、已知限制（Known Issues）、时钟树文档
   - 你提供：驱动代码、功能验证结果、Bug Report（附寄存器 dump + 复现步骤）
   - 沟通语言：寄存器名 + 偏移量 + 位域描述 + 时序图 + 波形截图
   - **Bug Report 模板建议：**
     ```
     [Bug] 显示控制器 Underrun 中断频繁触发

     环境：FPGA v1.3, Linux 5.15, 分辨率 1080x1920@60Hz
     现象：modetest 输出纯色画面时，约每 10 秒触发一次 underrun 中断
     寄存器 dump：
       DISP_STATUS (0x0010) = 0x0000_0004  (bit[2] underrun_flag = 1)
       DISP_DMA_STATUS (0x0014) = 0x0000_0000  (DMA idle)
     预期：正常输出时不应有 underrun
     分析：怀疑 DMA 取数带宽不足或 FIFO 深度不够
     复现步骤：
       1. modetest -M 公司 -s 31@30:1080x1920@AR24
       2. 等待 10 秒
       3. dmesg | grep underrun
     ```

**实践任务：**
- 了解组内 FPGA 验证环境的搭建方式（FPGA 板型号、如何烧录 bitstream、如何启动 Linux）
- 了解组内的 Bug 反馈流程（用什么工具？Jira/Redmine？Bug Report 模板是什么？）
- 学会用 Verdi 或 GTKWave 打开一个波形文件，找到特定信号看其时序变化
- 如果有 C-model 环境，了解如何编译和运行

---

### Day 28：显示质量优化与高级特性

**学习内容：**

1. **颜色校准**
   - Gamma 校正（Gamma 2.2）
   - 色温调节
   - DRM 中的 color management（CTM / Gamma LUT）

2. **刷新率与帧率**
   - 如何调整刷新率
   - 变速刷新（VRR，如果支持）

3. **功耗优化**
   - PSR (Panel Self Refresh)：eDP 屏幕的重要省电特性
   - 动态调整刷新率
   - 背光自适应调节

4. **多屏/多通路**
   - 双屏输出配置
   - DSI + HDMI / DSI + DP 同时输出

---

### Day 29：公司自研 SoC 显示驱动工作全景

**学习内容（针对公司媒体显示驱动组 + 自研 SoC）：**

1. **你的完整工作范围（自研 SoC 驱动工程师）**
   ```
   ┌─────────────────────────────────────────────────┐
   │              你的工作范围全景图                    │
   ├─────────────────────────────────────────────────┤
   │                                                 │
   │  Pre-silicon (FPGA)                             │
   │  ├── 从寄存器手册编写显示控制器驱动               │
   │  ├── 从寄存器手册编写 DSI/DP 控制器驱动           │
   │  ├── FPGA 板上功能验证                           │
   │  └── 反馈硬件 Bug 给 IC 团队                     │
   │                                                 │
   │  U-Boot                                         │
   │  ├── 显示控制器初始化                             │
   │  ├── DSI/DP 初始化                               │
   │  ├── 屏幕点亮 + Logo 显示                        │
   │  └── Seamless display 到 Linux                   │
   │                                                 │
   │  Linux Kernel (DRM/KMS)                         │
   │  ├── 显示控制器驱动 (CRTC + Plane)               │
   │  ├── DSI/DP Encoder 驱动                         │
   │  ├── Panel 驱动                                  │
   │  ├── Backlight 驱动                              │
   │  └── 显示质量调优 (Gamma/Color/功耗)              │
   │                                                 │
   │  Silicon Bringup (芯片回片后)                     │
   │  ├── 在真实芯片上验证所有驱动                      │
   │  ├── 性能调优 (带宽/功耗/延迟)                    │
   │  └── 量产问题支持                                │
   │                                                 │
   └─────────────────────────────────────────────────┘
   ```

2. **公司产品涉及的屏幕类型**
   - 遥控器屏幕：5-7 寸，MIPI DSI 或 eDP，720P/1080P/2K
   - 相机屏幕：1-2 寸，小尺寸高密度，MIPI DSI
   - 外接监视器：较大尺寸，HDMI / DP 输出
   - 图传模块的显示输出：HDMI / DP

3. **你需要协作的团队**
   - **IC 设计团队**：提供寄存器手册、RTL、FPGA bitstream；你反馈 Bug Report
   - **IC 验证团队**：他们做 SV/UVM 仿真；你可能需要看他们的波形、提供 C-model 测试用例
   - **硬件(HW)工程师**：确认原理图、PCB 布局、信号完整性、电源设计
   - **屏厂 (Panel Vendor)**：提供 init code、Timing 参数、Datasheet；你反馈兼容性问题
   - **系统软件团队**：U-Boot/Linux 基础平台、BSP
   - **上层应用/框架团队**：使用你提供的 DRM 接口，跑 Wayland/Qt/Flutter 等

4. **代码规范与提交流程**
   - 内核代码风格（`checkpatch.pl`）
   - Commit message 规范
   - Code review 流程
   - 自研 SoC 的代码通常在内部仓库，不提交到上游社区

5. **入职第一周必做清单**
   - [ ] 获取公司 SoC 的显示模块寄存器手册
   - [ ] 获取当前项目使用的屏幕 Datasheet 和 Init Code
   - [ ] 了解内部代码仓库地址、分支管理策略
   - [ ] 了解编译系统（Makefile / Yocto / Buildroot？）
   - [ ] 了解烧录/刷机流程
   - [ ] 了解 FPGA 验证环境（FPGA 板型号、如何连接、如何烧录 bitstream）
   - [ ] 了解 U-Boot 源码位置和编译方式
   - [ ] 找到组内现有的显示驱动代码，先读一遍
   - [ ] 了解 Bug 追踪系统（Jira / Redmine / 内部系统？）
   - [ ] 认识 IC 设计团队的对接人

---

### Day 30：总结与自检

**自检清单：**

硬件与协议：
- [ ] 能否手绘 LCD Timing 图并标注所有参数？
- [ ] 能否解释 MIPI DSI Video Mode 与 Command Mode 的区别？
- [ ] 能否手动计算 Pixel Clock 和 DSI Lane 带宽？
- [ ] 能否解释 DP 链路训练的 CR 和 EQ 两个阶段？
- [ ] 能否说出 MIPI DSI 与 DP/eDP 在 bringup 流程上的核心差异？

Linux DRM/KMS：
- [ ] 能否画出 DRM 对象关系图（FB→Plane→CRTC→Encoder→Connector→Panel）？
- [ ] 能否解释 Panel 驱动中 prepare/enable/disable/unprepare 的区别和调用顺序？
- [ ] 能否从屏厂 init code 转换出内核 panel 驱动代码？
- [ ] 能否写一段完整的屏幕设备树配置？

U-Boot 与全链路：
- [ ] 能否解释为什么 bringup 需要同时改 U-Boot 和 Linux？
- [ ] 能否说出 seamless display（无缝切换）的基本原理？
- [ ] 能否描述从按下电源键到看到 Linux 桌面，屏幕经历了哪些阶段？

调试：
- [ ] 能否用 modetest 测试屏幕输出？
- [ ] 看到屏幕不亮，能否按照调试决策树系统排查？
- [ ] 能否用示波器检查 Reset 时序和电源时序？

FPGA Pre-silicon 与自研 SoC：
- [ ] 能否描述自研 SoC 从 RTL → FPGA → Tape-out → Silicon 的完整流程？
- [ ] 能否在 FPGA 板上搭建最小 Linux 环境并启动？
- [ ] 能否从寄存器手册独立定义寄存器宏并编写初始化代码？
- [ ] 能否写出一份规范的 Bug Report（含寄存器 dump、复现步骤、预期 vs 实际）？
- [ ] 能否看懂 IC 团队提供的仿真波形，定位是驱动问题还是硬件问题？
- [ ] 是否了解公司 SoC 的 DSI/DP 控制器是购买 IP 还是自研？

---

## 推荐学习资源汇总

### 书籍/文档
- **Linux 内核文档** `Documentation/gpu/` —— 官方 DRM/KMS 文档
- **《嵌入式 Linux 驱动开发实战》** —— 基础回顾
- MIPI DSI Specification（MIPI Alliance 官方，可搜非官方解读）
- VESA DisplayPort Standard（可搜非官方解读）

### 文章/博客
- 搜索 "Linux DRM KMS 详解"
- 搜索 "MIPI DSI 协议详解"
- 搜索 "DisplayPort Link Training 详解"
- 搜索 "Linux panel driver tutorial"
- 搜索 "The DRM/KMS subsystem from a newbie's point of view"（推荐）
- 搜索 "U-Boot splash screen display driver"
- LWN.net 上的 DRM 相关文章

### 视频
- YouTube/B站 搜索 "Linux DRM tutorial"
- YouTube 搜索 "Boris Brezillon DRM"
- B站搜索 "MIPI DSI 协议" / "LCD 驱动开发" / "DisplayPort 原理"

### 代码阅读
- `drivers/gpu/drm/panel/panel-ilitek-ili9881c.c` —— MIPI DSI panel 典范
- `drivers/gpu/drm/panel/panel-simple.c` / `panel-edp.c` —— 最简单的 panel 驱动（eDP 屏参考后者）
- `drivers/gpu/drm/bridge/` —— 桥接芯片驱动参考
- `drivers/gpu/drm/drm_dp_helper.c` —— DP 辅助函数库（链路训练等）
- `include/drm/drm_dp_helper.h` —— DPCD 地址定义
- U-Boot `drivers/video/` —— U-Boot 显示驱动

### 工具
- `modetest`（libdrm 自带）—— 屏幕调试必备
- `checkpatch.pl`（内核自带）—— 代码检查
- 示波器 + 逻辑分析仪 —— 硬件调试

---

## 每日时间分配建议

| 时间段 | 内容 | 时长 |
|--------|------|------|
| 上午 | 理论学习（阅读文档/Datasheet/源码） | 2-3h |
| 下午 | 动手实践（写代码/调板子/用工具） | 3-4h |
| 晚上 | 整理笔记 + 复习 + 看一篇技术博客 | 1-2h |

**关键原则：**
1. **理论 : 实践 = 4 : 6** —— 动手远比看更有效
2. **每天写笔记** —— 写下来的才是你的
3. **不懂就问** —— 新人问问题是天然的权利，入职两个月后再问就不好意思了
4. **先跑通再理解** —— 先 bringup 成功一块屏（哪怕是照猫画虎），再回头理解原理
5. **从简单到复杂** —— 先搞定 MIPI DSI + Video Mode 的屏，其他都是变种

---

> **给自己的话：** 屏幕 bringup 这件事并不神秘。它本质上就是：
> 1. 给屏幕正确供电
> 2. 按正确时序发送初始化命令（DSI）或完成链路训练（DP）
> 3. 以正确的 Timing 推送像素数据
>
> 自研 SoC 的额外挑战在于：你拿到的不是现成的 vendor driver，而是一份寄存器手册。
> 但这也意味着——你对硬件的理解会比用现成方案的人深刻得多，这才是真正值钱的能力。
>
> 完整的 bringup 链路是：**FPGA 验证 → U-Boot 点亮 → Linux DRM 驱动 → 产品量产**。
> 一个月后你会回看这份文档，觉得当初的恐惧毫无必要。
