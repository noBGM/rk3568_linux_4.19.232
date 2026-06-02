# 屏幕 Command 模式与 Video 模式切换研究报告

## 1. 概述

本文档研究在 RK356x Linux DRM 框架下，MIPI DSI 屏幕的 **Command 模式（命令模式）** 与 **Video 模式（视频模式）** 切换机制，分析需要修改的组件及其接口，并从公司现有驱动代码中选取最佳参考实现。

### 1.1 两种模式的核心区别

| 特性 | Video Mode | Command Mode |
|------|-----------|-------------|
| 数据传输方式 | 类似 HDMI/DP，由主机持续推送像素流 | 类似 SPI 屏，主机发送命令后外设自刷新 |
| 时序信号 | 需要 HSYNC/VSYNC/DE 信号 | 不需要，使用 TE (Tearing Effect) 信号同步 |
| 带宽需求 | 持续占用 DSI 带宽 | 仅在更新帧时占用，可节省带宽 |
| 典型应用 | 高刷新率(60fps+)视频播放 | 静态显示、低功耗场景、带 RAM 的 Panel |
| 硬件要求 | DSI Host 持续输出 | Panel 需内置 GRAM，DSI Host 支持 DBI 接口 |

---

## 2. 关键组件分析

在 DRM 框架中，Command/Video 模式的切换涉及以下组件层次：

```
     ┌──────────────────────────────────┐
     │         Userspace (DRM API)      │
     └──────────────┬───────────────────┘
                    │
     ┌──────────────▼───────────────────┐
     │  CRTC (VOP2)                     │  ← 显示控制器
     │  - 配置输出模式 (P888/P666等)    │
     │  - 配置输出接口 (MIPI/LVDS等)    │
     │  - 可能涉及 MCU 接口配置         │
     └──────────────┬───────────────────┘
                    │
     ┌──────────────▼───────────────────┐
     │  Encoder (DSI Encoder)           │  ← 编码器
     │  - atomic_check: 设置 output_mode│
     │  - atomic_mode_set: 缓存 mode    │
     │  - enable: 触发 DSI 配置         │
     └──────────────┬───────────────────┘
                    │
     ┌──────────────▼───────────────────┐
     │  DSI Host Controller             │  ← MIPI DSI 主机控制器
     │  - 配置 DSI_MODE_CFG 寄存器      │
     │  - 配置 DPI vs DBI 接口          │
     │  - 设置时序参数 (Video Mode)     │
     │  - 处理 TE 信号 (Command Mode)   │
     └──────────────┬───────────────────┘
                    │
     ┌──────────────▼───────────────────┐
     │  DSI Panel / Bridge              │  ← 面板/桥接芯片
     │  - mode_flags 决定工作模式       │
     │  - DCS 初始化序列                 │
     └──────────────────────────────────┘
```

### 2.1 MIPI DSI Mode Flags（模式标志定义）

**文件**: [kernel/include/drm/drm_mipi_dsi.h](kernel/include/drm/drm_mipi_dsi.h)
**关键文件**: [kernel/include/dt-bindings/display/drm_mipi_dsi.h](kernel/include/dt-bindings/display/drm_mipi_dsi.h)

```c
/* Video Mode 相关标志 (drm_mipi_dsi.h:122-148) */
#define MIPI_DSI_MODE_VIDEO             BIT(0)  // 主标志: 区分 Video/Command
#define MIPI_DSI_MODE_VIDEO_BURST       BIT(1)  // Video: Burst 模式
#define MIPI_DSI_MODE_VIDEO_SYNC_PULSE  BIT(2)  // Video: 同步脉冲模式
#define MIPI_DSI_MODE_VIDEO_AUTO_VERT   BIT(3)
#define MIPI_DSI_MODE_VIDEO_HSE         BIT(4)
#define MIPI_DSI_MODE_VIDEO_HFP         BIT(5)
#define MIPI_DSI_MODE_VIDEO_HBP         BIT(6)
#define MIPI_DSI_MODE_VIDEO_HSA         BIT(7)
#define MIPI_DSI_MODE_VSYNC_FLUSH       BIT(8)
#define MIPI_DSI_MODE_EOT_PACKET        BIT(9)
#define MIPI_DSI_CLOCK_NON_CONTINUOUS   BIT(10)
#define MIPI_DSI_MODE_LPM               BIT(11)
```

**核心规则**: 若 `mode_flags` 中 **不包含** `MIPI_DSI_MODE_VIDEO`，则默认为 Command Mode。

---

