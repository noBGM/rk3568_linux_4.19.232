# 屏幕 Bringup 30 天逆袭计划

> **目标读者**：刚入职 公司 媒体的新人，有 Linux 驱动基础，零显示经验。
> **最终目标**：能独立完成一块 MIPI DSI 屏幕从 FPGA 到 SoC 的全流程 bringup，并具备 DP/eDP 的调试能力。

---

## 全局路线图

| 阶段 | 时间 | 主题 | 产出 |
|------|------|------|------|
| **第一阶段：地基** | Day 1–7 | 显示硬件原理 + DRM/KMS 框架 | 能画出完整的显示 pipeline 框图；能读懂现有 panel driver |
| **第二阶段：核心技能** | Day 8–15 | MIPI DSI 协议精读 + Panel Driver 编写 | 能独立写一个 MIPI DSI panel driver 骨架 |
| **第三阶段：实战链路** | Day 16–22 | U-Boot 显示 + FPGA 验证流程 + 完整 bringup | 掌握 FPGA 环境下点屏的完整流程 |
| **第四阶段：拓宽 + 巩固** | Day 23–30 | DP/eDP/LVDS + 调试技巧 + 体系化总结 | 具备多接口 bringup 能力；输出个人知识库 |

---

## 第一阶段：地基（Day 1–7）

### Day 1 — 显示系统全貌：从像素到人眼

**学习目标**：建立"一个像素如何从内存到达屏幕"的完整心智模型。

**核心知识点**：

1. **显示 Pipeline 概念**
   - Framebuffer → DMA → Display Controller（通常包含多个 Layer/Plane 叠加）→ Encoder（信号编码）→ Connector（物理接口）→ Panel/Monitor
   - 理解这条链路是后续所有工作的根基

2. **关键硬件模块**（对应到你将来看到的寄存器手册章节）
   - **Display Controller / CRTC**：负责从内存读取像素数据，进行合成（blending）、缩放（scaling）、颜色空间转换（CSC），最终按时序输出
   - **Encoder**：将并行 RGB 数据编码为特定协议的串行信号（如 MIPI DSI、DP、LVDS）
   - **Connector**：物理接口层，包含热插拔检测（HPD）、EDID 读取等
   - **Panel**：LCD 屏幕本身，需要特定的初始化序列（init sequence）和时序参数

3. **显示时序（Display Timing）基础**
   - 掌握以下参数的物理含义（**极其重要，每次 bringup 都会用到**）：
     - `hactive` / `vactive`：有效显示区域（分辨率）
     - `hfront_porch` / `vfront_porch`：前肩
     - `hback_porch` / `vback_porch`：后肩
     - `hsync_len` / `vsync_len`：同步脉冲宽度
     - `pixel_clock`：像素时钟频率
   - **关系公式**：`htotal = hactive + hfp + hsync + hbp`，`vtotal` 同理
   - `refresh_rate = pixel_clock / (htotal × vtotal)`
   - 理解 DE（Data Enable）模式 vs HV（Hsync/Vsync）模式的区别

4. **常见像素格式**
   - RGB888、RGB565、ARGB8888、YUV420/422/NV12
   - 了解每种格式的内存布局和使用场景（UI 通常用 ARGB，视频通常用 YUV/NV12）

**学习资源**：
- 搜索 "LCD display timing diagram" 的图片，务必能自己画出来
- 《Understanding Display Timings》（在 kernel 文档 `Documentation/gpu/` 目录下有相关说明）
- 任意一份 LCD 屏幕的 Datasheet（如 ILI9881C、HX8394），重点看 Timing Parameters 章节

**今日练习**：
- [ ] 手绘一张完整的显示 pipeline 框图（从 CPU/GPU 到屏幕）
- [ ] 手绘一张显示时序图（包含 hsync、vsync、hbp、hfp、vbp、vfp、DE）
- [ ] 给定参数：1920×1080, hfp=88, hbp=148, hsync=44, vfp=4, vbp=36, vsync=5，算出 pixel_clock（60Hz 刷新率）

---

### Day 2 — DRM/KMS 框架概览

**学习目标**：理解 Linux DRM（Direct Rendering Manager）/ KMS（Kernel Mode Setting）框架的整体架构与核心抽象。

**核心知识点**：

1. **为什么需要 DRM/KMS**
   - 历史：FBDEV → DRM/KMS 的演进
   - DRM 统一了显示和 GPU 渲染的内核接口；KMS 是其中负责"显示输出"的子系统
   - 对于你的工作，主要关注 KMS 部分（不涉及 GPU 渲染）

2. **KMS 五大核心对象**（**必须烂熟于心**）

   | 对象 | 内核结构体 | 职责 | 硬件对应 |
   |------|-----------|------|----------|
   | **Framebuffer** | `drm_framebuffer` | 描述一块图像数据的内存布局 | 显存中的一帧图像 |
   | **Plane** | `drm_plane` | 将 Framebuffer 送入合成引擎的某个图层 | Display Controller 的 Layer/Overlay |
   | **CRTC** | `drm_crtc` | 扫描输出引擎，按时序读取 Plane 数据并输出 | Display Controller 的扫描引擎 |
   | **Encoder** | `drm_encoder` | 将 CRTC 输出转换为特定协议的信号 | DSI TX、DP TX、LVDS TX |
   | **Connector** | `drm_connector` | 代表物理输出端口 | DSI 端口、DP 端口、HDMI 端口 |

   **连接关系**：`Framebuffer → Plane → CRTC → Encoder → Connector → Panel`

3. **Atomic Modesetting**
   - 现代 DRM 使用 Atomic API（`drmModeAtomicCommit`）
   - 所有模式设置（分辨率、图层配置等）打包为一个原子操作，要么全部成功，要么全部回滚
   - `TEST_ONLY` 标志可以用于预检查而不实际应用

4. **重要的 DRM Property**
   - CRTC: `ACTIVE`, `MODE_ID`
   - Connector: `CRTC_ID`, `DPMS`
   - Plane: `FB_ID`, `CRTC_ID`, `SRC_X/Y/W/H`, `CRTC_X/Y/W/H`

5. **驱动的层次结构**
   ```
   DRM Core（内核提供，drivers/gpu/drm/）
       ├── drm_drv.c          -- 设备注册、文件操作
       ├── drm_crtc.c         -- CRTC 管理
       ├── drm_plane.c        -- Plane 管理
       ├── drm_connector.c    -- Connector 管理
       ├── drm_atomic.c       -- Atomic 提交流程
       └── ...
   Vendor Driver（你要写的，如 公司_drm_drv.c）
       ├── 公司_drm_crtc.c     -- CRTC 操作回调（配置时序、使能输出等）
       ├── 公司_drm_plane.c    -- Plane 操作回调（配置 DMA 地址、格式等）
       ├── 公司_drm_dsi.c      -- DSI Encoder/Connector
       └── 公司_drm_dp.c       -- DP Encoder/Connector
   Panel Driver（drivers/gpu/drm/panel/）
       └── panel-xxx.c        -- 屏幕初始化序列 + timing 参数
   ```

**学习资源**：
- Kernel 文档：`Documentation/gpu/drm-kms.rst`
- Bootlin 的 DRM 培训幻灯片（搜索 "bootlin drm kms training"）
- `include/drm/drm_crtc.h`、`include/drm/drm_plane.h` 等头文件中的注释

**今日练习**：
- [ ] 画出 KMS 五大对象的连接关系图
- [ ] 阅读 `drivers/gpu/drm/drm_simple_kms_helper.c`，理解最简单的 KMS 驱动是什么样的
- [ ] 找到你们组已有的 vendor driver 代码，对照上面的层次结构标注出每个文件的角色

---

### Day 3 — DRM 驱动初始化流程 + Bridge 机制

**学习目标**：理解一个 DRM 驱动从 `probe()` 到画面输出的完整初始化链路；掌握 drm_bridge 的作用。

**核心知识点**：

1. **DRM 驱动初始化的标准流程**
   ```
   platform_driver.probe()
     ├── 1. drm_dev_alloc()           -- 分配 drm_device
     ├── 2. 硬件资源获取              -- clk, regmap, irq, reset, power domain
     ├── 3. drm_mode_config_init()    -- 初始化 mode_config
     │      └── 设置 min/max width/height, fb funcs 等
     ├── 4. 创建 KMS 对象
     │      ├── drm_crtc_init_with_planes()
     │      ├── drm_universal_plane_init()
     │      ├── drm_encoder_init()
     │      └── drm_connector_init() 或通过 bridge 连接
     ├── 5. drm_mode_config_reset()   -- 重置所有对象到初始状态
     ├── 6. drm_dev_register()        -- 注册设备节点 /dev/dri/cardX
     └── 7. drm_fbdev_generic_setup() -- 可选，提供 fbdev 兼容层
   ```

2. **`drm_bridge` 机制**（**在 公司 的自研 SoC 中非常重要**）
   - Bridge 是 DRM 中用于串联多个硬件模块的抽象
   - 典型场景：`CRTC → Encoder → DSI Controller(bridge) → DSI-to-eDP Bridge(bridge) → Panel`
   - 一个 Encoder 可以挂一条 bridge chain
   - 关键回调：`attach`, `mode_set`, `enable`, `disable`, `mode_fixup`
   - `drm_panel_bridge`：将一个 `drm_panel` 包装为 bridge，统一链路管理
   - 在 bringup 阶段，理解 bridge chain 的调用顺序是排查问题的关键

3. **Component 框架**
   - 大型 display subsystem 通常使用 `component` 框架来管理子设备的 probe 顺序
   - `component_master`（主设备，通常是 display controller）等待所有 `component`（CRTC、encoder 等）就绪后再执行 `bind()`
   - 你可以在 Rockchip / Mediatek 等 vendor driver 中看到这个模式

4. **Panel 驱动的位置与作用**
   - Panel driver 独立于 vendor driver，位于 `drivers/gpu/drm/panel/`
   - 负责：提供 timing 参数（`drm_display_mode`）、发送初始化命令序列、控制背光和电源
   - 通过 Device Tree 的 `panel` phandle 与 encoder/bridge 关联

**学习资源**：
- `drivers/gpu/drm/bridge/` 目录下随便看一个简单的 bridge driver（如 `ti-sn65dsi86.c`）
- `drivers/gpu/drm/panel/panel-simple.c` — 理解最基础的 panel driver 结构
- `drivers/gpu/drm/rockchip/rockchip_drm_drv.c` — component 框架的典型用法

**今日练习**：
- [ ] 画出 DRM 驱动初始化的时序图（从 probe 到第一帧显示）
- [ ] 阅读一个 bridge driver 的 `enable()`/`disable()` 回调，理解使能/关断顺序
- [ ] 理解 `drm_panel_bridge_add()` 是如何将 panel 包装成 bridge 的

---

### Day 4 — MIPI DSI 协议精读（物理层 + 链路层）

**学习目标**：掌握 MIPI DSI 协议的物理层（D-PHY）和链路层基础，能看懂示波器/逻辑分析仪抓到的 DSI 信号。

**核心知识点**：

1. **MIPI 联盟与 DSI 概述**
   - MIPI DSI（Display Serial Interface）是移动设备上最常见的显示接口
   - 典型用于：手机屏、车载小屏、嵌入式 LCD（**公司相机屏、遥控器屏就是这类**）
   - DSI 协议栈：Application Layer → Protocol Layer → Lane Management → D-PHY