## 3. 最合适的公司代码选择

### 3.1 候选驱动对比

| 公司/芯片 | DSI Host 驱动路径 | CMD/VID 分离度 | 参考价值 |
|-----------|------------------|---------------|---------|
| **Rockchip (瑞芯微)** | [rockchip/dw-mipi-dsi.c](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c) | 中等 (同一文件分函数) | ★★★★★ 最相关 |
| **Qualcomm/MSM** | [msm/disp/dpu1/](kernel/drivers/gpu/drm/msm/disp/dpu1/) | 高 (独立 cmd/vid 文件) | ★★★★☆ 架构参考 |
| **Mediatek** | [mediatek/mtk_dsi.c](kernel/drivers/gpu/drm/mediatek/mtk_dsi.c) | 中等 | ★★★☆☆ |
| **Exynos (Samsung)** | [exynos/exynos_drm_dsi.c](kernel/drivers/gpu/drm/exynos/exynos_drm_dsi.c) | 低 | ★★☆☆☆ |
| **TI OMAP** | [omapdrm/displays/panel-dsi-cm.c](kernel/drivers/gpu/drm/omapdrm/displays/panel-dsi-cm.c) | 中 (专门的 cmd panel) | ★★★☆☆ |
| **Synopsys DW (通用)** | [bridge/synopsys/dw-mipi-dsi.c](kernel/drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c) | 中等 | ★★★☆☆ |

### 3.2 推荐: 优先研究 Rockchip dw-mipi-dsi.c，架构参考 MSM dpu

**理由**:
1. **Rockchip dw-mipi-dsi.c** 是 RK356x 平台原生驱动，直接对应硬件寄存器，修改后可立即验证
2. **MSM dpu** 驱动是 Linux DRM 社区中 Command/Video 模式分离最清晰的实现 (`dpu_encoder_phys_cmd.c` + `dpu_encoder_phys_vid.c`)，架构设计值得参考
3. Rockchip 的 `rk618/rk628` 桥接芯片也有 DSI Command 模式支持，适合研究桥接芯片侧的处理

---

## 4. Rockchip dw-mipi-dsi.c 代码详解

### 4.1 核心数据结构和枚举

**文件**: [rockchip/dw-mipi-dsi.c:222-225](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L222-L225)

```c
enum operation_mode {
    VIDEO_MODE,    // = 0
    COMMAND_MODE,  // = 1
};
```

**关键结构体域**: `struct dw_mipi_dsi` (line 266-297)
```c
struct dw_mipi_dsi {
    // ...
    unsigned long mode_flags;    // 来自 panel 的 MIPI_DSI_MODE_* 标志
    u32 format;                  // 像素格式
    u32 channel;                 // DSI 虚拟通道
    // ...
};
```

### 4.2 模式设置的核心寄存器

**DSI_MODE_CFG 寄存器** (offset 0x034, line 67-68):
```c
#define DSI_MODE_CFG            0x034
#define CMD_VIDEO_MODE(x)       UPDATE(x, 0, 0)  // Bit 0: 0=Video, 1=Command
```

**DSI_VID_MODE_CFG 寄存器** (offset 0x038, line 69-79):
```c
#define DSI_VID_MODE_CFG        0x038
#define VPG_EN                  BIT(16)
#define LP_CMD_EN               BIT(15)
#define FRAME_BTA_ACK_EN        BIT(14)
#define LP_HFP_EN               BIT(13)
#define LP_HBP_EN               BIT(12)
#define LP_VACT_EN              BIT(11)
#define LP_VFP_EN               BIT(10)
#define LP_VBP_EN               BIT(9)
#define LP_VSA_EN               BIT(8)
#define VID_MODE_TYPE(x)        UPDATE(x, 1, 0)  // 0=NonBurstSyncPulse
                                                  // 1=NonBurstSyncEvents
                                                  // 2=Burst
```

**DSI_CMD_MODE_CFG 寄存器** (offset 0x068, line 97-112):
```c
#define DSI_CMD_MODE_CFG        0x068
#define MAX_RD_PKT_SIZE         BIT(24)
// ... 各种 TX 模式选择位
#define TEAR_FX_EN              BIT(0)    // TE (Tearing Effect) 信号使能
```

**DSI_DBI_VCID 寄存器** (offset 0x01c, line 59-60):
```c
#define DSI_DBI_VCID            0x01c    // DBI (Command Mode) 虚拟通道ID
#define DBI_VCID(x)             UPDATE(x, 1, 0)
```

### 4.3 Video Mode 配置函数