2. **D-PHY 物理层**
   - **差分信号**：每条 Lane 是一对差分线（Dp/Dn）
   - **Lane 组成**：1 个 Clock Lane + 1~4 个 Data Lane（常见 1/2/4 lane）
   - **两种模式**：
     - **HS (High Speed) 模式**：高速差分信号，数百 Mbps ~ 数 Gbps per lane
     - **LP (Low Power) 模式**：低速单端信号，仅在 Data Lane 0 上传输命令
   - **数据率计算**：
     - 所需带宽 = `width × height × bpp × fps`
     - 单 Lane 速率 = 总带宽 / Lane 数（考虑编码开销，通常取 1.1~1.2 倍 margin）
   - **时序参数**（写寄存器时会遇到）：
     - `T_HS_PREPARE`, `T_HS_ZERO`, `T_HS_TRAIL`, `T_HS_EXIT`
     - `T_CLK_PREPARE`, `T_CLK_ZERO`, `T_CLK_POST`, `T_CLK_TRAIL`
     - 这些参数的合法范围定义在 MIPI D-PHY 规范中，通常由 PHY 驱动自动计算

3. **DSI 链路层**
   - **操作模式**：
     - **Command Mode**：CPU 主动写数据到屏幕内部 RAM（类似 SPI 屏），适合低分辨率小屏
     - **Video Mode**：持续流式传输像素数据，屏幕没有内部 RAM，依赖持续刷新
       - Burst Mode：高速突发传输，每行有空闲时间
       - Non-Burst Sync Pulse / Sync Event：严格按时序传输
   - **大部分嵌入式 LCD 使用 Video Mode**（公司的场景大概率也是）

4. **DSI 数据包格式**
   - **Short Packet**（4 字节）：`DI + Data0 + Data1 + ECC`
   - **Long Packet**（4 + N + 2 字节）：`DI + WC(2B) + ECC + Payload(N) + Checksum(2B)`
   - `DI (Data Identifier)` = Virtual Channel ID (2bit) + Data Type (6bit)
   - **常见 Data Type**：
     - `0x05` — DCS Short Write (无参数)
     - `0x15` — DCS Short Write (1个参数)
     - `0x39` — DCS Long Write
     - `0x06` — DCS Read
     - `0x0E` — Packed Pixel Stream, RGB565
     - `0x3E` — Packed Pixel Stream, RGB888

5. **DCS (Display Command Set) 命令**
   - MIPI 标准定义的一组通用命令（部分常用命令**必须记住**）：

   | 命令 | 代码 | 说明 |
   |------|------|------|
   | `sleep_out` | `0x11` | 退出睡眠模式（点屏必须） |
   | `display_on` | `0x29` | 开启显示（点屏必须） |
   | `display_off` | `0x28` | 关闭显示 |
   | `sleep_in` | `0x10` | 进入睡眠模式 |
   | `set_column_address` | `0x2A` | 设置列地址范围 |
   | `set_page_address` | `0x2B` | 设置行地址范围 |
   | `write_memory_start` | `0x2C` | 开始写帧数据 |
   | `set_pixel_format` | `0x3A` | 设置像素格式（如 RGB888=0x77） |
   | `set_display_brightness` | `0x51` | 设置亮度 |

   - 屏幕厂商还会定义私有命令，通过"写 Page 寄存器"切换 command page 后发送

**学习资源**：
- MIPI DSI Specification（能拿到最好，拿不到看网上的总结文章）
- 搜索 "MIPI DSI protocol tutorial" 或 "MIPI DSI for beginners"
- 任意一份 LCD Driver IC 的 Datasheet（ILI9881C、NT35596、HX8394F 等）

**今日练习**：
- [ ] 计算：720×1280 屏幕，RGB888，60fps，4 lane，所需的 D-PHY 时钟频率是多少？
- [ ] 画出 DSI Short Packet 和 Long Packet 的结构图
- [ ] 从一份 LCD datasheet 中找到初始化命令序列，能逐条解释每条命令的含义

---

### Day 5 — Linux MIPI DSI 子系统 + Panel Driver 编写

**学习目标**：掌握 Linux 内核的 MIPI DSI Host/Device 框架；能读懂和编写一个 panel driver。

**核心知识点**：

1. **Linux MIPI DSI 框架**
   - 头文件：`include/drm/drm_mipi_dsi.h`
   - **`mipi_dsi_host`**：DSI 控制器驱动注册此结构，代表 SoC 端的 DSI TX
     - 提供 `transfer()` 回调，用于收发 DSI 数据包
   - **`mipi_dsi_device`**：DSI 设备（通常是 panel），通过 DT 描述挂载在 DSI host 下
   - **`mipi_dsi_driver`**：DSI 设备的驱动（panel driver 就是一种 `mipi_dsi_driver`）

2. **DSI 消息发送 API**
   ```c
   /* 发送 DCS 命令 */
   mipi_dsi_dcs_write_seq(dsi, cmd, data...);  /* 宏，自动计算长度 */
   mipi_dsi_dcs_write_buffer(dsi, buf, len);    /* 通用发送 */

   /* 标准 DCS 命令的便捷封装 */
   mipi_dsi_dcs_exit_sleep_mode(dsi);           /* 0x11 */
   mipi_dsi_dcs_set_display_on(dsi);            /* 0x29 */
   mipi_dsi_dcs_set_display_off(dsi);           /* 0x28 */
   mipi_dsi_dcs_enter_sleep_mode(dsi);          /* 0x10 */

   /* Generic 命令（非 DCS 标准的私有命令） */
   mipi_dsi_generic_write(dsi, buf, len);
   mipi_dsi_generic_read(dsi, ...);
   ```

3. **一个典型 MIPI DSI Panel Driver 的结构**
   ```c
   struct xxx_panel {
       struct drm_panel panel;
       struct mipi_dsi_device *dsi;
       struct regulator *supply;       // 电源
       struct gpio_desc *reset_gpio;   // 复位脚
       struct backlight_device *backlight;
   };

   /* 核心回调 */
   static const struct drm_panel_funcs xxx_panel_funcs = {
       .prepare    = xxx_prepare,    // 上电 + reset + 发送 init 序列
       .enable     = xxx_enable,     // 打开背光
       .disable    = xxx_disable,    // 关闭背光
       .unprepare  = xxx_unprepare,  // 下电
       .get_modes  = xxx_get_modes,  // 返回支持的分辨率和 timing
   };
   ```

4. **prepare() 中的典型上电时序**（**bringup 的核心**）
   ```
   1. 使能电源 regulator（VCC, IOVCC）
   2. 等待电源稳定（通常 1~10ms）
   3. Reset 脚拉高 → 延时 → 拉低 → 延时 → 拉高（具体时序看 datasheet）
   4. 等待 Reset 完成（通常 10~120ms）
   5. 发送初始化命令序列（从 IC datasheet 或屏厂提供的 init code）
   6. 发送 sleep_out (0x11)，等待 120ms
   7. 发送 display_on (0x29)
   ```

5. **Device Tree 绑定**
   ```dts
   &dsi0 {
       status = "okay";

       panel@0 {
           compatible = "vendor,panel-model";
           reg = <0>;  /* DSI virtual channel */
           reset-gpios = <&gpio 42 GPIO_ACTIVE_LOW>;
           vcc-supply = <&reg_lcd_vcc>;
           backlight = <&backlight>;
       };
   };
   ```

6. **`panel-simple-dsi`**：如果屏幕不需要复杂的初始化命令（仅需标准 DCS），可以直接在 `panel-simple-dsi.c` 中添加一个 `drm_display_mode` 和 `panel_desc` 结构体

**学习资源**：
- `drivers/gpu/drm/panel/panel-boe-tv101wum-nl6.c` — 一个比较规范的 DSI panel driver 范例
- `drivers/gpu/drm/panel/panel-simple.c` — 最简单的非 DSI panel driver
- `include/drm/drm_panel.h` — panel 框架接口

**今日练习**：
- [ ] 阅读一个已有的 DSI panel driver（如 `panel-boe-tv101wum-nl6.c`），标注出 `prepare/enable/disable/unprepare` 每一步在做什么
- [ ] 根据一份假设的 LCD datasheet，尝试写一个 panel driver 骨架（不需要编译通过，重在理解结构）
- [ ] 理解 `drm_panel_funcs` 中四个回调的调用时序：`prepare → enable → ... → disable → unprepare`

---

### Day 6 — Display Controller 驱动（CRTC + Plane）

**学习目标**：理解 Display Controller 驱动的核心——CRTC 和 Plane 的实现要点。

**核心知识点**：

1. **CRTC 的核心回调（`drm_crtc_helper_funcs`）**

   | 回调 | 职责 | 典型操作 |
   |------|------|----------|
   | `atomic_check` | 校验硬件是否支持请求的配置 | 检查分辨率、刷新率、时钟范围 |
   | `atomic_begin` | 原子提交开始 | 通常无操作或关闭 vblank 中断 |
   | `atomic_flush` | 原子提交结束，触发硬件更新 | 写寄存器使新配置在下一个 vsync 生效 |
   | `atomic_enable` | 使能 CRTC | 配置时序寄存器、使能时钟、使能中断 |
   | `atomic_disable` | 关闭 CRTC | 关中断、关时钟 |
   | `mode_set_nofb` | 设置显示模式（已被 atomic 流程取代） | 配置时序参数到寄存器 |

2. **Plane 的核心回调（`drm_plane_helper_funcs`）**

   | 回调 | 职责 | 典型操作 |
   |------|------|----------|
   | `atomic_check` | 校验 plane 配置 | 检查格式支持、缩放能力、裁剪范围 |
   | `atomic_update` | 应用新的 plane 配置 | 写 DMA 地址、stride、格式、位置、缩放等寄存器 |
   | `atomic_disable` | 关闭 plane | 关闭对应 layer |

3. **Plane 类型**
   - `DRM_PLANE_TYPE_PRIMARY`：主图层，通常对应硬件的 Layer 0
   - `DRM_PLANE_TYPE_OVERLAY`：叠加图层（视频层等）
   - `DRM_PLANE_TYPE_CURSOR`：光标图层（嵌入式场景较少用）

4. **Vblank 处理**
   - CRTC 每扫描完一帧会产生 Vblank（垂直消隐）中断
   - 驱动需要在中断中调用 `drm_crtc_handle_vblank()` 通知 DRM Core
   - Atomic commit 可选 `DRM_MODE_PAGE_FLIP_ASYNC`（不等 vblank）或同步模式（等 vblank）
   - Vblank 是实现无撕裂显示的核心机制

5. **时钟树配置**
   - Display Controller 的时钟通常包括：
     - **AXI/AHB Clock**：寄存器访问和 DMA 时钟
     - **Pixel Clock**：必须与目标分辨率和刷新率匹配
   - Pixel Clock 通常来自 PLL，需要精确配置（误差 < 0.5%）
   - bringup 时时钟配不对是最常见的问题之一

**今日练习**：
- [ ] 阅读 `drivers/gpu/drm/rockchip/rockchip_drm_vop2.c`（或你们组已有的 CRTC 驱动），找到上述每个回调的实现
- [ ] 重点理解 `atomic_enable()` 中如何配置显示时序寄存器
- [ ] 理解 `atomic_flush()` 中如何触发寄存器更新（通常涉及 shadow register 和 config done 位）