**文件**: [rockchip/dw-mipi-dsi.c:993-1053](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L993-L1053)

```c
static void dw_mipi_dsi_set_vid_mode(struct dw_mipi_dsi *dsi)
{
    struct drm_display_mode *mode = &dsi->mode;

    // 1. 配置 Low Power 使能区域
    val = LP_HFP_EN | LP_HBP_EN | LP_VACT_EN | LP_VFP_EN | LP_VBP_EN | LP_VSA_EN;

    // 2. 根据 mode_flags 关闭特定区域
    if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP)   val &= ~LP_HFP_EN;
    if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP)   val &= ~LP_HBP_EN;

    // 3. 选择 Video 子类型 (Burst / Non-Burst Sync Pulse / Non-Burst Sync Events)
    if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
        val |= VID_MODE_TYPE_BURST;
    else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
        val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
    else
        val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

    regmap_write(dsi->regmap, DSI_VID_MODE_CFG, val);

    // 4. 配置 Non-Continuous Clock
    if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
        regmap_update_bits(dsi->regmap, DSI_LPCLK_CTRL,
                          AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);

    // 5. 配置视频包大小
    regmap_write(dsi->regmap, DSI_VID_PKT_SIZE, VID_PKT_SIZE(val));

    // 6. 计算并写入时序参数
    //    - HLINE_TIME, HSA_TIME, HBP_TIME (以 lane byte clock 为单位)
    //    - VACTIVE_LINES, VSA_LINES, VFP_LINES, VBP_LINES
    // ...

    // 7. 最终写入 DSI_MODE_CFG = VIDEO_MODE (0)
    regmap_write(dsi->regmap, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
}
```

### 4.4 Command Mode 配置函数

**文件**: [rockchip/dw-mipi-dsi.c:1055-1065](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1055-L1065)

```c
static void dw_mipi_dsi_set_cmd_mode(struct dw_mipi_dsi *dsi)
{
    struct drm_display_mode *mode = &dsi->mode;

    // 1. 配置 DBI 虚拟通道
    regmap_write(dsi->regmap, DSI_DBI_VCID, DBI_VCID(dsi->channel));

    // 2. 禁用 DCS Long Write TX
    regmap_update_bits(dsi->regmap, DSI_CMD_MODE_CFG, DCS_LW_TX, 0);

    // 3. 配置 EDPI 命令大小 (= 水平像素数)
    regmap_write(dsi->regmap, DSI_EDPI_CMD_SIZE,
                 EDPI_ALLOWED_CMD_SIZE(mode->hdisplay));

    // 4. 最终写入 DSI_MODE_CFG = COMMAND_MODE (1)
    regmap_write(dsi->regmap, DSI_MODE_CFG,
                 CMD_VIDEO_MODE(COMMAND_MODE));
}
```

### 4.5 模式决策点 (Enable 路径)

**文件**: [rockchip/dw-mipi-dsi.c:1306-1309](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1306-L1309)

```c
static void dw_mipi_dsi_enable(struct dw_mipi_dsi *dsi)
{
    // ... DPI 配置 (颜色编码、极性) ...

    // 核心决策点: 根据 mode_flags 选择 Video 或 Command 模式
    if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO)
        dw_mipi_dsi_set_vid_mode(dsi);
    else
        dw_mipi_dsi_set_cmd_mode(dsi);
}
```

### 4.6 Pre-Enable 和 Disable 路径

```c
// Pre-Enable: 始终先设置为 Command Mode (line 1233)
static void dw_mipi_dsi_pre_enable(struct dw_mipi_dsi *dsi)
{
    regmap_write(dsi->regmap, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
    // ... 时钟、PHY 配置 ...
}

// Disable: 回到 Command Mode (line 1072)
static void dw_mipi_dsi_disable(struct dw_mipi_dsi *dsi)
{
    regmap_write(dsi->regmap, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}
```

### 4.7 Encoder Atomic Check (连接 VOP2)

**文件**: [rockchip/dw-mipi-dsi.c:1357-1413](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1357-L1413)