---

### Day 7 — 阶段回顾 + 第一次"模拟 Bringup"

**学习目标**：整合前 6 天的知识，完成一次纸面上的"模拟 bringup"。

**复习检查清单**：

- [ ] 能否默写显示 pipeline 的完整框图？
- [ ] 能否说出 KMS 五大对象的作用和连接关系？
- [ ] 能否说出 MIPI DSI Short/Long Packet 的结构？
- [ ] 能否说出 panel driver 的 prepare/enable/disable/unprepare 每一步做什么？
- [ ] 能否计算给定分辨率和 timing 参数的 pixel_clock 和 DSI lane rate？

**模拟 Bringup 练习**：

假设你拿到一块新屏幕（MIPI DSI, 720×1280, 4lane），屏厂给了你 datasheet 和 init code。请在纸上写出：

1. **Device Tree 需要新增/修改哪些内容？**
   - DSI controller 节点的配置
   - Panel 子节点的定义（compatible, reg, reset-gpio, supply, backlight）
   - 时钟和引脚复用

2. **需要新增/修改哪些驱动文件？**
   - Panel driver 文件
   - 是否需要修改 DSI host driver？
   - Makefile / Kconfig 的修改

3. **上电后预期的调用顺序是什么？**
   - DSI host probe → Panel probe → Component bind → ...
   - CRTC enable → Encoder enable → Bridge enable → Panel prepare → Panel enable

4. **如果屏幕不亮，你的排查顺序是什么？**
   - 列出至少 10 个检查点（后续 Day 会详细学习调试方法）

**产出**：
- 一份手绘的完整知识图谱（涵盖第一周所有知识点）
- 一份"模拟 bringup"的完整操作文档

---

## 第二阶段：核心技能（Day 8–15）

### Day 8 — DSI Host Controller 驱动深入

**学习目标**：理解 SoC 端 DSI Controller 驱动的结构与关键配置，为后续看自研 SoC 的寄存器手册做准备。

**核心知识点**：

1. **DSI Host 驱动的职责**
   - 初始化 D-PHY（配置 lane 数、速率、时序参数）
   - 配置 DSI 协议层（video mode/command mode、虚拟通道、像素格式）
   - 实现 `mipi_dsi_host_ops.transfer()` —— 发送/接收 DSI 数据包的底层实现
   - 管理 DSI 时钟和电源

2. **D-PHY 配置流程**（**FPGA 阶段和 SoC 阶段都会频繁调试**）
   ```
   1. Power up PHY
   2. 配置 PLL（设置目标数据率）
   3. 配置 Lane 数
   4. 配置 HS timing parameters（T_HS_PREPARE, T_HS_ZERO, etc.）
   5. 使能 Clock Lane（进入 HS 模式）
   6. 使能 Data Lane
   7. 等待 PHY lock（PLL 锁定）
   ```

3. **Video Mode 配置**
   - 需要配置的关键参数：
     - `hactive`, `hsa`, `hbp`, `hfp`（水平方向）
     - `vactive`, `vsa`, `vbp`, `vfp`（垂直方向）
     - `burst_mode` / `non_burst_sync_pulse` / `non_burst_sync_event`
     - 像素格式（RGB888 / RGB666 / RGB565）
     - 是否 loosely packed
   - 注意：DSI 的 timing 参数单位通常是 **byte clock cycle**，需要从 pixel clock 换算

4. **Command Mode 配置**
   - 适用于小尺寸、低刷新率的屏幕（如某些 AMOLED）
   - 需要配置 TE（Tearing Effect）信号来同步
   - 数据传输由 CPU/DMA 主动发起

5. **中断处理**
   - DSI controller 的常见中断：
     - ACK error（屏幕回复错误）
     - PHY error（时序违规、contention detection）
     - Overflow/Underflow（数据流异常）
     - TE 触发（command mode）
   - bringup 时关注这些中断能帮助快速定位问题

**参考驱动**：
- `drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c` — Synopsys DesignWare DSI IP，**非常多 SoC 使用这个 IP**，很可能 公司 也使用类似架构
- `drivers/gpu/drm/samsung/exynos_drm_dsi.c` — Samsung 的 DSI 实现
- `drivers/gpu/drm/mediatek/mtk_dsi.c` — MediaTek 的 DSI 实现

**今日练习**：
- [ ] 精读 `dw-mipi-dsi.c` 的 `dw_mipi_dsi_host_transfer()` 函数，理解一个 DSI 包是怎么被发出去的
- [ ] 理解 D-PHY timing 参数的计算方法（通常有一个公式根据目标数据率算出各个时间参数）
- [ ] 画出 DSI Host 初始化的完整流程图

---

### Day 9 — 从 Datasheet 到代码：Init Sequence 实战

**学习目标**：掌握如何将屏厂提供的初始化序列转化为内核 panel driver 代码。

**核心知识点**：

1. **屏厂提供的初始化序列格式**

   屏厂通常以如下格式提供（每家格式不同，但原理一样）：

   ```
   // 以 ILI9881C 为例
   // {cmd_type, delay_ms, data_len, {data...}}
   {0x39, 0, 4, {0xFF, 0x98, 0x81, 0x03}},  // 切到 Page 3
   {0x15, 0, 2, {0x01, 0x00}},                // 写寄存器 0x01 = 0x00
   {0x15, 0, 2, {0x02, 0x00}},
   ...
   {0x39, 0, 4, {0xFF, 0x98, 0x81, 0x00}},  // 切回 Page 0
   {0x05, 120, 1, {0x11}},                    // Sleep Out, 等 120ms
   {0x05, 20, 1, {0x29}},                     // Display On, 等 20ms
   ```

2. **转化为内核代码的方法**

   **方法一：逐条翻译为 `mipi_dsi_dcs_write_seq()` 调用**
   ```c
   static int xxx_panel_init(struct xxx_panel *ctx)
   {
       struct mipi_dsi_device *dsi = ctx->dsi;

       /* Switch to Page 3 */
       mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x98, 0x81, 0x03);
       mipi_dsi_dcs_write_seq(dsi, 0x01, 0x00);
       mipi_dsi_dcs_write_seq(dsi, 0x02, 0x00);
       /* ... 更多命令 ... */

       /* Switch to Page 0 */
       mipi_dsi_dcs_write_seq(dsi, 0xFF, 0x98, 0x81, 0x00);

       mipi_dsi_dcs_exit_sleep_mode(dsi);
       msleep(120);
       mipi_dsi_dcs_set_display_on(dsi);
       msleep(20);

       return 0;
   }
   ```

   **方法二：使用 `mipi_dsi_multi` 新接口（Linux 6.x+）**
   ```c
   static int xxx_panel_init(struct xxx_panel *ctx)
   {
       struct mipi_dsi_multi_context mctx = { .dsi = ctx->dsi };

       mipi_dsi_dcs_write_seq_multi(&mctx, 0xFF, 0x98, 0x81, 0x03);
       /* ... */
       mipi_dsi_dcs_exit_sleep_mode_multi(&mctx);
       mipi_dsi_msleep(&mctx, 120);
       mipi_dsi_dcs_set_display_on_multi(&mctx);

       return mctx.accum_err;
   }
   ```

3. **常见坑点**
   - 注意区分 **DCS 命令**（`0x05/0x15/0x39`）和 **Generic 命令**（`0x03/0x13/0x29`），发送 API 不同
   - 有些屏的初始化命令需要在 **LP 模式** 下发送（`MIPI_DSI_MSG_USE_LPM` 标志）
   - 延时不能随意删减——尤其是 `sleep_out` 后的 120ms 和 `display_on` 后的延时
   - 初始化序列的顺序不能打乱，有些寄存器有写入依赖关系
   - 有时屏厂给的 init code 有错，需要对照 IC datasheet 逐条验证

4. **如何验证 init sequence 是否正确**
   - 通过 DSI read 命令读回关键寄存器（如 `0x0A` — Display Power Mode）
   - `0x0A` 返回 `0x9C` 表示正常（Display On, Sleep Out, 正常模式）
   - 如果返回异常值，说明初始化有问题

**今日练习**：
- [ ] 找一份 LCD IC 的 datasheet（网上搜 ILI9881C datasheet），将其初始化序列翻译为内核代码
- [ ] 理解 `mipi_dsi_dcs_write_seq` 宏展开后实际调用了什么
- [ ] 思考：如果屏厂给了你一份 Android 平台的 init code（xml 格式或数组格式），你怎么快速转换成 Linux panel driver 格式？

---

### Day 10 — Backlight 和 Power 管理

**学习目标**：掌握屏幕背光控制和电源管理的实现方式。

**核心知识点**：

1. **背光（Backlight）控制方式**

   | 方式 | 原理 | 适用场景 |
   |------|------|----------|
   | **PWM Backlight** | 通过 SoC 的 PWM 输出控制背光驱动芯片 | 最常见 |
   | **DSI Backlight** | 通过 DSI DCS 命令 `0x51` 设置亮度 | 部分 AMOLED |
   | **I2C Backlight** | 通过 I2C 接口控制独立的背光驱动 IC | 高端方案 |
   | **GPIO Backlight** | 简单的开/关控制 | 简单场景 |

2. **Linux Backlight 框架**
   - 头文件：`include/linux/backlight.h`
   - Panel driver 通过 `backlight_device` 与背光驱动关联
   - DT 中通过 `backlight = <&xxx_backlight>;` 引用

3. **PWM Backlight 的 DT 配置**
   ```dts
   backlight: backlight {
       compatible = "pwm-backlight";
       pwms = <&pwm0 0 25000 0>;  /* PWM 设备、通道、周期（ns）、极性 */
       brightness-levels = <0 4 8 16 32 64 128 160 200 255>;
       default-brightness-level = <7>;
       power-supply = <&reg_bl_pwr>;
   };
   ```

4. **电源管理**
   - LCD 屏幕通常需要多路电源：
     - **VCC**（通常 2.8V~3.3V）：LCD 驱动 IC 的模拟电源
     - **IOVCC**（通常 1.8V）：IO 电平
   - 上电顺序和延时由 datasheet 严格定义
   - 使用 Regulator 框架管理：`regulator_enable()` / `regulator_disable()`

5. **DPMS（Display Power Management Signaling）状态**
   - `ON` → `STANDBY` → `SUSPEND` → `OFF`
   - Panel driver 的回调映射：
     - `ON`: prepare + enable
     - `OFF`: disable + unprepare
   - Runtime PM 集成：可选但推荐

**今日练习**：
- [ ] 阅读 `drivers/video/backlight/pwm_bl.c`，理解 PWM 背光的实现
- [ ] 在你写的 panel driver 骨架中加入完整的电源管理代码（regulator + GPIO reset + 背光）
- [ ] 理解 panel 的 `prepare/enable/disable/unprepare` 与硬件上电/下电时序的对应关系

---

### Day 11 — Device Tree 与 DRM 的绑定实践

**学习目标**：能独立编写显示子系统的 Device Tree 配置。

**核心知识点**：

1. **Display 子系统的 DT 拓扑**
   ```dts
   /* SoC 级定义（dtsi） */
   display_subsystem: display-subsystem {
       compatible = "vendor,display-subsystem";
       ports = <&dc_out>;
   };

   dc: display-controller@xxxx {
       compatible = "vendor,dc";
       reg = <0x0 0xXXXXXXXX 0x0 0x10000>;
       interrupts = <GIC_SPI XX IRQ_TYPE_LEVEL_HIGH>;
       clocks = <&cru CLK_DC_AXI>, <&cru CLK_DC_PIX>;
       clock-names = "aclk", "pclk";
       power-domains = <&power RK3568_PD_VO>;
       /* ... */

       dc_out: port {
           dc_out_dsi0: endpoint {
               remote-endpoint = <&dsi0_in>;
           };
       };
   };

   dsi0: dsi@xxxx {
       compatible = "vendor,mipi-dsi";
       reg = <0x0 0xXXXXXXXX 0x0 0x1000>;
       /* ... */

       ports {
           port@0 {
               dsi0_in: endpoint {
                   remote-endpoint = <&dc_out_dsi0>;
               };
           };
           port@1 {
               dsi0_out: endpoint {
                   remote-endpoint = <&panel_in>;
               };
           };
       };
   };
   ```

   ```dts
   /* Board 级定义（dts） */
   &dsi0 {
       status = "okay";

       panel@0 {
           compatible = "vendor,xxx-panel";
           reg = <0>;
           reset-gpios = <&gpio1 RK_PA4 GPIO_ACTIVE_LOW>;
           vcc-supply = <&vcc_lcd>;
           iovcc-supply = <&vcc_lcd_io>;
           backlight = <&backlight>;

           port {
               panel_in: endpoint {
                   remote-endpoint = <&dsi0_out>;
               };
           };
       };
   };
   ```

2. **`of_graph` 端口绑定**
   - DRM 使用 `of_graph`（`port` / `endpoint` / `remote-endpoint`）描述硬件连接
   - `drm_of_find_panel_or_bridge()` 用于在 encoder 驱动中查找下游的 panel 或 bridge

3. **常见 DT 属性**
   - `rotation = <90>;` — 屏幕旋转
   - `width-mm`, `height-mm` — 物理尺寸（用于 DPI 计算）
   - `panel-init-sequence` — 某些 vendor 方案支持直接在 DT 中写 init 序列（如 Rockchip）

4. **DT binding 文档**
   - 位于 `Documentation/devicetree/bindings/display/`
   - 提交新 panel driver 时需要同时提交 `.yaml` 格式的 binding 文档

**今日练习**：
- [ ] 为你之前写的 panel driver 编写完整的 Device Tree 配置
- [ ] 理解 `of_graph` 的 port/endpoint 匹配机制
- [ ] 看一个实际的 board dts 文件中显示部分的配置（如 `arch/arm64/boot/dts/rockchip/rk3568-evb.dtsi`）

---

### Day 12 — FPGA Pre-Silicon 验证环境

**学习目标**：理解 FPGA 验证的工作流程和特殊注意事项，为 公司 的 pre-silicon 工作做准备。

**核心知识点**：

1. **什么是 Pre-Silicon 验证**
   - SoC 流片前，将 RTL 综合到 FPGA 上进行功能验证
   - 目的：在真正的芯片生产之前验证 IP 的功能正确性
   - 你的角色：验证 Display IP（DC + DSI TX + D-PHY）在 FPGA 上能否正确驱动屏幕

2. **FPGA 环境的关键差异**

   | 差异项 | FPGA | 真实 SoC |
   |--------|------|----------|
   | **时钟频率** | 低得多（10~50MHz vs 数百 MHz） | 正常频率 |
   | **D-PHY** | 通常无法综合真实 D-PHY，需要外挂 PHY 芯片或用 LVDS 替代 | 集成 D-PHY |
   | **中断** | 可能不完全可用 | 正常 |
   | **地址映射** | 可能与最终 SoC 不同 | 最终地址 |
   | **外设** | 部分不可用（如 PLL、power domain） | 完整 |
   | **稳定性** | 可能有 RTL bug | 经过验证 |

3. **FPGA 点屏的典型步骤**
   ```
   1. 获取 FPGA bitstream（IC 团队提供）
   2. 确认地址映射表（寄存器基地址等）
   3. 先用裸机代码（直接写寄存器）验证基本功能
   4. 配置时钟（FPGA 上通常用 MMCM/PLL 分频出 pixel clock）
   5. 配置 Display Controller（输出固定色彩条纹 color bar 作为测试模式）
   6. 配置 DSI TX（注意 FPGA 上的 D-PHY 可能需要特殊处理）
   7. 连接屏幕，发送 init sequence
   8. 观察屏幕是否显示 color bar
   ```

4. **FPGA 调试技巧**
   - **Color Bar 模式**：大多数 Display Controller 支持内置 color bar 输出，绕过 framebuffer 和 DMA，直接验证后端链路
   - **Loopback 测试**：DSI controller 通常支持 loopback 模式，不需要真实屏幕也能验证数据路径
   - **寄存器 dump**：频繁读回寄存器值验证配置是否正确写入
   - **ILA/ChipScope**：FPGA 上的逻辑分析仪，抓取内部信号波形

5. **裸机验证 vs Linux 驱动**
   - 在 FPGA 阶段，通常先用裸机代码（C + devmem）验证 IP 功能
   - 裸机验证通过后再移植到 Linux DRM 框架中
   - 裸机代码虽然粗糙但调试效率高——**不要一上来就写 DRM 驱动**

6. **与 IC 团队协作要点**
   - 拿到寄存器手册后先通读，标注不理解的地方
   - 发现疑似硬件 bug 时：先确认软件无误，再提供详细的复现步骤和寄存器 dump
   - FPGA 版本管理：记录每个 bitstream 对应的 RTL 版本和已知问题

**今日练习**：
- [ ] 向同事了解 公司 FPGA 验证环境的具体搭建方式
- [ ] 学习使用 `devmem2` / `busybox devmem` 命令直接读写寄存器
- [ ] 如果有 FPGA 环境，尝试读取 Display Controller 的 ID 寄存器（通常在偏移 0x00）

---

### Day 13 — 寄存器手册阅读方法论

**学习目标**：掌握高效阅读自研 SoC 寄存器手册（Register Reference Manual）的方法。

**核心知识点**：

1. **寄存器手册的结构**（以 Display Controller 为例）
   ```
   1. Overview / Block Diagram      ← 先看这个，理解整体架构
   2. Feature List                   ← 了解硬件能力边界
   3. Register Map (地址表)          ← 打印出来贴墙上
   4. Register Description (逐个寄存器)
      ├── Offset / Name
      ├── Reset Value
      ├── Bitfield Table
      │    ├── Bit range
      │    ├── Field name
      │    ├── R/W attribute
      │    └── Description
      └── Notes / Constraints
   5. Programming Guide             ← 如果有，这是金矿
   6. Timing Diagrams                ← 理解硬件行为的关键
   ```

2. **阅读优先级**
   - **第一遍（1~2小时）**：只看 Block Diagram + Feature List + Register Map，建立整体印象
   - **第二遍（按需）**：针对当前要配置的功能，精读相关寄存器
   - **不要试图一次性看完所有寄存器**——Display Controller 通常有 200~500 个寄存器

3. **Display Controller 核心寄存器分组**

   | 分组 | 典型寄存器 | 说明 |
   |------|-----------|------|
   | **全局控制** | SYS_CTRL, VERSION_ID | 使能、复位、版本号 |
   | **显示时序** | DSP_HTOTAL, DSP_VTOTAL, DSP_HACT, DSP_VACT, etc. | 配置显示时序 |
   | **Layer/Win** | WIN0_CTRL, WIN0_YRGB_MST, WIN0_ACT_INFO, WIN0_DSP_INFO | 图层控制 |
   | **DMA** | WIN0_YRGB_MST（地址）, WIN0_STRIDE（行步长） | 内存读取配置 |
   | **缩放** | WIN0_SCL_FACTOR, WIN0_SCL_OFFSET | 图层缩放 |
   | **叠加** | OVL_CTRL, ALPHA_CTRL | 多图层叠加和 Alpha 混合 |
   | **配置生效** | REG_CFG_DONE | Shadow register 更新触发 |
   | **中断** | INT_STATUS, INT_ENABLE | 中断状态和使能 |

4. **关键概念：Shadow Register**
   - 大多数 Display Controller 使用 shadow register 机制
   - 你写的配置先存在 "shadow" 区域，不会立即生效
   - 写 `REG_CFG_DONE` 后，在下一个 vsync 时 shadow 值加载到 active 寄存器
   - 这保证了多个图层的配置同时生效，避免撕裂

5. **常见的坑**
   - 某些寄存器需要先解锁才能写入
   - 某些 bitfield 是 write-1-to-clear（写 1 清除，如中断状态）
   - 大小端问题（FPGA 上尤其要注意）
   - 寄存器手册可能有错（**是的，IC 团队的文档也会有 bug**）

**今日练习**：
- [ ] 拿到你们 SoC 的 Display Controller 寄存器手册（或者用公开的 RK3568 TRM 练手）
- [ ] 用上述方法进行第一遍快速阅读，画出 Block Diagram
- [ ] 找到"配置一个图层显示纯色"需要写哪些寄存器（至少列出寄存器名和大概功能）

---

### Day 14 — DSI Controller 寄存器精读 + D-PHY 配置

**学习目标**：精读 DSI Controller 的关键寄存器，能手动计算 D-PHY 时序参数。

**核心知识点**：

1. **DSI Controller 核心寄存器**（以 Synopsys DW-DSI 为参考）

   | 寄存器组 | 说明 |
   |----------|------|
   | `DSI_VERSION` | IP 版本号 |
   | `DSI_PWR_UP` | 控制器使能/复位 |
   | `DSI_CLKMGR_CFG` | 时钟分频配置 |
   | `DSI_DPI_VCID` | 虚拟通道 ID |
   | `DSI_DPI_COLOR_CODING` | 像素格式（RGB888/RGB666/RGB565） |
   | `DSI_DPI_CFG_POL` | 信号极性（HSYNC/VSYNC/DE 极性） |
   | `DSI_DPI_LP_CMD_TIM` | LP 模式下的命令时间配置 |
   | `DSI_PCKHDL_CFG` | 包处理配置 |
   | `DSI_VID_MODE_CFG` | Video Mode 配置（burst/non-burst） |
   | `DSI_VID_PKT_SIZE` | Video Mode 一行数据的包大小 |
   | `DSI_VID_HSA_TIME` | Video Mode HSA 时间（字节时钟数） |
   | `DSI_VID_HBP_TIME` | Video Mode HBP 时间 |
   | `DSI_VID_HLINE_TIME` | Video Mode 一行总时间 |
   | `DSI_VID_VSA_LINES` | Video Mode VSA 行数 |
   | `DSI_VID_VBP_LINES` | Video Mode VBP 行数 |
   | `DSI_VID_VFP_LINES` | Video Mode VFP 行数 |
   | `DSI_VID_VACTIVE_LINES` | Video Mode 有效行数 |
   | `DSI_CMD_MODE_CFG` | Command Mode 配置 |
   | `DSI_GEN_HDR` | Generic 包头寄存器 |
   | `DSI_GEN_PLD_DATA` | Generic 包数据寄存器 |
   | `DSI_PHY_TMR_LPCLK_CFG` | Clock Lane LP 时序 |
   | `DSI_PHY_TMR_CFG` | Data Lane LP/HS 时序 |
   | `DSI_PHY_RSTZ` | PHY 复位控制 |
   | `DSI_PHY_IF_CFG` | Lane 数和停止等待时间 |
   | `DSI_PHY_STATUS` | PHY 状态（lock, stop state 等） |