```c
static int dw_mipi_dsi_encoder_atomic_check(...)
{
    // 根据 DSI pixel format 设置 VOP output mode
    switch (dsi->format) {
    case MIPI_DSI_FMT_RGB888: s->output_mode = ROCKCHIP_OUT_MODE_P888; break;
    case MIPI_DSI_FMT_RGB666: s->output_mode = ROCKCHIP_OUT_MODE_P666; break;
    case MIPI_DSI_FMT_RGB565: s->output_mode = ROCKCHIP_OUT_MODE_P565; break;
    }

    // 设置输出类型和接口
    s->output_type = DRM_MODE_CONNECTOR_DSI;
    s->output_if   = dsi->id ? VOP_OUTPUT_IF_MIPI1 : VOP_OUTPUT_IF_MIPI0;
    s->bus_format  = info->bus_formats[0];  // 或默认 MEDIA_BUS_FMT_RGB888_1X24
    s->bus_flags   = info->bus_flags;

    // RK3568 特定: MIPI 在 posedge 驱动数据
    if (dsi->pdata->soc_type == RK3568) {
        s->bus_flags &= ~DRM_BUS_FLAG_PIXDATA_NEGEDGE;
        s->bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;
    }

    // 双通道配置
    if (dsi->slave) {
        s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
        if (dsi->data_swap)
            s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;
    }
}
```

---

## 5. MSM DPU 驱动的架构参考

### 5.1 独立的 Command/Video 物理编码器

MSM 驱动将物理编码器分为两个独立的子类:

```
dpu_encoder_phys (基类)
├── dpu_encoder_phys_vid    → dpu_encoder_phys_vid.c  (Video Mode)
└── dpu_encoder_phys_cmd    → dpu_encoder_phys_cmd.c  (Command Mode)
```

**关键文件**:
- [msm/disp/dpu1/dpu_encoder_phys.h](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys.h) — 基类和接口定义
- [msm/disp/dpu1/dpu_encoder_phys_vid.c](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c) — Video 模式实现
- [msm/disp/dpu1/dpu_encoder_phys_cmd.c](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_cmd.c) — Command 模式实现

### 5.2 核心区别总结

| 方面 | Video Mode (phys_vid) | Command Mode (phys_cmd) |
|------|----------------------|------------------------|
| 接口模式 | `intf_mode_sel = DPU_CTL_MODE_SEL_VID` | `intf_mode_sel = DPU_CTL_MODE_SEL_CMD` |
| 帧同步 | VSYNC 硬件中断 | TE (Tear Effect) 或 Ping-Pong Done 中断 |
| 触发机制 | 硬件自动持续输出 | `trigger_start` → CTL_START |
| Kickoff 超时 | 84ms | 更长 |
| VBLANK | 硬件驱动 | 软件模拟 (等待 TE) |
| 自动时钟门控 | 不适用 | `prepare_idle_pc` 支持 |

### 5.3 值得借鉴的设计模式

1. **intf_mode 枚举**: 在物理编码器级别区分 `DPU_CTL_MODE_SEL_CMD` / `DPU_CTL_MODE_SEL_VID`
2. **wait_for_commit_done / wait_for_vblank**: 用虚函数表实现模式特定行为
3. **ping-pong 中断 vs VSYNC 中断**: Command 模式使用 PP_DONE，Video 模式使用 VSYNC

---

## 6. 模式切换需要修改的组件清单

### 6.1 Panel 驱动层

**修改内容**: Panel 驱动需要在 `mipi_dsi_device` 中正确设置 `mode_flags`

```c
// 在 Panel 驱动的 probe 或初始化中
struct mipi_dsi_device *dsi;

// 如果 Panel 支持 Command Mode (有 GRAM)
dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;
// 不包含 MIPI_DSI_MODE_VIDEO → 默认为 Command Mode

// 如果 Panel 支持 Video Mode
dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
                  MIPI_DSI_MODE_VIDEO_BURST |
                  MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
                  MIPI_DSI_CLOCK_NON_CONTINUOUS;
```

**需要修改的文件**:
- [kernel/drivers/gpu/drm/panel/](kernel/drivers/gpu/drm/panel/) 下对应的 Panel 驱动
- 或通过 Device Tree 的 `dsi,flags` 属性传参

### 6.2 DSI Host 控制器驱动层

**修改内容**: dw-mipi-dsi.c 已经有完整的 Video/Command 切换代码，但 Command Mode 功能可能不够完善

需要增强的点:
1. **TE (Tearing Effect) 信号处理**: 当前 `TEAR_FX_EN` 位在 Command Mode 配置中未显式使能
2. **Command Mode 中断处理**: TE 中断和 BTA (Bus Turn Around) 中断的回调
3. **DBI 接口的读写指令**: Command Mode 下需要通过 DBI 接口发送像素数据
4. **低功耗模式切换**: 在 Command Mode 的空闲期间进入 ULPS