2. **D-PHY 时序参数计算**

   给定目标数据率 `bitrate`（单位 Mbps/lane），byte clock = bitrate / 8：

   ```
   T_UI = 1 / bitrate（单位 ns）
   T_BYTE_CLK = 8 × T_UI

   T_HS_PREPARE: 40ns + 4×T_UI ~ 85ns + 6×T_UI
   T_HS_ZERO:    max(105ns + 6×T_UI - T_HS_PREPARE, 0)
   T_HS_TRAIL:   max(8×T_UI, 60ns + 4×T_UI)
   T_CLK_PREPARE: 38ns ~ 95ns
   T_CLK_ZERO:    max(300ns - T_CLK_PREPARE, 0)
   T_CLK_POST:    max(60ns + 52×T_UI, 0)
   T_CLK_TRAIL:   max(60ns, 0)
   ```

   - 需要将时间值换算为 byte clock 周期数写入寄存器
   - 公式：`cycles = ceil(T_ns / T_BYTE_CLK)`

3. **DSI 时序参数换算（pixel clock → byte clock）**
   ```
   byte_clock = lane_bitrate / 8
   ratio = byte_clock / pixel_clock
   dsi_hsa = hsa × ratio
   dsi_hbp = hbp × ratio
   dsi_hline = htotal × ratio
   ```

**今日练习**：
- [ ] 给定 720×1280@60Hz, 4lane, RGB888，计算所需 lane bitrate 和所有 D-PHY 时序参数
- [ ] 将 Day 1 的显示时序参数换算为 DSI byte clock 周期，填入 DSI 寄存器
- [ ] 精读 `dw-mipi-dsi.c` 中 `dw_mipi_dsi_dphy_timing()` 和 `dw_mipi_dsi_video_mode_config()` 函数

---

### Day 15 — 阶段回顾 + 独立写一个完整的 Panel Driver

**学习目标**：综合前两周所学，独立完成一个可编译的 MIPI DSI panel driver。

**产出要求**：

1. **一个完整的 panel driver 源文件**，包含：
   - `struct xxx_panel` 定义
   - `probe()` / `remove()` 函数
   - `prepare()` / `enable()` / `disable()` / `unprepare()` 回调
   - `get_modes()` 回调
   - 完整的初始化序列（可以从网上找一份 ILI9881C 的 init code）
   - Regulator / GPIO / Backlight 管理
   - `mipi_dsi_driver` 注册

2. **对应的 Device Tree 配置**

3. **对应的 Kconfig / Makefile 修改**

**自检清单**：

- [ ] `prepare()` 中上电顺序是否与 datasheet 一致？
- [ ] reset 时序是否正确（高→低→高，各段延时正确）？
- [ ] init sequence 中是否设置了正确的 `dsi->mode_flags`（`MIPI_DSI_MODE_VIDEO` 等）？
- [ ] `get_modes()` 中的 timing 参数是否与 datasheet 一致？
- [ ] `unprepare()` 中下电顺序是否与上电顺序相反？
- [ ] 是否处理了错误返回值？
- [ ] 是否正确设置了 `dsi->lanes` 和 `dsi->format`？

---

## 第三阶段：实战链路（Day 16–22）

### Day 16 — U-Boot 显示框架

**学习目标**：理解 U-Boot 中的显示驱动架构，为 "开机 Logo" 和 "Seamless Display" 做准备。

**核心知识点**：

1. **U-Boot 显示的必要性**
   - **开机 Logo**：用户按下电源键后第一时间看到画面，提升体验
   - **Seamless Display**：U-Boot 点亮屏幕 → 内核接管时不闪屏、不黑屏
   - 对于 公司 的产品（相机、遥控器），这是用户体验的关键

2. **U-Boot DM（Driver Model）中的显示**
   - U-Boot 有自己的驱动模型，类似 Linux 但更简化
   - 关键 UCLASS：
     - `UCLASS_VIDEO`：视频设备（对应 display controller）
     - `UCLASS_VIDEO_BRIDGE`：视频桥（如 DSI-to-eDP bridge）
     - `UCLASS_PANEL`：面板设备
     - `UCLASS_DSI_HOST`：DSI 控制器
     - `UCLASS_BACKLIGHT`：背光

3. **U-Boot 显示驱动结构**
   ```
   video-uclass.c         ← 顶层 video 框架
     ├── xxx_dc.c          ← Display Controller 驱动
     ├── xxx_dsi.c          ← DSI Host 驱动
     └── xxx_panel.c        ← Panel 驱动（或复用 panel-simple）
   ```

4. **U-Boot 点屏的关键流程**
   ```
   1. Video probe（分配 Framebuffer）
   2. 配置 Display Controller（时序、格式）
   3. 初始化 DSI Host + D-PHY
   4. 发送 Panel init sequence
   5. 将 Logo BMP/PNG 解码到 Framebuffer
   6. Enable 输出
   ```

5. **Seamless Display 实现要点**
   - U-Boot 配置好所有显示硬件后，将 Framebuffer 地址和显示参数通过 DT 或 command line 传递给 Kernel
   - Kernel DRM 驱动在初始化时检测到硬件已经在运行，**跳过重新初始化**
   - 需要精心处理：
     - 同一块 Framebuffer 内存不能被 kernel 回收
     - DRM 驱动的 `atomic_enable` 需要检查 CRTC 是否已经在运行
     - 中间不能有时钟重配或 PHY 重置

6. **U-Boot 与 Linux Panel Driver 的代码复用**
   - 很多 init sequence 可以在 U-Boot 和 Linux 之间共享
   - 有些方案将 init sequence 放在 DT 中，U-Boot 和 Linux 共用同一份 DT

**学习资源**：
- U-Boot 源码 `drivers/video/` 目录
- Rockchip U-Boot 的显示实现（如果你们用 RK3568 作为参考）

**今日练习**：
- [ ] 浏览 U-Boot 源码中 `drivers/video/rockchip/` 目录的结构
- [ ] 理解 `UCLASS_VIDEO` 的 probe 流程
- [ ] 思考 Seamless Display 的实现方案：U-Boot 和 Kernel 之间需要传递哪些信息？

---

### Day 17 — U-Boot DSI + Panel 驱动实践

**学习目标**：能在 U-Boot 中编写和调试 DSI panel 驱动。

**核心知识点**：

1. **U-Boot Panel Driver 结构**（比 Linux 简单很多）
   ```c
   static int xxx_panel_enable_backlight(struct udevice *dev)
   {
       struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
       struct mipi_dsi_device *dsi = plat->device;

       /* 发送 init sequence */
       /* ... */

       mipi_dsi_dcs_exit_sleep_mode(dsi);
       mdelay(120);
       mipi_dsi_dcs_set_display_on(dsi);

       return 0;
   }

   static int xxx_panel_get_display_timing(struct udevice *dev,
                                           struct display_timing *timing)
   {
       /* 填入 timing 参数 */
       timing->hactive.typ = 720;
       timing->vactive.typ = 1280;
       /* ... */
       return 0;
   }

   static const struct panel_ops xxx_panel_ops = {
       .enable_backlight = xxx_panel_enable_backlight,
       .get_display_timing = xxx_panel_get_display_timing,
   };

   U_BOOT_DRIVER(xxx_panel) = {
       .name = "xxx_panel",
       .id = UCLASS_PANEL,
       .ops = &xxx_panel_ops,
       /* ... */
   };
   ```

2. **U-Boot 中发送 DSI 命令**
   - 接口与 Linux 类似：`mipi_dsi_dcs_write_buffer()`, `mipi_dsi_dcs_read()`
   - 但错误处理更简单，通常直接返回错误码

3. **U-Boot 调试手段**
   - `md` / `mw` 命令：直接读写内存/寄存器
   - `log` 系统：`log_debug()`, `log_err()`
   - BMP 显示测试：`bmp display ${loadaddr}`
   - `dm tree` 命令：查看设备模型树

**今日练习**：
- [ ] 参照 U-Boot 中已有的 panel driver，为你 Day 15 写的 panel 编写 U-Boot 版本
- [ ] 理解 U-Boot video probe 的完整流程（从 `video_post_probe()` 开始追代码）
- [ ] 思考如何在 U-Boot 和 Linux 之间共享 panel init sequence 代码

---

### Day 18 — 完整 Bringup 流程：从上电到画面

**学习目标**：以 MIPI DSI 屏幕为例，掌握从零开始 bringup 的完整流程和检查点。

**完整 Bringup 流程图**：

```
┌─────────────────────────────────────────────────────────────┐
│                     硬件准备                                  │
│  1. 确认硬件连接（FPC 排线、电源、背光）                        │
│  2. 万用表测量电源电压是否正确                                  │
│  3. 确认 GPIO 引脚定义（reset, TE, backlight enable）          │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                  裸机/最小化验证                               │
│  4. 用 devmem 配置 Display Controller，输出 color bar         │
│  5. 用 devmem 配置 DSI TX + D-PHY                            │
│  6. 用 devmem 发送 panel init sequence                       │
│  7. 检查屏幕是否显示 color bar                                │
│     → 成功：硬件链路OK，进入驱动开发                            │
│     → 失败：进入调试流程（见 Day 19）                           │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                  Linux DRM 驱动集成                            │
│  8. 编写/适配 Panel Driver                                    │
│  9. 编写/适配 DSI Host Driver（如果是新 IP）                    │
│  10. 适配 Display Controller Driver（CRTC + Plane）           │
│  11. 编写 Device Tree                                         │
│  12. 编译 + 烧录 + 启动                                       │
│  13. 用 modetest 工具测试显示                                  │
└──────────────────────────┬──────────────────────────────────┘
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                     功能验证                                   │
│  14. 分辨率/刷新率正确性                                       │
│  15. 颜色正确性（R/G/B 是否反了）                               │
│  16. 图层叠加功能                                              │
│  17. 背光调节                                                  │
│  18. 休眠/唤醒                                                │
│  19. 性能测试（帧率、延迟）                                     │
└─────────────────────────────────────────────────────────────┘
```

**关键命令和工具**：

```bash
# modetest：DRM 测试工具（libdrm 提供）
modetest -M <driver_name> -c           # 列出 connectors
modetest -M <driver_name> -p           # 列出 planes 和 properties
modetest -M <driver_name> -s <conn>:<mode>  # 设置模式并显示测试图案

# 示例：在 connector 31 上以 720x1280 显示 SMPTE color bar
modetest -M rockchip -s 31:720x1280 -P 35@31:720x1280

# 读取 EDID（用于 DP/HDMI）
cat /sys/class/drm/card0-DSI-1/edid | edid-decode

# 查看当前显示状态
cat /sys/kernel/debug/dri/0/state

# 读取 panel 寄存器（如果驱动支持）
cat /sys/kernel/debug/dri/0/DSI-1/panel_regs
```

**今日练习**：
- [ ] 按照上面的流程图，为你的目标平台列出每一步需要的具体操作
- [ ] 在开发板上运行 `modetest`，熟悉其输出和用法
- [ ] 用 `modetest` 的 `-P` 参数测试不同像素格式（ARGB8888、RGB565、NV12）

---

### Day 19 — 调试方法论：屏幕不亮怎么办

**学习目标**：掌握系统化的显示调试方法，能在屏幕不亮时快速定位问题。

**"屏幕不亮"排查决策树**：

```
屏幕不亮
├── 1. 检查电源
│   ├── 用万用表测量 VCC/IOVCC 电压是否在规范范围内
│   └── regulator 是否 enable？→ cat /sys/class/regulator/
│
├── 2. 检查 Reset
│   ├── GPIO 是否配置正确？→ cat /sys/kernel/debug/gpio
│   └── Reset 时序是否正确？（示波器/逻辑分析仪）
│
├── 3. 检查 Clock
│   ├── Pixel Clock 频率是否正确？→ cat /sys/kernel/debug/clk/clk_summary
│   └── DSI byte clock 是否正确？
│
├── 4. 检查 D-PHY
│   ├── PHY 是否 lock？→ 读 DSI_PHY_STATUS 寄存器
│   ├── Lane 是否进入 HS 模式？→ 示波器看差分信号
│   └── Clock Lane 是否持续输出？
│
├── 5. 检查 DSI Init Sequence
│   ├── 命令是否发送成功？→ 检查 transfer() 返回值
│   ├── 尝试读 panel ID (0xDA/0xDB/0xDC) 或 power mode (0x0A)
│   └── 如果读回全 0 或全 FF → 通信链路有问题（回到 4）
│
├── 6. 检查 Display Controller
│   ├── CRTC 是否 enable？→ 读相关寄存器
│   ├── Framebuffer 地址是否正确？→ 读 DMA 地址寄存器
│   ├── 时序参数是否正确？→ 读 timing 寄存器
│   └── 是否有 underrun/overflow 中断？
│
├── 7. 检查信号质量
│   ├── 用示波器看 MIPI 信号眼图
│   └── 信号质量差 → 调整 D-PHY 驱动强度或时序参数
│
└── 8. 其他
    ├── 屏幕是否需要特定的 TE 信号？
    ├── 背光是否亮？（有些屏有背光但无图像 = 白屏或灰屏）
    └── 检查是否有 connector/encoder/crtc 的绑定错误 → dmesg
```

**常见问题及解决方法**：

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| 全黑（背光也不亮） | 电源或背光未开 | 测量电压，检查 PWM 输出 |
| 白屏/灰屏 | Init sequence 未发送或有误 | 读 0x0A，检查 DSI 传输日志 |
| 花屏 | D-PHY timing 不对 / 格式不匹配 | 调整 PHY 参数，确认 RGB888 vs RGB666 |
| 偏色（R/B 反了） | RGB 顺序配置错误 | 修改 color coding 寄存器 |
| 图像偏移/错位 | 显示时序参数不对 | 对照 datasheet 核实 porch 参数 |
| 闪烁/撕裂 | Vsync 同步问题 | 检查 vblank 中断处理 |
| 上半部分正常下半部分花 | 带宽不足 / DMA underrun | 检查时钟频率，增大 FIFO threshold |
| 间歇性闪烁 | D-PHY 信号质量 | 示波器看眼图 |

**调试工具一览**：

| 工具 | 用途 |
|------|------|
| `devmem` / `io` | 直接读写寄存器 |
| `modetest` | DRM 功能测试 |
| `dmesg` | 内核日志 |
| `/sys/kernel/debug/dri/` | DRM debug 信息 |
| `/sys/kernel/debug/clk/` | 时钟树信息 |
| `/sys/kernel/debug/gpio` | GPIO 状态 |
| `/sys/kernel/debug/regulator/` | 电源状态 |
| 示波器 + MIPI 探头 | 信号质量分析 |
| 逻辑分析仪 | 低速信号时序分析 |

**今日练习**：
- [ ] 将上面的排查决策树打印出来贴在工位上
- [ ] 在开发板上故意制造几种错误（如修改 timing 参数、关掉 PHY），观察屏幕表现
- [ ] 练习使用 `/sys/kernel/debug/dri/0/state` 查看当前 DRM 状态

---

### Day 20 — DRM 调试工具与内核调试技巧

**学习目标**：掌握 DRM 子系统特有的调试方法和工具。

**核心知识点**：

1. **DRM Debug 日志**
   ```bash
   # 开启 DRM debug 日志
   echo 0x1f > /sys/module/drm/parameters/debug
   # 各 bit 含义：
   # 0x01 - CORE
   # 0x02 - DRIVER
   # 0x04 - KMS
   # 0x08 - PRIME
   # 0x10 - ATOMIC
   # 0x1f - 全部打开

   # 查看日志
   dmesg | grep drm
   ```

2. **DRM debugfs**
   ```bash
   # 查看所有 DRM 对象状态
   cat /sys/kernel/debug/dri/0/state

   # 输出内容包括：
   # - 每个 plane 的状态（fb_id, src/crtc rect, format 等）
   # - 每个 CRTC 的状态（active, mode, gamma 等）
   # - 每个 connector 的状态（status, encoder, DPMS 等）
   ```

3. **ftrace 追踪显示流程**
   ```bash
   # 追踪 atomic commit 流程
   echo 1 > /sys/kernel/debug/tracing/events/drm/drm_vblank_event/enable
   echo 1 > /sys/kernel/debug/tracing/events/dma_fence/enable
   cat /sys/kernel/debug/tracing/trace_pipe
   ```

4. **常用的内核调试宏（在驱动代码中使用）**
   ```c
   #include <drm/drm_print.h>

   drm_dbg_kms(drm, "format: %p4cc\n", &format);  /* KMS 调试信息 */
   drm_info(drm, "display connected\n");
   drm_err(drm, "failed to enable: %d\n", ret);
   drm_WARN_ON(drm, condition);                     /* 条件不满足时警告 */
   ```

5. **用户空间调试工具**
   ```bash
   # libdrm 工具集
   modetest       # 模式设置测试
   proptest       # property 读写测试
   drmdevice      # 设备信息

   # Wayland/Weston 相关
   weston-simple-egl   # 简单 EGL 渲染测试
   weston-info         # 查看 Weston 输出信息

   # 截屏（直接从 DRM framebuffer 读取）
   cat /dev/fb0 > /tmp/screen.raw
   # 或者
   drm-screenshot      # 如果有安装
   ```

6. **性能分析**
   ```bash
   # 帧率监控
   cat /sys/kernel/debug/dri/0/crtc-0/fps

   # vblank 计数
   cat /sys/kernel/debug/dri/0/vblank

   # GPU/Display 占用
   cat /sys/kernel/debug/dri/0/state | grep -A5 "plane"
   ```

**今日练习**：
- [ ] 在开发板上开启 DRM debug（`echo 0x1f > /sys/module/drm/parameters/debug`），观察一次 modeset 的完整日志
- [ ] 使用 `cat /sys/kernel/debug/dri/0/state` 解读每个字段的含义
- [ ] 使用 `modetest` 的 `-w` 参数修改一个 property（如 brightness），观察效果

---

### Day 21 — 多接口基础：DP (DisplayPort) 入门

**学习目标**：了解 DP 协议的核心概念，与 MIPI DSI 做对比理解。

**核心知识点**：

1. **DP vs MIPI DSI 对比**

   | 特性 | MIPI DSI | DisplayPort |
   |------|----------|-------------|
   | **应用场景** | 移动/嵌入式设备 | 桌面/笔记本/嵌入式 |
   | **物理层** | D-PHY（差分对） | Main Link（1/2/4 lane）+ AUX CH |
   | **速率** | 1~2.5 Gbps/lane | 1.62/2.7/5.4/8.1 Gbps/lane (HBR/HBR2/HBR3) |
   | **热插拔** | 无（固定连接） | 有（HPD） |
   | **EDID** | 无（固定 panel） | 有（通过 AUX 读取 DPCD/EDID） |
   | **链路训练** | 无 | 有（Clock Recovery + Channel EQ） |
   | **音频** | 无 | 支持 |

2. **DP 协议栈**
   ```
   Application Layer
     ├── Video Stream（像素数据）
     ├── Audio Stream
     └── Secondary Data
   Link Layer
     ├── Main Link（高速数据传输，1/2/4 lane）
     ├── AUX Channel（低速双向通道，1 Mbps）
     │    ├── DPCD 读写（DisplayPort Configuration Data）
     │    ├── EDID 读取
     │    └── Link Training 控制
     └── HPD（Hot Plug Detect，热插拔检测）
   Physical Layer
     └── 8b/10b 或 128b/132b 编码
   ```

3. **Link Training（链路训练）**（**DP 特有的核心概念**）
   - DP Source（SoC）和 Sink（显示器）之间需要协商最佳传输参数
   - **阶段 1 — Clock Recovery**：调整电压摆幅（Voltage Swing）和预加重（Pre-emphasis），直到 Sink 能锁定时钟
   - **阶段 2 — Channel Equalization**：调整均衡参数，直到 Sink 确认所有 lane 的信号质量达标
   - 通过 AUX Channel 读写 DPCD 寄存器来完成协商
   - 如果链路训练失败 → 降速降 lane 重试

4. **DPCD（DisplayPort Configuration Data）**
   - Sink 设备中的一组寄存器，通过 AUX Channel 读写
   - 关键地址：
     - `0x00000` — DPCD Rev
     - `0x00001` — Max Link Rate
     - `0x00002` — Max Lane Count
     - `0x00100` — Link BW Set（设置链路速率）
     - `0x00101` — Lane Count Set（设置 lane 数）
     - `0x00200~0x0020x` — Link Training 状态

5. **eDP（Embedded DisplayPort）**
   - DP 的嵌入式变体，用于笔记本内屏
   - 与 DP 的区别：
     - 固定连接（无热插拔）
     - 支持 PSR（Panel Self Refresh，省电）
     - 支持 backlight 控制（通过 AUX 或 PWM）
     - 通常不需要 link training（可选 fast link training）
   - 公司 遥控器的大屏可能使用 eDP

**学习资源**：
- VESA DisplayPort Standard（公开的 overview 部分）
- `drivers/gpu/drm/dp/` — 内核 DP helper 代码
- `drivers/gpu/drm/bridge/analogix/` — Analogix DP bridge 驱动（常见方案）

**今日练习**：
- [ ] 阅读 `include/drm/display/drm_dp_helper.h` 中 DPCD 寄存器定义
- [ ] 理解 Link Training 的两个阶段，画出流程图
- [ ] 对比 MIPI DSI 和 DP 的 bringup 流程差异

---

### Day 22 — DP/eDP 驱动结构 + LVDS 简介

**学习目标**：了解 DP/eDP 驱动的结构；快速入门 LVDS 接口。

**核心知识点**：

1. **DP 驱动的核心组件**
   ```
   DP Controller Driver (dp_core.c)
     ├── AUX Channel 实现 (drm_dp_aux)
     │    └── dpcd_read / dpcd_write
     ├── Link Training 引擎
     │    └── drm_dp_link_train_clock_recovery()
     │    └── drm_dp_link_train_channel_eq()
     ├── HPD 中断处理
     │    └── connector status change → userspace notification
     └── Video 配置
          └── 根据 EDID 和链路能力配置分辨率
   ```