**需要修改的文件**:
- [rockchip/dw-mipi-dsi.c](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c)
- 可能需要参考 [bridge/synopsys/dw-mipi-dsi.c](kernel/drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c) 的 `dw_mipi_dsi_set_mode()` 和 `dw_mipi_dsi_command_mode_config()` 函数

### 6.3 VOP2 (CRTC) 驱动层

**修改内容**: VOP2 需要支持 Command Mode 下的输出配置

VOP2 Header 中已预留 MCU 输出相关寄存器 (见 [rockchip_drm_vop.h:270-281](kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop.h#L270-L281)):
```c
/* MCU OUTPUT */
struct vop_reg mcu_pix_total;
struct vop_reg mcu_cs_pst;
struct vop_reg mcu_cs_pend;
struct vop_reg mcu_rw_pst;
struct vop_reg mcu_rw_pend;
struct vop_reg mcu_clk_sel;
struct vop_reg mcu_hold_mode;
struct vop_reg mcu_frame_st;
struct vop_reg mcu_rs;
struct vop_reg mcu_bypass;
struct vop_reg mcu_type;
struct vop_reg mcu_rw_bypass_port;
```

**需要做的**:
- 确认 VOP2 的 MCU 寄存器是否在 RK3568 上有效
- 在 CRTC atomic_enable 中根据模式设置 MCU 配置
- 可能需要通过 `rockchip_crtc_state` 传递 Command Mode 信息

**需要修改的文件**:
- [rockchip/rockchip_drm_vop2.c](kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c)
- [rockchip/rockchip_drm_vop.h](kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop.h)
- [rockchip/rockchip_drm_drv.h](kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.h)

### 6.4 Encoder 层 (DRM Core)

**修改内容**: 增加 connector 属性或 CRTC property 让 Userspace 可以动态切换模式

可能的方案:
- 添加 DRM property: `"DSI_MODE"` (enum: video / command)
- 通过 connector state 传递到 encoder 的 `atomic_check`
- 在 `dw_mipi_dsi_encoder_atomic_check` 中将模式传递给 CRTC state

**需要修改的文件**:
- [rockchip/dw-mipi-dsi.c](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c)
- [drm_atomic_helper.c](kernel/drivers/gpu/drm/drm_atomic_helper.c) (可能需要)
- [drm_connector.c](kernel/drivers/gpu/drm/drm_connector.c) (可能需要添加 DRM property)

### 6.5 Device Tree 层

**修改内容**: 在 DTS 中通过 Panel 节点设置 mode flags

```dts
&dsi {
    panel@0 {
        compatible = "...";
        /* Video Mode 示例 */
        dsi,flags = <(MIPI_DSI_MODE_VIDEO |
                      MIPI_DSI_MODE_VIDEO_BURST |
                      MIPI_DSI_MODE_VIDEO_SYNC_PULSE)>;
        dsi,format = <MIPI_DSI_FMT_RGB888>;
        dsi,lanes = <4>;
    };
};
```

**需要修改的文件**:
- `arch/arm64/boot/dts/rockchip/rk3568*.dts` / `rk3566*.dts`

### 6.6 桥接芯片驱动层（如适用）

如果使用 RK618/RK628 等桥接芯片，也需要修改:
- [rockchip/rk618/rk618_dsi.c](kernel/drivers/gpu/drm/rockchip/rk618/rk618_dsi.c)
- [rockchip/rk628/rk628_dsi.c](kernel/drivers/gpu/drm/rockchip/rk628/rk628_dsi.c)

---

## 7. 实施建议

### 7.1 推荐的实施路径

```
Phase 1: 静态 Command Mode 支持
├── 选一块带 GRAM 的 Command Mode Panel
├── 编写 Panel 驱动 (设置 mode_flags 不含 MIPI_DSI_MODE_VIDEO)
├── 编写 Device Tree 节点
├── 验证 dw-mipi-dsi.c 的 Command Mode 路径是否正常工作
└── 增强 DSI 驱动中缺失的功能 (TE 信号、DBI 接口)

Phase 2: 动态切换支持
├── 添加 DRM Property "DSI_MODE" (video / command)
├── 在 encoder atomic_check 中处理模式切换
├── 实现运行时 DSI 模式切换 (enable/disable 序列)
└── 添加 Userspace 测试工具

Phase 3: 优化
├── 低功耗模式集成
├── TE 信号自适应时序
└── 性能测试与调优
```

### 7.2 关键注意事项

1. **切换时需要完整的 disable → enable 循环**: DSI 模式切换不能"在线"完成，需要先 disable 当前模式再 enable 新模式
2. **DCS 命令序列**: Command Mode Panel 需要特定的 DCS 初始化序列 (exit_sleep_mode → set_display_on 等)
3. **时钟配置差异**: Command Mode 下可能不需要 dclk 持续输出，需要确认 VOP2 侧的时钟门控
4. **TE GPIO**: Command Mode 需要配置 TE GPIO 中断来同步帧更新
5. **RK3568 的 VOP2 对 Command Mode 的支持程度**: 需要查阅 RK3568 TRM 确认 MCU 接口是否可用

---

## 8. 参考代码索引

### 8.1 核心分析文件 (Rockchip 原生)

| 文件 | 关键内容 |
|------|---------|
| [rockchip/dw-mipi-dsi.c](kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c) | DSI Host 驱动，含 Video/Command 模式切换 |
| [rockchip/rockchip_drm_vop2.c](kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c) | VOP2 显示控制器驱动 |
| [rockchip/rockchip_drm_vop.h](kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop.h) | VOP 寄存器定义 (含 MCU 寄存器) |
| [rockchip/rockchip_drm_drv.h](kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.h) | Rockchip DRM 私有结构体 (output_mode/output_if) |
| [rockchip/rk618/rk618_dsi.c](kernel/drivers/gpu/drm/rockchip/rk618/rk618_dsi.c) | RK618 桥接芯片 DSI 驱动 |
| [rockchip/rk628/rk628_dsi.c](kernel/drivers/gpu/drm/rockchip/rk628/rk628_dsi.c) | RK628 桥接芯片 DSI 驱动 |

### 8.2 参考实现文件 (其他厂商)

| 文件 | 参考价值 |
|------|---------|
| [msm/disp/dpu1/dpu_encoder_phys_cmd.c](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_cmd.c) | Command Mode 物理编码器实现 |
| [msm/disp/dpu1/dpu_encoder_phys_vid.c](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_vid.c) | Video Mode 物理编码器实现 |
| [msm/disp/dpu1/dpu_encoder_phys.h](kernel/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys.h) | 编码器基类和虚函数接口定义 |
| [msm/dsi/dsi_host.c](kernel/drivers/gpu/drm/msm/dsi/dsi_host.c) | MSM DSI Host 驱动 |
| [omapdrm/displays/panel-dsi-cm.c](kernel/drivers/gpu/drm/omapdrm/displays/panel-dsi-cm.c) | TI OMAP 的 Command Mode Panel 参考 |
| [bridge/synopsys/dw-mipi-dsi.c](kernel/drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c) | Synopsys DW DSI 通用驱动 |
| [mediatek/mtk_dsi.c](kernel/drivers/gpu/drm/mediatek/mtk_dsi.c) | Mediatek DSI 驱动 |

### 8.3 MIPI DSI 规范头文件

| 文件 | 内容 |
|------|------|
| [include/drm/drm_mipi_dsi.h](kernel/include/drm/drm_mipi_dsi.h) | DSI Host/Device API 和 Mode Flags 定义 |
| [include/dt-bindings/display/drm_mipi_dsi.h](kernel/include/dt-bindings/display/drm_mipi_dsi.h) | DT 绑定用 DSI 标志定义 |
| [include/video/mipi_display.h](kernel/include/video/mipi_display.h) | MIPI DCS 命令码定义 |

---

## 9. 总结

**最合适研究的公司代码**: **Rockchip 自己的 dw-mipi-dsi.c**，因为:
- 直接运行在 RK356x 平台上，硬件寄存器映射完全准确
- 已实现完整的 `set_vid_mode()` 和 `set_cmd_mode()` 函数
- 有明确的 `enum operation_mode { VIDEO_MODE, COMMAND_MODE }`
- 与 VOP2 的接口通过 `atomic_check` 已打通

**架构参考**: **Qualcomm MSM dpu** 驱动，其 `dpu_encoder_phys_cmd.c` 和 `dpu_encoder_phys_vid.c` 的清晰分离是 Linux DRM 中最佳实践，特别是在中断处理（TE vs VSYNC）、帧同步机制、kickoff 超时处理等方面值得借鉴。

**核心修改范围**:
1. **Panel 驱动**: 设置 mode_flags
2. **DSI Host 驱动**: 增强 Command Mode 功能 (TE/BTA/DBI)
3. **VOP2 驱动**: 确认 MCU 接口支持
4. **Encoder 层**: 添加动态切换 property
5. **Device Tree**: 配置 Panel 模式参数