2. **Linux DRM DP Helper**
   - `drm_dp_aux`：AUX Channel 的抽象
   - `drm_dp_dpcd_read()` / `drm_dp_dpcd_write()`：DPCD 读写
   - `drm_dp_link_train_init()` / `drm_dp_link_train()`：链路训练
   - `drm_edid_read()`：通过 DDC 读取 EDID

3. **eDP 特殊处理**
   - eDP 面板通常有一个 `drm_panel` 关联
   - 上电时序：Panel Power On → AUX ready → Link Training → Video On
   - 下电时序相反
   - eDP 的 Panel Power Sequencing 有严格的时序要求（T1~T12，定义在 eDP spec 中）

4. **LVDS（Low Voltage Differential Signaling）**
   - 较老但仍在使用的显示接口
   - 特点：
     - 多对差分信号并行传输（通常 4 对数据 + 1 对时钟）
     - 速率较低（通常 < 1Gbps 总带宽）
     - 无需初始化命令（纯硬件时序）
     - 通常用于工业/车载显示
   - Linux 驱动中通常实现为一个简单的 encoder + panel-simple

5. **接口选择指南**（帮助你理解 公司 不同产品的技术选型）

   | 接口 | 分辨率范围 | 典型应用 | bringup 复杂度 |
   |------|-----------|----------|---------------|
   | MIPI DSI | QVGA~FHD+ | 手机屏、小屏 | 中（需 init sequence） |
   | eDP | HD~4K | 笔记本屏、中大屏 | 中高（需 link training） |
   | DP | HD~8K | 外接显示器 | 高（需 HPD + link training） |
   | LVDS | VGA~WXGA | 工业屏 | 低（纯时序） |

**今日练习**：
- [ ] 阅读 `drivers/gpu/drm/bridge/analogix/analogix_dp_core.c` 中链路训练的实现
- [ ] 理解 eDP Panel Power Sequence 的时序要求
- [ ] 列出 MIPI DSI / DP / eDP / LVDS 四种接口 bringup 的异同点

---

## 第四阶段：拓宽 + 巩固（Day 23–30）

### Day 23 — 颜色管理与图像质量

**学习目标**：理解显示系统中的颜色管理，能处理偏色、色带等图像质量问题。

**核心知识点**：

1. **颜色空间**
   - **sRGB**：标准 RGB 颜色空间，绝大多数 LCD 使用
   - **BT.601 / BT.709 / BT.2020**：视频标准的颜色空间
   - **YUV ↔ RGB 转换**：Display Controller 中的 CSC（Color Space Conversion）模块完成
   - CSC 矩阵的配置在 bringup 时通常使用默认值，后期调优

2. **Gamma 校正**
   - LCD 面板的亮度响应不是线性的
   - Gamma LUT（Look-Up Table）用于补偿非线性
   - DRM 中通过 CRTC 的 `GAMMA_LUT` property 设置
   - 典型值：gamma 2.2（sRGB 标准）

3. **像素格式与内存布局**
   ```
   ARGB8888: [A7..A0][R7..R0][G7..G0][B7..B0] - 4 bytes/pixel
   RGB888:   [R7..R0][G7..G0][B7..B0]          - 3 bytes/pixel
   RGB565:   [R4..R0][G5..G0][B4..B0]          - 2 bytes/pixel
   NV12:     Y plane + interleaved UV plane     - 1.5 bytes/pixel
   ```
   - stride = width × bytes_per_pixel（需要对齐，通常 64/128 字节对齐）

4. **常见图像质量问题**

   | 问题 | 原因 | 解决方法 |
   |------|------|----------|
   | R/B 反了 | RGB 顺序配置错误 | 修改 DPI color coding 或 Plane 格式 |
   | 色带（banding） | 位深不够（如 RGB666 当 RGB888 用） | 检查 color coding，开启 dithering |
   | 亮度不均 | 背光设计问题 | 非驱动范畴，反馈硬件 |
   | 偏黄/偏蓝 | Gamma 或 CSC 不对 | 校准 Gamma LUT |
   | 暗部细节丢失 | Gamma 曲线不合适 | 调整 Gamma |

**今日练习**：
- [ ] 理解 DRM Plane 支持的像素格式列表（`drm_fourcc.h`）
- [ ] 在开发板上用 `modetest` 测试不同像素格式，观察颜色差异
- [ ] 了解你们 SoC 的 Display Controller 是否支持 Gamma LUT 和 CSC

---

### Day 24 — 休眠/唤醒（Suspend/Resume）

**学习目标**：掌握显示驱动在系统休眠/唤醒流程中的行为。

**核心知识点**：

1. **显示驱动在 Suspend 中需要做的事**
   ```
   System Suspend
     ├── drm_mode_config_helper_suspend()
     │    ├── 保存当前 atomic state
     │    ├── 执行 atomic disable:
     │    │    ├── Plane disable
     │    │    ├── CRTC disable
     │    │    │    ├── 关中断
     │    │    │    └── 关时钟
     │    │    └── Encoder/Bridge disable
     │    │         ├── Panel disable + unprepare
     │    │         ├── DSI TX disable
     │    │         └── D-PHY power down
     │    └── 关闭 Power Domain
     └── done
   ```

2. **Resume 中需要做的事**
   ```
   System Resume
     ├── 恢复 Power Domain
     ├── drm_mode_config_helper_resume()
     │    ├── 用保存的 state 执行 atomic enable:
     │    │    ├── CRTC enable（重新配置时序、时钟）
     │    │    ├── Encoder/Bridge enable
     │    │    │    ├── D-PHY power up
     │    │    │    ├── DSI TX enable
     │    │    │    └── Panel prepare + enable
     │    │    └── Plane update
     │    └── 恢复 fbdev console
     └── done
   ```

3. **常见 Suspend/Resume 问题**
   - 唤醒后黑屏：Panel 未正确重新初始化（init sequence 需要重新发送）
   - 唤醒后花屏：时钟未正确恢复，或 DMA 地址丢失
   - 唤醒慢：init sequence 中的延时太长
   - Power Domain 依赖：确保 display 的 power domain 在 resume 时先于设备恢复

4. **Runtime PM**
   - 在屏幕空闲时自动关闭 Display Controller 以省电
   - 通过 `pm_runtime_get_sync()` / `pm_runtime_put()` 管理
   - 需要在 CRTC enable/disable 中正确调用

**今日练习**：
- [ ] 阅读 `drm_mode_config_helper_suspend/resume()` 的实现
- [ ] 在开发板上测试 `echo mem > /sys/power/state` 后显示是否正常恢复
- [ ] 如果唤醒后异常，使用 Day 19 的排查方法定位问题

---

### Day 25 — 多屏显示与屏幕旋转

**学习目标**：了解多屏（多 connector）场景和屏幕旋转的实现。

**核心知识点**：

1. **多屏显示架构**
   - 一个 SoC 可能有多个 Display Controller 或多个输出端口
   - 典型场景：
     - **公司 遥控器**：主屏（eDP/DSI）+ 外接 HDMI（给飞行员看）
     - **公司 相机**：机身小屏（DSI）+ HDMI 输出（给导演监看）
   - 每个 CRTC 独立输出，可以显示不同内容（clone 模式或扩展模式）

2. **DRM 中的多屏管理**
   - 多个 CRTC 共享同一个 `drm_device`
   - 每个 CRTC 可以绑定不同的 Connector
   - Atomic commit 可以同时更新多个 CRTC
   - 用户空间通过 `drmModeSetCrtc()` 或 atomic API 分别配置

3. **屏幕旋转**
   - 硬件旋转：Display Controller 的某些 Plane 支持硬件旋转（0°/90°/180°/270°）
     - 通过 Plane 的 `rotation` property 设置
     - `DRM_MODE_ROTATE_0/90/180/270`
   - 软件旋转：如果硬件不支持，需要在渲染侧完成
   - DT 中 `rotation = <90>;` 可以设置 panel 的物理安装方向

4. **CRTC 资源分配**
   - 当 CRTC 数量少于 Connector 数量时，需要仲裁
   - `drm_crtc_helper_funcs.atomic_check()` 中需要处理资源冲突
   - `possible_crtcs` bitmask 定义了哪些 CRTC 可以驱动哪些 Encoder

**今日练习**：
- [ ] 查看你们 SoC 支持多少个 CRTC 和 Connector
- [ ] 用 `modetest` 列出所有 CRTC 和 Connector 的映射关系
- [ ] 如果有多屏，尝试同时在两个屏幕上显示不同内容

---

### Day 26 — Seamless Display 深入 + Splash Screen

**学习目标**：深入理解从 U-Boot 到 Kernel 的无缝显示技术。

**核心知识点**：

1. **Seamless Display 的完整链路**
   ```
   ┌─────────┐     ┌──────────┐     ┌──────────┐
   │ U-Boot  │────→│  Kernel  │────→│Userspace │
   │ (Logo)  │     │(DRM接管) │     │(UI框架)  │
   └─────────┘     └──────────┘     └──────────┘
      显示 Logo     保持画面不灭      替换为 UI
   ```

2. **实现方式一：Kernel 检测并跳过初始化**
   - U-Boot 点屏后，在 DT chosen 节点传递：
     ```dts
     chosen {
         framebuffer {
             compatible = "simple-framebuffer";
             reg = <0x0 0x7F000000 0x0 0x800000>; /* FB 地址和大小 */
             width = <720>;
             height = <1280>;
             stride = <2880>; /* 720 * 4 */
             format = "a8r8g8b8";
         };
     };
     ```
   - Kernel 的 `simple-framebuffer` 驱动在早期接管显示
   - DRM 驱动初始化时检测到 CRTC 已经在运行，跳过 disable/re-enable

3. **实现方式二：DRM 的 "fastboot" / "no initial modeset"**
   - 部分 DRM 驱动支持继承 U-Boot 的显示状态
   - `drm_atomic_helper_commit_duplicated_state()` 用于将硬件当前状态同步到 DRM 软件状态
   - 关键：kernel 启动时**不要 reset** display controller 和 DSI TX

4. **实现难点**
   - Framebuffer 内存保留：需要在 DT 中用 `reserved-memory` 标记，防止 kernel 回收
   - 时钟和 Power Domain：kernel 的 clk 框架和 PM domain 框架可能在 boot 时关闭 "unused" 的时钟
     - 使用 `CLK_IS_CRITICAL` 标志或在驱动 probe 之前 grab 住时钟
   - 中断处理：kernel 注册中断处理后，可能收到 U-Boot 阶段遗留的中断

5. **Splash Screen 方案**
   - **方案 A**：U-Boot 显示 Logo → Kernel simple-framebuffer 保持 → Userspace 替换
   - **方案 B**：U-Boot 显示 Logo → Kernel DRM 驱动直接接管 → 显示启动动画
   - **方案 C**：使用 Plymouth 或自定义的 boot splash 程序

**今日练习**：
- [ ] 阅读 `drivers/video/fbdev/simplefb.c` 的实现
- [ ] 理解 `reserved-memory` 在 DT 中的用法
- [ ] 设计一份 公司 产品的 Seamless Display 方案文档（从按下电源到 UI 显示）

---

### Day 27 — IC 团队协作与硬件 Bug 定位

**学习目标**：掌握与 IC 设计团队协作的方法，学会定位和报告硬件 Bug。

**核心知识点**：

1. **如何高效与 IC 团队沟通**
   - **说硬件的语言**：用寄存器地址和位域名称，而不是"驱动代码第 XX 行"
   - **提供完整的复现信息**：
     ```
     1. 使用的 bitstream/芯片版本
     2. 完整的寄存器配置序列（按时间顺序列出每次写操作）
     3. 异常时的寄存器 dump（所有相关模块）
     4. 波形截图（如果有示波器数据）
     5. 复现概率（必现 / 概率性）
     ```
   - **区分软件问题和硬件问题**：先排除软件因素，再报告硬件 Bug

2. **常见的硬件 Bug 类型**
   - **寄存器行为不符合手册描述**：某个 bit 写入后读回值不对
   - **时序违规**：某些配置下 D-PHY 输出不符合 MIPI spec
   - **功能缺陷**：某个 feature 在特定条件下不工作
   - **性能问题**：DMA 带宽不够、FIFO 深度不足
   - **Power Domain 问题**：某个模块关电后状态未正确保存/恢复

3. **Bug Report 模板**
   ```markdown
   ## 问题标题
   [简短描述]

   ## 环境
   - FPGA bitstream 版本 / 芯片 stepping
   - 软件版本
   - 使用的屏幕型号

   ## 复现步骤
   1. 写寄存器 A (offset 0xXX) = 0xYY
   2. 写寄存器 B (offset 0xXX) = 0xYY
   3. ...

   ## 预期行为
   根据寄存器手册第 X 页，预期应该 ...

   ## 实际行为
   实际观察到 ...

   ## 附件
   - 寄存器 dump
   - 波形截图
   - 内核日志
   ```

4. **Workaround 管理**
   - 硬件 Bug 修复需要等下一版 RTL / 下一次 tapeout，软件需要先做 workaround
   - **关键原则**：
     - 每个 workaround 用 `#ifdef` 或 chip version check 隔离
     - 详细注释 workaround 的原因、对应的 bug ID
     - 在代码中标记 `/* TODO: remove after chip revision XX */`

**今日练习**：
- [ ] 为一个假设的硬件 Bug 编写一份完整的 Bug Report
- [ ] 向 IC 同事请教他们的 RTL 仿真环境和验证流程
- [ ] 了解你们公司的 Bug Tracking 系统和流程

---

### Day 28 — 性能优化与功耗管理

**学习目标**：了解显示驱动的性能优化和功耗管理策略。

**核心知识点**：

1. **显示带宽优化**
   - Display Controller 的 DMA 需要从 DDR 读取 Framebuffer 数据
   - 带宽计算：`BW = width × height × bpp × fps × num_layers`
   - 优化手段：
     - 使用压缩格式（AFBC、UBWC 等）减少带宽
     - 合理配置 Plane 数量，避免不必要的叠加
     - 使用 NV12 代替 ARGB8888（视频场景）

2. **帧率控制**
   - 不是所有场景都需要 60fps，降低帧率可以显著省电
   - DRM 的 `vrr_enabled` property（Variable Refresh Rate）
   - 某些嵌入式场景使用 30fps 或更低

3. **Panel Self Refresh (PSR)**
   - eDP 面板支持 PSR：画面静止时 SoC 停止输出，面板使用内部 RAM 维持显示
   - 可以节省大量 DDR 带宽和 SoC 功耗
   - 需要 eDP Sink 和 Source 都支持

4. **Command Mode 省电**
   - DSI Command Mode 下，只在画面变化时传输数据
   - 适合 公司 遥控器等画面更新频率较低的场景

5. **时钟门控**
   - 在不需要显示时关闭 pixel clock 和 PHY clock
   - Runtime PM 与 DPMS OFF 状态的配合

**今日练习**：
- [ ] 计算你们产品的典型显示带宽需求
- [ ] 了解你们 SoC 是否支持 Framebuffer 压缩
- [ ] 思考：公司 相机在录制视频时，显示驱动如何与视频编码共享 DDR 带宽？

---

### Day 29 — 体系化知识总结

**学习目标**：将 30 天的知识体系化，形成个人知识库。

**产出要求**：

1. **个人技术 Wiki / 知识库**，包含以下章节：
   ```
   1. 显示系统架构总览
      ├── 显示 Pipeline 框图
      ├── KMS 对象关系图
      └── 各接口对比表
   2. MIPI DSI 协议速查
      ├── D-PHY 时序参数速查表
      ├── DSI 包格式速查表
      ├── DCS 命令速查表
      └── DSI 数据率计算公式
   3. Bringup 流程 Checklist
      ├── 硬件检查清单
      ├── DSI 屏 Bringup 步骤
      ├── DP/eDP Bringup 步骤
      └── 常见问题排查决策树
   4. 代码模板
      ├── Panel Driver 模板
      ├── U-Boot Panel Driver 模板
      └── Device Tree 模板
   5. 调试命令速查
      ├── devmem 常用命令
      ├── modetest 常用命令
      ├── DRM debug 开启方法
      └── 时钟/GPIO/Regulator 调试
   6. 与 IC 团队协作
      ├── Bug Report 模板
      ├── Workaround 管理规范
      └── 寄存器手册阅读方法
   ```

2. **一份 Bringup Checklist**（可打印版），覆盖从硬件检查到功能验证的每一步

3. **一份常见问题 FAQ**，至少包含 20 个常见问题的解答

**今日练习**：
- [ ] 完成上述知识库的框架，填充核心内容
- [ ] 检查 30 天学习中的知识盲点，列出还需要深入学习的主题
- [ ] 准备一份 5 分钟的技术分享 PPT，讲述 MIPI DSI 屏幕 bringup 流程

---

### Day 30 — 实战演练 + 后续规划

**学习目标**：用一天时间做一次完整的 bringup 实战（或模拟），并制定后续学习计划。

**实战任务（选择适用的场景）**：

**场景 A — 如果有 FPGA 环境：**
1. 使用裸机代码在 FPGA 上点亮一块 MIPI DSI 屏幕（输出 color bar）
2. 将裸机代码翻译为 Linux DRM 驱动
3. 验证 `modetest` 能正确显示测试图案

**场景 B — 如果有 SoC 开发板：**
1. 适配一块新屏幕（从零开始：写 panel driver + DT + 编译 + 调试）
2. 验证显示功能（分辨率、颜色、背光）
3. 测试休眠唤醒

**场景 C — 如果只有代码环境：**
1. 写一个完整的 MIPI DSI panel driver，能通过编译
2. 写配套的 DT 配置
3. 代码 review 自己的驱动，检查所有 edge case

**后续学习规划**：

| 时间 | 主题 | 目标 |
|------|------|------|
| **月 2** | 深入 DRM atomic + 自研 SoC 适配 | 完成第一块屏幕的真实 bringup |
| **月 3** | DP/eDP 实战 + U-Boot seamless | 完成多接口适配和无缝显示 |
| **月 4** | 性能优化 + 稳定性 | 通过各种压力测试和功耗测试 |
| **月 5** | 高级特性（HDR、色彩管理等） | 成为团队的技术 backbone |
| **月 6** | 输出与分享 | 形成团队的标准 bringup 文档和流程 |

---

## 附录 A：推荐学习资源汇总

### 书籍
- 《Linux Device Drivers Development (2nd Edition)》— John Madieu
- 《Understanding the Linux Kernel》— Daniel Bovet（内核基础参考）
- MIPI Alliance 官方规范（如果公司有会员资格）

### 在线资源
- **Bootlin DRM/KMS Training**：https://bootlin.com/doc/training/graphics/ （强烈推荐）
- **Kernel DRM 文档**：`Documentation/gpu/` 目录
- **LWN.net DRM 相关文章**：搜索 "LWN DRM KMS"
- **DRI-devel 邮件列表**：了解 DRM 社区最新动态

### 参考驱动代码
| 驱动 | 路径 | 学习点 |
|------|------|--------|
| Rockchip VOP2 | `drivers/gpu/drm/rockchip/` | CRTC + Plane 实现 |
| Synopsys DW-DSI | `drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c` | DSI Host |
| Samsung Exynos DSI | `drivers/gpu/drm/samsung/exynos_drm_dsi.c` | 另一种 DSI 实现 |
| Panel BOE | `drivers/gpu/drm/panel/panel-boe-tv101wum-nl6.c` | DSI Panel Driver |
| Panel Simple | `drivers/gpu/drm/panel/panel-simple.c` | 最简 Panel Driver |
| Analogix DP | `drivers/gpu/drm/bridge/analogix/` | DP/eDP Bridge |
| TI SN65DSI86 | `drivers/gpu/drm/bridge/ti-sn65dsi86.c` | DSI-to-eDP Bridge |

### 工具
| 工具 | 用途 | 安装方式 |
|------|------|----------|
| `modetest` | DRM 测试 | libdrm-tests 包 |
| `edid-decode` | EDID 解析 | edid-decode 包 |
| `devmem2` | 寄存器读写 | busybox 或独立工具 |
| `i2cdetect` / `i2cdump` | I2C 调试 | i2c-tools 包 |
| `sigrok` / `PulseView` | 逻辑分析仪软件 | sigrok.org |

---

## 附录 B：关键公式速查

```
=== 显示时序 ===
htotal = hactive + hfp + hsync + hbp
vtotal = vactive + vfp + vsync + vbp
pixel_clock = htotal × vtotal × fps

=== MIPI DSI 数据率 ===
bits_per_pixel: RGB888=24, RGB666=18, RGB565=16
total_bitrate = width × height × bpp × fps
lane_bitrate = total_bitrate / num_lanes × 1.1  (10% margin)
byte_clock = lane_bitrate / 8

=== DSI Timing 换算（pixel clock → byte clock）===
ratio = byte_clock × num_lanes / (pixel_clock × bpp_to_bytes)
  where bpp_to_bytes: RGB888=3, RGB666_packed=2.25, RGB565=2
dsi_hsa_time = hsa × ratio
dsi_hbp_time = hbp × ratio
dsi_hline_time = htotal × ratio

=== D-PHY Timing ===
T_UI = 1 / lane_bitrate (ns)
T_HS_PREPARE: 40 + 4×T_UI ~ 85 + 6×T_UI (ns)
T_HS_ZERO:    max(105 + 6×T_UI - T_HS_PREPARE, 0) (ns)
T_HS_TRAIL:   max(8×T_UI, 60 + 4×T_UI) (ns)
T_CLK_PREPARE: 38 ~ 95 (ns)
T_CLK_ZERO:   max(300 - T_CLK_PREPARE, 0) (ns)

=== 带宽 ===
display_bandwidth = width × height × bytes_per_pixel × fps × num_active_layers
```

---

## 附录 C：每日学习时间分配建议

| 活动 | 建议时长 | 说明 |
|------|---------|------|
| **理论学习**（看文档/规范） | 2~3 小时 | 上午精力最好时进行 |
| **代码阅读**（内核/驱动源码） | 2~3 小时 | 带着问题去读，不要漫无目的 |
| **动手实践**（写代码/调试） | 2~3 小时 | 下午或晚上 |
| **笔记整理** | 0.5~1 小时 | 每天结束前 |
| **与同事交流** | 0.5~1 小时 | 午饭时间或 tea break |

> **核心原则**：理论 30% + 代码阅读 30% + 动手实践 40%。不要只看不动手。

---

*Last Updated: 2026-03-11*
*Author: 公司 Display Driver Bringup Study Plan*
