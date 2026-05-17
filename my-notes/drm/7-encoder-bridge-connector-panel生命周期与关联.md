# DRM Encoder、Bridge、Connector、Panel 的生命周期与关联

> 基于 Linux DRM 核心框架 + Rockchip 驱动实例（LVDS / DSI / RGB），每个关键函数标注可跳转的文件地址。

---

## 一、四个组件在显示管道中的位置

```
┌──────┐    ┌─────────┐    ┌────────────┐    ┌───────────┐    ┌───────┐
│ CRTC │───→│ Encoder │───→│ Bridge(s)  │───→│ Connector │───→│ Panel │
│(VOP2)│    │(DSI/LVDS│    │(协议转换芯片)│    │(物理接口)  │    │(LCD屏)│
│      │    │ /RGB/TV)│    │linked list │    │           │    │       │
└──────┘    └─────────┘    └────────────┘    └───────────┘    └───────┘
   ↑             ↑               ↑                ↑               ↑
 生成像素    信号格式转换    外部芯片桥接     物理端口+EDID    面板电源/时序
 时序信号    (并行→DSI等)   (DSI→HDMI等)    状态检测+模式    背光+模式列表
```

**关键认知**：Encoder、Bridge、Connector 三者通过链表和 ID 数组相互引用，但**没有嵌入关系**——它们是通过 `attach` 语义关联的独立 KMS 对象。

---

## 二、结构体定义与关联字段

### 2.1 `struct drm_encoder` — 信号格式转换器

**定义位置:** [drm_encoder.h:101](../../kernel/include/drm/drm_encoder.h#L101)

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `bridge` | `struct drm_bridge *` | [drm_encoder.h:281](../../kernel/include/drm/drm_encoder.h#L281) | **Bridge 链表头指针**。多个 bridge 通过 `bridge->next` 串成单向链表 |
| `possible_crtcs` | `uint32_t` | [drm_encoder.h:190](../../kernel/include/drm/drm_encoder.h#L190) | 此 encoder 能接收哪些 CRTC 的像素流（位掩码） |
| `possible_clones` | `uint32_t` | [drm_encoder.h:248](../../kernel/include/drm/drm_encoder.h#L248) | 哪些 encoder 可以同时输出相同的画面 |
| `crtc` | `struct drm_crtc *` | [drm_encoder.h:271](../../kernel/include/drm/drm_encoder.h#L271) | **Legacy only**：当前绑定的 CRTC。atomic 驱动不用此字段 |
| `encoder_type` | `int` | [drm_encoder.h:163](../../kernel/include/drm/drm_encoder.h#L163) | `DRM_MODE_ENCODER_DSI` / `LVDS` / `TMDS` / `DPI` 等 |
| `index` | `unsigned` | [drm_encoder.h:171](../../kernel/include/drm/drm_encoder.h#L171) | encoder_list 中的位置索引 |
| `funcs` | `const struct drm_encoder_funcs *` | [drm_encoder.h:293](../../kernel/include/drm/drm_encoder.h#L293) | 生命周期回调（至少提供 `destroy`） |
| `helper_private` | `const struct drm_encoder_helper_funcs *` | [drm_encoder.h:305](../../kernel/include/drm/drm_encoder.h#L305) | 模式设置回调（atomic_check, enable, disable 等） |

### 2.2 `struct drm_bridge` — 外部桥接芯片抽象

**定义位置:** [drm_bridge.h:319](../../kernel/include/drm/drm_bridge.h#L319)

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `dev` | `struct drm_device *` | [drm_bridge.h:327](../../kernel/include/drm/drm_bridge.h#L327) | 所属 DRM 设备，`drm_bridge_attach()` 时设置 |
| `encoder` | `struct drm_encoder *` | [drm_bridge.h:339](../../kernel/include/drm/drm_bridge.h#L339) | 上游 encoder。同一链条所有 bridge 指向同一个 encoder |
| `next` | `struct drm_bridge *` | [drm_bridge.h:357](../../kernel/include/drm/drm_bridge.h#L357) | **链表下一节点**（靠近 connector 方向）。构建链条：`encoder->bridge → b0 → b1 → NULL` |
| `list` | `struct list_head` | [drm_bridge.h:393](../../kernel/include/drm/drm_bridge.h#L393) | **全局 bridge_list 节点**。通过 `drm_bridge_add()` 注册 |
| `of_node` | `struct device_node *` | [drm_bridge.h:379](../../kernel/include/drm/drm_bridge.h#L379) | 设备树节点，`of_drm_find_bridge()` 按此查找 |
| `funcs` | `const struct drm_bridge_funcs *` | [drm_bridge.h:463](../../kernel/include/drm/drm_bridge.h#L463) | 可选回调表 |
| `timings` | `const struct drm_bridge_timings *` | [drm_bridge.h:413](../../kernel/include/drm/drm_bridge.h#L413) | 输入侧物理时序约束 |
| `driver_private` | `void *` | [drm_bridge.h:482](../../kernel/include/drm/drm_bridge.h#L482) | 桥驱动私有数据（DSI host 等） |

### 2.3 `struct drm_connector` — 物理输出接口

**定义位置:** [drm_connector.h:1009](../../kernel/include/drm/drm_connector.h#L1009)

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `encoder_ids[]` | `uint32_t[3]` | [drm_connector.h:1392](../../kernel/include/drm/drm_connector.h#L1392) | **可能的 encoder 的 mode object ID 数组**（最多 3 个）。通过 `drm_connector_attach_encoder()` 填充 |
| `encoder` | `struct drm_encoder *` | [drm_connector.h:1409](../../kernel/include/drm/drm_connector.h#L1409) | **Legacy only**：当前绑定的 encoder。atomic 驱动使用 `connector_state->best_encoder` |
| `panel` | `struct drm_panel *` | [drm_connector.h:1678](../../kernel/include/drm/drm_connector.h#L1678) | 内嵌面板指针（DSI/eDP/LVDS 使用，HDMI/DP 为 NULL） |
| `status` | `enum drm_connector_status` | [drm_connector.h:1158](../../kernel/include/drm/drm_connector.h#L1158) | connected / disconnected / unknown |
| `modes` | `struct list_head` | [drm_connector.h:1145](../../kernel/include/drm/drm_connector.h#L1145) | 已验证的模式列表（暴露给用户空间） |
| `probed_modes` | `struct list_head` | [drm_connector.h:1168](../../kernel/include/drm/drm_connector.h#L1168) | `get_modes()` 返回的原始模式 |
| `polled` | `uint8_t` | [drm_connector.h:1314](../../kernel/include/drm/drm_connector.h#L1314) | 热插拔检测方式：`HPD` / `CONNECT` / `DISCONNECT` |
| `force` | `enum drm_connector_force` | [drm_connector.h:1361](../../kernel/include/drm/drm_connector.h#L1361) | 内核命令行/调试覆盖状态 |
| `display_info` | `struct drm_display_info` | [drm_connector.h:1182](../../kernel/include/drm/drm_connector.h#L1182) | 物理尺寸、bpc、HDMI 信息 |

### 2.4 `struct drm_connector_state` — 运行时状态（atomic 关键字段）

**定义位置:** [drm_connector.h:544](../../kernel/include/drm/drm_connector.h#L544)

| 字段 | 位置 | 含义 |
|------|------|------|
| `crtc` | [drm_connector.h:554](../../kernel/include/drm/drm_connector.h#L554) | 此 connector 连接到哪个 CRTC (NULL = 禁用) |
| `best_encoder` | [drm_connector.h:563](../../kernel/include/drm/drm_connector.h#L563) | 当前选中的 encoder（由 `update_connector_routing()` 填充） |

**核心关联链路（atomic 模式）：**
```
connector_state->crtc          → 此 connector 在哪个 CRTC 上
connector_state->best_encoder  → 此 connector 由哪个 encoder 驱动
encoder->bridge                → bridge 链表头
bridge->next                   → 下一个 bridge
```

### 2.5 `struct drm_panel` — 显示面板抽象

**定义位置:** [drm_panel.h:154](../../kernel/include/drm/drm_panel.h#L154)

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `drm` | `struct drm_device *` | — | `drm_panel_attach()` 时设置 |
| `connector` | `struct drm_connector *` | — | 关联的 connector，用于 `get_modes()` 添加模式 |
| `funcs` | `const struct drm_panel_funcs *` | — | 回调表 |
| `list` | `struct list_head` | — | 全局 `panel_list` 节点 |

`drm_panel_funcs` 定义在 [drm_panel.h:91](../../kernel/include/drm/drm_panel.h#L91)：
```c
struct drm_panel_funcs {
    int (*loader_protect)(struct drm_panel *panel, bool on);   // Rockchip 扩展
    int (*disable)(struct drm_panel *panel);
    int (*unprepare)(struct drm_panel *panel);
    int (*prepare)(struct drm_panel *panel);
    int (*enable)(struct drm_panel *panel);
    int (*get_modes)(struct drm_panel *panel);
    int (*get_timings)(struct drm_panel *panel, unsigned int num_timings,
                       struct display_timing *timings);
};
```

### 2.6 Rockchip 驱动中的实现模式

Rockchip 没有独立的 `struct rockchip_encoder`。各输出驱动将 `drm_encoder` **直接嵌入**私有结构体：

| 驱动文件 | 私有结构体 | encoder 字段 | `container_of` 宏 |
|----------|-----------|-------------|-------------------|
| [rockchip_lvds.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c) | `struct rockchip_lvds` ([L103](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L103)) | `encoder` | `encoder_to_lvds()` ([L130](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L130)) |
| [rockchip_rgb.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c) | `struct rockchip_rgb` ([L66](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L66)) | `encoder` | `encoder_to_rgb()` ([L85](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L85)) |
| [dw-mipi-dsi.c](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c) | `struct dw_mipi_dsi` ([L266](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L266)) | `encoder` | `encoder_to_dsi()` |

---

## 三、Encoder 的完整生命周期

### 3.1 回调表

**`drm_encoder_funcs`** — 生命周期回调（[drm_encoder.h:293](../../kernel/include/drm/drm_encoder.h#L293)）：至少提供 `destroy`。可选 `late_register`, `early_unregister`。

**`drm_encoder_helper_funcs`** — 模式设置回调（[drm_modeset_helper_vtables.h:466](../../kernel/include/drm/drm_modeset_helper_vtables.h#L466)）：

| 回调 | 定义位置 | 用途 |
|------|----------|------|
| `atomic_check` | [L765](../../kernel/include/drm/drm_modeset_helper_vtables.h#L765) | atomic：验证 encoder 完整状态（mode_fixup 的超集） |
| `enable` | [L727](../../kernel/include/drm/drm_modeset_helper_vtables.h#L727) | atomic：使能 encoder（CRTC 使能**之后**调用） |
| `disable` | [L709](../../kernel/include/drm/drm_modeset_helper_vtables.h#L709) | atomic：禁用 encoder（CRTC 禁用**之前**调用） |
| `atomic_mode_set` | [L643](../../kernel/include/drm/drm_modeset_helper_vtables.h#L643) | atomic：编程模式时序 |
| `mode_fixup` | [L564](../../kernel/include/drm/drm_modeset_helper_vtables.h#L564) | 调整模式参数（`atomic_check` 的 fallback） |
| `mode_valid` | [L521](../../kernel/include/drm/drm_modeset_helper_vtables.h#L521) | 验证模式是否在 encoder 硬件能力内 |

### 3.2 分配与初始化

#### `drm_encoder_init()`

**文件:** [drm_encoder.c:106](../../kernel/drivers/gpu/drm/drm_encoder.c#L106)

```c
int drm_encoder_init(struct drm_device *dev, struct drm_encoder *encoder,
                     const struct drm_encoder_funcs *funcs,
                     int encoder_type, const char *name, ...);
```

**内部步骤：**
1. 限制 encoder 总数 ≤ 32（[L114](../../kernel/drivers/gpu/drm/drm_encoder.c#L114)）
2. 注册为 KMS mode object（[L117](../../kernel/drivers/gpu/drm/drm_encoder.c#L117)）
3. 存储 `dev`、`encoder_type`、`funcs`（[L121-123](../../kernel/drivers/gpu/drm/drm_encoder.c#L121)）
4. 格式化名称（[L124-134](../../kernel/drivers/gpu/drm/drm_encoder.c#L124)）：指定 name 或自动生成如 `"TMDS-0"`
5. 插入 `encoder_list`，分配 `encoder->index`（[L140-141](../../kernel/drivers/gpu/drm/drm_encoder.c#L140)）

**驱动调用前必须预先设置**：`possible_crtcs` 和 `possible_clones`（通过 `drm_of_find_possible_crtcs()` 获取）。

#### Rockchip LVDS 中的 encoder 初始化示意

```c
// [rockchip_lvds.c:369-400]
encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev, dev->of_node);
drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
                 DRM_MODE_ENCODER_LVDS, NULL);
drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);
```

### 3.3 注册

#### `drm_modeset_register_all()` — 注册入口

**文件:** [drm_mode_config.c:30](../../kernel/drivers/gpu/drm/drm_mode_config.c#L30)

调用自 `drm_dev_register()`（[drm_drv.c:957](../../kernel/drivers/gpu/drm/drm_drv.c#L957)）。注册顺序：
1. `drm_plane_register_all()` — planes 先
2. `drm_crtc_register_all()` — CRTC
3. **`drm_encoder_register_all()`** — encoder（[drm_encoder.c:66](../../kernel/drivers/gpu/drm/drm_encoder.c#L66)）
4. `drm_connector_register_all()` — connector 最后

`drm_encoder_register_all()` 为每个 encoder 调用可选的 `late_register` 回调。

### 3.4 销毁

#### `drm_encoder_cleanup()`

**文件:** [drm_encoder.c:157](../../kernel/drivers/gpu/drm/drm_encoder.c#L157)

1. **遍历并 detach 全部 bridge**（[L166-175](../../kernel/drivers/gpu/drm/drm_encoder.c#L166)）
2. 注销 mode object（[L177](../../kernel/drivers/gpu/drm/drm_encoder.c#L177)）
3. 释放 name（[L178](../../kernel/drivers/gpu/drm/drm_encoder.c#L178)）
4. 从 encoder_list 移除，递减 num_encoder（[L179-180](../../kernel/drivers/gpu/drm/drm_encoder.c#L179)）
5. **memset 清零**（[L182](../../kernel/drivers/gpu/drm/drm_encoder.c#L182)）

由驱动的 `->destroy` 回调调用。`drm_mode_config_cleanup()`（[drm_mode_config.c:431](../../kernel/drivers/gpu/drm/drm_mode_config.c#L431)）中 encoder 最先被销毁。

---

## 四、Bridge 的完整生命周期与链表概念

### 4.1 Bridge 回调表

**定义位置:** [drm_bridge.h:38](../../kernel/include/drm/drm_bridge.h#L38)，**全部可选**：

| 回调 | 定义位置 | 调用时机 | 信号状态 |
|------|----------|----------|----------|
| `attach` | [L51](../../kernel/include/drm/drm_bridge.h#L51) | `drm_bridge_attach()` 时 | — |
| `detach` | [L61](../../kernel/include/drm/drm_bridge.h#L61) | `drm_bridge_detach()` 时 | — |
| `mode_valid` | [L94](../../kernel/include/drm/drm_bridge.h#L94) | atomic check 阶段 | — |
| `mode_fixup` | [L132](../../kernel/include/drm/drm_bridge.h#L132) | atomic check 阶段（唯一可拒绝 modeset 的钩子） | — |
| `mode_set` | [L198](../../kernel/include/drm/drm_bridge.h#L198) | modeset commit 阶段，CRTC 已编程 | 时钟未运行 |
| `pre_enable` | [L219](../../kernel/include/drm/drm_bridge.h#L219) | 上游使能前 | 时钟未运行 |
| `enable` | [L239](../../kernel/include/drm/drm_bridge.h#L239) | 上游使能后 | 时钟运行中（**必须使能下游链路**） |
| `disable` | [L151](../../kernel/include/drm/drm_bridge.h#L151) | 上游禁用前 | 时钟仍运行 |
| `post_disable` | [L170](../../kernel/include/drm/drm_bridge.h#L170) | 上游禁用后 | 时钟已停止 |

### 4.2 Bridge 链表结构

```
encoder->bridge → [bridge_0].next → [bridge_1].next → NULL
                   (encoder侧)      (connector侧, 最后一个)
```

- `drm_bridge_attach(encoder, bridge, NULL)` — bridge 成为链首（`encoder->bridge = bridge`）
- `drm_bridge_attach(encoder, bridge2, bridge1)` — bridge2 链接在 bridge1 之后（`bridge1->next = bridge2`）
- 同一链条所有 bridge 的 `->encoder` 指针**都指向链首的 encoder**

### 4.3 全局注册 vs 链条连接（两个独立的链表）

```
全局注册（发现机制）:              链条连接（运行时拓扑）:

  bridge_list (全局)               encoder->bridge
    ├─ bridge_A                       ├─ bridge_A.next → bridge_B.next → NULL
    ├─ bridge_B                       │   (encoder = &encoder_0)   (encoder = &encoder_0)
    └─ bridge_C                       │

of_drm_find_bridge(np)            drm_bridge_chain_enable(encoder->bridge)
  → 遍历 bridge_list                  → 遍历 bridge->next 链表
  → 匹配 of_node
```

### 4.4 生命周期函数

| 函数 | 文件:行号 | 作用 |
|------|-----------|------|
| `drm_bridge_add()` | [drm_bridge.c:71](../../kernel/drivers/gpu/drm/drm_bridge.c#L71) | 注册到全局 `bridge_list`（bridge 驱动 probe 时调用） |
| `drm_bridge_remove()` | [drm_bridge.c:84](../../kernel/drivers/gpu/drm/drm_bridge.c#L84) | 从 `bridge_list` 注销 |
| `drm_bridge_attach()` | [drm_bridge.c:110](../../kernel/drivers/gpu/drm/drm_bridge.c#L110) | 绑定到 encoder 并链接到链条 |
| `drm_bridge_detach()` | [drm_bridge.c:145](../../kernel/drivers/gpu/drm/drm_bridge.c#L145) | 从链条解绑（调用 `detach` 回调） |
| `of_drm_find_bridge()` | [drm_bridge.c:361](../../kernel/drivers/gpu/drm/drm_bridge.c#L361) | 按设备树节点在全局列表中查找 |

### 4.5 链表遍历方向（enable/disable 顺序的核心原因）

**Bridge 遍历函数一览：**

| 函数 | 文件:行号 | 方向 | 含义 |
|------|-----------|------|------|
| `drm_bridge_disable()` | [drm_bridge.c:246](../../kernel/drivers/gpu/drm/drm_bridge.c#L246) | **逆向**（尾→头） | 先递归 `next`，再处理当前 |
| `drm_bridge_post_disable()` | [drm_bridge.c:268](../../kernel/drivers/gpu/drm/drm_bridge.c#L268) | 正向（头→尾） | 先处理当前，再递归 `next` |
| `drm_bridge_pre_enable()` | [drm_bridge.c:317](../../kernel/drivers/gpu/drm/drm_bridge.c#L317) | **逆向**（尾→头） | 先递归 `next`，再处理当前 |
| `drm_bridge_enable()` | [drm_bridge.c:339](../../kernel/drivers/gpu/drm/drm_bridge.c#L339) | 正向（头→尾） | 先处理当前，再递归 `next` |
| `drm_bridge_mode_set()` | [drm_bridge.c:292](../../kernel/drivers/gpu/drm/drm_bridge.c#L292) | 正向 | 先处理当前，再递归 `next` |
| `drm_bridge_mode_fixup()` | [drm_bridge.c:185](../../kernel/drivers/gpu/drm/drm_bridge.c#L185) | 正向 | 处理当前，再递归 `next` |
| `drm_bridge_mode_valid()` | [drm_bridge.c:218](../../kernel/drivers/gpu/drm/drm_bridge.c#L218) | 正向 | 处理当前，遇到失败立即返回 |

**为什么 disable 是逆向？** connector 侧的 bridge 必须**先**关闭（下游先变暗），但时钟还在运行可以安全完成信号清理。然后上游逐个关闭。`post_disable` 是正向的，因为时钟已停止，从上游开始顺序断电。

**为什么 pre_enable 是逆向？** connector 侧的 bridge 必须**先**准备好（下游先上电），然后上游才开始发送时钟/数据。`enable` 是正向的，因为上游正在发送稳定时钟/数据，下游可以安全转发视频。

### 4.6 两种 Bridge 类型

| 类型 | 例子 | 特点 |
|------|------|------|
| **透明 Bridge (panel bridge)** | [bridge/panel.c](../../kernel/drivers/gpu/drm/bridge/panel.c) | 将 `drm_panel` 包装成 `drm_bridge`。**无自己的 DT 节点**，继承 panel 的 `of_node`。内部自动创建 `drm_connector`。 |
| **硬件 Bridge** | lt8912, dw-hdmi, analogix-anx78xx | 代表真实外部芯片（DSI→HDMI 转换器等）。**有独立 DT 节点**。完整实现 `drm_bridge_funcs`。 |

两者使用相同的 `drm_bridge_add()` / `drm_bridge_attach()` 生命周期，这是 bridge 抽象的要点：核心和 encoder 驱动**统一对待**所有 bridge。

---

## 五、Connector 的完整生命周期

### 5.1 分配与初始化

#### `drm_connector_init()`

**文件:** [drm_connector.c:192](../../kernel/drivers/gpu/drm/drm_connector.c#L192)

```c
int drm_connector_init(struct drm_device *dev, struct drm_connector *connector,
                       const struct drm_connector_funcs *funcs,
                       int connector_type);
```

**内部步骤：**
1. atomic 驱动要求 `atomic_destroy_state` + `atomic_duplicate_state`（[L202-204](../../kernel/drivers/gpu/drm/drm_connector.c#L202)）
2. 注册为 KMS mode object（[L206-208](../../kernel/drivers/gpu/drm/drm_connector.c#L206)）
3. 从 `connector_ida` 分配 index（范围 0~31）（[L217-224](../../kernel/drivers/gpu/drm/drm_connector.c#L217)）
4. 从 per-type ida 分配 type_id（如 "HDMI-A-**1**" 的数字部分）（[L228-233](../../kernel/drivers/gpu/drm/drm_connector.c#L228)）
5. 自动生成名称（[L234-241](../../kernel/drivers/gpu/drm/drm_connector.c#L234)）
6. 初始化 `probed_modes` 和 `modes` 链表（[L243-244](../../kernel/drivers/gpu/drm/drm_connector.c#L243)）
7. 初始化 `mutex`（[L245](../../kernel/drivers/gpu/drm/drm_connector.c#L245)）
8. 设置 `status = connector_status_unknown`（[L246-249](../../kernel/drivers/gpu/drm/drm_connector.c#L246)）
9. 解析 `video=<connector>:<mode>` 内核命令行（[L251](../../kernel/drivers/gpu/drm/drm_connector.c#L251)）
10. 添加到 `connector_list` 尾部（[L255-258](../../kernel/drivers/gpu/drm/drm_connector.c#L255)）
11. 附加标准属性：EDID blob、DPMS、link-status、non-desktop、CRTC_ID（[L260-277](../../kernel/drivers/gpu/drm/drm_connector.c#L260)）

#### `drm_connector_attach_encoder()`

**文件:** [drm_connector.c:324](../../kernel/drivers/gpu/drm/drm_connector.c#L324)

将 encoder 的 mode object ID 填入 `connector->encoder_ids[]`（最多 3 个）。这是 connector→encoder 关联的建立方式。

### 5.2 注册

`drm_connector_register()`（[drm_connector.c:447](../../kernel/drivers/gpu/drm/drm_connector.c#L447)）：
1. 仅当 `dev->registered == true` 时生效（[L451](../../kernel/drivers/gpu/drm/drm_connector.c#L451)）
2. 创建 sysfs：`/sys/class/drm/cardX-<name>/`（[L458](../../kernel/drivers/gpu/drm/drm_connector.c#L458)）
3. 创建 debugfs：`/sys/kernel/debug/dri/<minor>/<name>/`（[L462](../../kernel/drivers/gpu/drm/drm_connector.c#L462)）
4. 调用可选的 `late_register`（[L467-470](../../kernel/drivers/gpu/drm/drm_connector.c#L467)）
5. 注册 mode object 暴露给用户空间（[L473](../../kernel/drivers/gpu/drm/drm_connector.c#L473)）

注册顺序（[drm_mode_config.c:30](../../kernel/drivers/gpu/drm/drm_mode_config.c#L30)）：plane → CRTC → encoder → **connector（最后）**

注销时顺序相反：**connector 先注销**（[drm_mode_config.c:62](../../kernel/drivers/gpu/drm/drm_mode_config.c#L62)）。

### 5.3 状态检测与模式获取

#### `drm_helper_probe_single_connector_modes()` — 核心填充流程

**文件:** [drm_probe_helper.c:387](../../kernel/drivers/gpu/drm/drm_probe_helper.c#L387)

这是 **`fill_modes`** 的标准实现。完整流程：

```
1. 标记旧模式为 MODE_STALE
2. 状态检测:
   ├─ 如果 force 被覆盖 → 直接设置 status
   └─ 否则 → drm_helper_probe_detect(connector, &ctx, true)
3. 如果断开连接:
   ├─ 清除 EDID (drm_connector_update_edid_property(connector, NULL))
   └─ 跳到 prune 步骤
4. get_modes():
   ├─ 调用 connector_funcs->get_modes(connector)
   │    ├─ Embedded panel: drm_panel_get_modes(panel)
   │    │    └─ panel->funcs->get_modes(panel) → drm_mode_probed_add(connector, mode)
   │    └─ External monitor: drm_add_edid_modes(connector, edid)
   │         └─ 解析 EDID 详细时序 → drm_mode_probed_add()
   ├─ Fallback: firmware EDID 覆盖
   ├─ Fallback: VESA DMT (最高 1024x768)
   └─ Fallback: 内核命令行模式
5. drm_connector_list_update() → 合并 probed_modes → modes
6. 管道验证: 每个 mode 通过:
   ├─ drm_mode_validate_driver()    → 基本检查
   ├─ drm_mode_validate_size()      → max_width/max_height
   ├─ drm_mode_validate_flag()      → interlace/doublescan/stereo
   └─ drm_mode_validate_pipeline()  → encoder→bridge→crtc 逐级 mode_valid
7. drm_mode_prune_invalid() → 清除 MODE_OK 外的所有 mode
8. 排序: preferred 优先 → 按尺寸降序
```

**热插拔机制**：
- **HPD 中断**：`drm_helper_hpd_irq_event()`（[drm_probe_helper.c:772](../../kernel/drivers/gpu/drm/drm_probe_helper.c#L772)）检测 `POLL_HPD` 的 connector
- **定时轮询**：`output_poll_execute()`（[drm_probe_helper.c:575](../../kernel/drivers/gpu/drm/drm_probe_helper.c#L575)）每 10 秒轮询无 HPD 能力的 connector（`POLL_CONNECT`/`POLL_DISCONNECT`），`force=false` 避免破坏性探测

### 5.4 销毁

#### `drm_connector_cleanup()`

**文件:** [drm_connector.c:389](../../kernel/drivers/gpu/drm/drm_connector.c#L389)

1. 如仍为 REGISTERED 状态，先调用 `drm_connector_unregister()`（[L397-399](../../kernel/drivers/gpu/drm/drm_connector.c#L397)）
2. 释放 tile_group 引用（[L401-404](../../kernel/drivers/gpu/drm/drm_connector.c#L401)）
3. 销毁 `probed_modes` 和 `modes` 链表（[L406-410](../../kernel/drivers/gpu/drm/drm_connector.c#L406)）
4. 释放 type_id 和 index（[L412-416](../../kernel/drivers/gpu/drm/drm_connector.c#L412)）
5. 释放 `display_info.bus_formats`（[L418](../../kernel/drivers/gpu/drm/drm_connector.c#L418)）
6. 注销 mode object（[L419](../../kernel/drivers/gpu/drm/drm_connector.c#L419)）
7. 释放 name（[L420-421](../../kernel/drivers/gpu/drm/drm_connector.c#L420)）
8. 从 `connector_list` 移除（[L422-425](../../kernel/drivers/gpu/drm/drm_connector.c#L422)）
9. 销毁 atomic state（[L427-430](../../kernel/drivers/gpu/drm/drm_connector.c#L427)）
10. 销毁 mutex + memset 清零（[L432-434](../../kernel/drivers/gpu/drm/drm_connector.c#L432)）

`drm_mode_config_cleanup()` 中 encoder 先于 connector 销毁（[L442-463](../../kernel/drivers/gpu/drm/drm_mode_config.c#L442)）。connector 使用 `drm_connector_put()` 降引用+工作队列延迟释放机制。

---

## 六、Panel 的完整生命周期

### 6.1 创建与注册

```c
// 1. panel 驱动 probe 时:
drm_panel_init(&panel->base);              // [drm_panel.c:48]
panel->base.funcs = &my_panel_funcs;
panel->base.dev = dev;
drm_panel_add(&panel->base);              // [drm_panel.c:64] → 加入全局 panel_list

// 2. encoder 驱动 bind 时查找:
panel = of_drm_find_panel(remote_np);     // [drm_panel.c:151] → 遍历 panel_list 匹配 of_node

// 3. 绑定:
drm_panel_attach(panel, connector);       // [drm_panel.c:103] → 设置 panel->connector = connector
connector->panel = panel;                 // 由调用者设置
```

### 6.2 电源生命周期：prepare → enable → disable → unprepare

这是 panel 最核心的语义——**信号和电源的分离**：

| 阶段 | 信号状态 | panel callback | 典型操作 |
|------|----------|----------------|----------|
| **prepare** | 视频尚未开始 | `prepare()` | 上电 (AVDD/IOVCC)、取消复位、发 DSI 初始化命令 |
| **enable** | 视频已开始传输 | `enable()` | 开启背光 |
| **disable** | 视频仍在传输 | `disable()` | 关闭背光（可发送 DCS "display off"） |
| **unprepare** | 视频已停止 | `unprepare()` | 复位、关闭电源 |

**为什么这样设计？** 关键约束来自于 LCD panel 的初始化时序要求：
- 必须**先供电+初始化命令**，再**开启背光**（否则花屏或白屏）
- 必须**先关背光**，再**断电**（否则闪屏或损坏 panel）

#### 实际例子：Rockchip DW-MIPI-DSI encoder 中的调用

**文件:** [dw-mipi-dsi.c](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c)

**使能路径**（`dw_mipi_dsi_encoder_enable` [L1324](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1324)）：
```c
dw_mipi_dsi_pre_enable(dsi);       // [L1345]: 初始化 DSI PHY、配置时钟
drm_panel_prepare(dsi->panel);     // [L1347]: 面板上电、发初始化命令
dw_mipi_dsi_enable(dsi);           // [L1348]: 开始发送视频流
drm_panel_enable(dsi->panel);      // [L1354]: 开启背光 ← 视频流已稳定
```

**禁用路径**（`dw_mipi_dsi_encoder_disable` [L1091](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1091)）：
```c
drm_panel_disable(dsi->panel);      // [L1096]: 关闭背光（视频仍在运行）
dw_mipi_dsi_disable(dsi);           // [L1101]: 停止视频流
drm_panel_unprepare(dsi->panel);    // [L1103]: 面板断电
dw_mipi_dsi_post_disable(dsi);      // [L1104]: 关闭 DSI PHY
```

### 6.3 Panel-Bridge 适配器

**文件:** [bridge/panel.c](../../kernel/drivers/gpu/drm/bridge/panel.c)

`struct panel_bridge`（[L20-25](../../kernel/drivers/gpu/drm/bridge/panel.c#L20)）是一个**内置 composite 对象**：嵌入 `drm_bridge` + `drm_connector`，包装 `drm_panel`。

```c
struct panel_bridge {
    struct drm_bridge bridge;        // 注册给 encoder 的 bridge
    struct drm_connector connector;  // 内部自动创建的 connector
    struct drm_panel *panel;         // 被包装的 panel
    u32 connector_type;
};
```

**Bridge → Panel 回调映射**（[bridge/panel.c:127-133](../../kernel/drivers/gpu/drm/bridge/panel.c#L127)）：

| Bridge 回调 | Panel 回调 | 原因 |
|-------------|-----------|------|
| `pre_enable` ([L99](../../kernel/drivers/gpu/drm/bridge/panel.c#L99)) | `drm_panel_prepare()` ([L103](../../kernel/drivers/gpu/drm/bridge/panel.c#L103)) | bridge 阶段"信号未运行"=panel 阶段"上电准备" |
| `enable` ([L106](../../kernel/drivers/gpu/drm/bridge/panel.c#L106)) | `drm_panel_enable()` ([L110](../../kernel/drivers/gpu/drm/bridge/panel.c#L110)) | bridge 阶段"信号已运行"=panel 阶段"开背光" |
| `disable` ([L113](../../kernel/drivers/gpu/drm/bridge/panel.c#L113)) | `drm_panel_disable()` ([L117](../../kernel/drivers/gpu/drm/bridge/panel.c#L117)) | bridge 阶段"信号仍在运行"=panel 阶段"关背光" |
| `post_disable` ([L120](../../kernel/drivers/gpu/drm/bridge/panel.c#L120)) | `drm_panel_unprepare()` ([L124](../../kernel/drivers/gpu/drm/bridge/panel.c#L124)) | bridge 阶段"信号已停止"=panel 阶段"断电" |

`drm_panel_bridge_add()`（[L156](../../kernel/drivers/gpu/drm/bridge/panel.c#L156)）：用 `devm_kzalloc` 分配、设置 `bridge.of_node = panel->dev->of_node`（用于 `of_drm_find_bridge` 查找）、`drm_bridge_add()` 注册。

`attach` 回调（[L60-90](../../kernel/drivers/gpu/drm/bridge/panel.c#L60)）中：自动创建 `drm_connector`、调用 `drm_connector_attach_encoder()`、调用 `drm_panel_attach()`；`get_modes` 直接委托给 `drm_panel_get_modes()`。

### 6.4 模式获取流程

```
用户空间: DRM_IOCTL_MODE_GETCONNECTOR
  → drm_mode_getconnector()
    → drm_helper_probe_single_connector_modes(connector)
      → connector_funcs->get_modes(connector)
          ├─ Embedded Panel 路径:
          │    drm_panel_get_modes(panel)                      [drm_panel.h:339]
          │      → panel->funcs->get_modes(panel)
          │           ├─ drm_mode_duplicate(panel->drm, desc_mode)
          │           ├─ drm_mode_probed_add(panel->connector, mode)  ← 加入 connector
          │           └─ 填充 display_info.width_mm/height_mm/bpc
          │
          └─ External Monitor 路径:
               drm_add_edid_modes(connector, edid)
                 → 解析 EDID 详细时序 → drm_mode_probed_add()
```

---

## 七、Rockchip 驱动中的完整绑定流程

### 7.1 OF Graph 拓扑 — CRTC→Encoder 的连线

设备树决定了哪个 VP 输出到哪个 encoder：

```
&vp0 {                        // VOP2 video_port0 → crtc->port
    vp0_out_dsi0: endpoint@0 { remote-endpoint = <&dsi0_in>; };
    vp0_out_lvds0: endpoint@1 { remote-endpoint = <&lvds0_in>; };
};

&dsi0 {                       // DSI encoder
    ports {
        port@0 { endpoint { remote-endpoint = <&vp0_out_dsi0>; }; };
        port@1 { endpoint { remote-endpoint = <&panel_in>; }; };   // panel 或 bridge
    };
};
```

### 7.2 Encoder 的 Panel/Bridge 发现

每个 encoder 的 bind 函数中统一调用：
```c
ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1, &panel, &bridge);
```
参数 `port=1, endpoint=-1` → 在 encoder DT 节点的 port@1 找第一个 endpoint 的远端设备。

调用位置：
- [rockchip_lvds.c:384](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L384)
- [rockchip_rgb.c:285](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L285)
- [dw-mipi-dsi.c:1553](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1553)

**分支处理：**

| 发现结果 | 处理方式 |
|----------|----------|
| **Panel** | encoder 驱动自己创建 `drm_connector` → 附加到 encoder → `drm_panel_attach()` → 注册 `rockchip_drm_sub_dev` |
| **Bridge** | 调用 `drm_bridge_attach(encoder, bridge, NULL)` → bridge 内部处理 connector |
| **都失败** | 返回 `-EPROBE_DEFER` 等待 panel/bridge 驱动加载 |

### 7.3 组件框架 — 主设备与子驱动的聚合

**文件:** [rockchip_drm_drv.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c)

`rockchip_drm_platform_driver`（匹配 `"rockchip,display-subsystem"`）是**主设备**。使用 Linux component framework 聚合子驱动：

```c
// rockchip_drm_init() [rockchip_drm_drv.c:3670]:
ADD_ROCKCHIP_SUB_DRIVER(vop2_platform_driver, CONFIG_ROCKCHIP_VOP2);
ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver, CONFIG_ROCKCHIP_LVDS);
ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_driver, CONFIG_ROCKCHIP_DW_MIPI_DSI);
ADD_ROCKCHIP_SUB_DRIVER(rockchip_rgb_driver, CONFIG_ROCKCHIP_RGB);
// ... HDMI, DP 等
```

**主设备 probe**（[rockchip_drm_drv.c:3570](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L3570)）：
1. 扫描 `platform_bus_type` 找到所有子设备
2. 为每个子设备调用 `component_match_add()`
3. `component_master_add_with_match()` → 触发 `rockchip_drm_bind()`

**主设备 bind**（[rockchip_drm_drv.c:1957](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L1957)）：
1. `drm_dev_alloc()` + 分配 `rockchip_drm_private`
2. **`component_bind_all(dev, drm_dev)`**（[L2154](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L2154)）— 依次调用每个子组件的 `.bind()`
3. VOP2 bind → 创建 CRTCs 和 planes
4. LVDS/DSI/RGB/HDMI bind → 创建 encoders/connectors/bridges
5. fbdev helper 初始化、vblank 初始化、IOMMU 初始化
6. `drm_dev_register()` → 注册到用户空间

### 7.4 VOP2 CRTC 输出到外部 Encoder 的硬件路由

**VOP2 内部分复用器**（[rockchip_drm_vop2.c:4565-4684](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4565)）：

在 `vop2_crtc_atomic_enable()` 中，根据 `rockchip_crtc_state->output_if`（由 encoder 的 `atomic_check` 设置）配置 VOP2 内部 mux：

```c
// 哪个 VP 驱动哪个物理接口:
VOP_CTRL_SET(vop2, rgb_en, 1);   VOP_CTRL_SET(vop2, rgb_mux,   vp_data->id);
VOP_CTRL_SET(vop2, lvds0_en, 1); VOP_CTRL_SET(vop2, lvds0_mux, vp_data->id);
VOP_CTRL_SET(vop2, mipi0_en, 1); VOP_CTRL_SET(vop2, mipi0_mux, vp_data->id);
// ... eDP0, HDMI0 等同理
```

**SoC 级 GRF (General Register File) 路由**（各 encoder 在 enable 时写入）：
- DSI：[dw-mipi-dsi.c:1110-1120](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1110) — `grf_field_write(dsi, VOPSEL, pipe)` 选择哪个 VP 驱动 DSI PHY
- LVDS：SoC 特定（如 RK3568 `rk3568_lvds_enable()`, [rockchip_lvds.c:661](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L661)）
- RGB：SoC 特定（如 PX30 `px30_rgb_enable()`, [rockchip_rgb.c:426](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L426)）

这是**两层路由**：VOP2 内部 mux + SoC 级 GRF mux。

### 7.5 Encoder 的 `atomic_check` — output_if 回传

每个 encoder 的 `atomic_check` 负责设置 `rockchip_crtc_state` 中的 `output_if`/`output_mode`，供 VOP2 `atomic_enable` 使用：

| Encoder | 函数 | 文件:行号 | 设置的 output_if |
|---------|------|-----------|-----------------|
| LVDS | `rockchip_lvds_encoder_atomic_check` | [rockchip_lvds.c:217](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L217) | `VOP_OUTPUT_IF_LVDS0` / `LVDS1`（或 dual） |
| RGB | `rockchip_rgb_encoder_atomic_check` | [rockchip_rgb.c:192](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L192) | `VOP_OUTPUT_IF_RGB` / `BT656` / `BT1120` |
| DSI | `dw_mipi_dsi_encoder_atomic_check` | [dw-mipi-dsi.c:1358](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1358) | `VOP_OUTPUT_IF_MIPI0` / `MIPI1`（或 dual） |

---

## 八、Atomic Modeset 路径中的 Enable/Disable 序列

这是理解 encoder/bridge/connector 在运行时如何交互的核心。

### 8.1 整体顺序

来自 `drm_atomic_helper_commit_tail()`（[drm_atomic_helper.c:1490](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L1490)）：

```
1. drm_atomic_helper_commit_modeset_disables(dev, old_state)
   ├─ disable_outputs():        ← 先关输出
   │    ├─ bridge_disable       (尾→头)
   │    ├─ encoder->disable     (encoder 本身)
   │    ├─ bridge_post_disable  (头→尾)
   │    └─ crtc->atomic_disable (CRTC 最后关)
   ├─ update_legacy_modeset_state()
   └─ crtc_set_mode()           ← 设置新模式参数（时序、encoder mode_set）

2. drm_atomic_helper_commit_planes(dev, old_state, ...)  ← 更新 plane 硬件寄存器

3. drm_atomic_helper_commit_modeset_enables(dev, old_state)
   ├─ 先使能 CRTC（索引顺序）    ← CRTC 先开
   └─ 再使能 encoders/connectors:
        ├─ bridge_pre_enable    (尾→头)
        ├─ encoder->enable      (encoder 本身)
        └─ bridge_enable        (头→尾)
```

### 8.2 为什么是这个顺序？

```
DISABLE 顺序（connector 先于 CRTC）:
  bridge[N].disable → ... → bridge[0].disable   (下游先关, 上游时钟仍在)
  encoder->disable                               (encoder 自身)
  bridge[0].post_disable → ... → bridge[N].post_disable  (时钟已停, 顺序断电能)
  crtc->atomic_disable                           (CRTC 最后关)

ENABLE 顺序（CRTC 先于 connector）:
  crtc->atomic_enable                            (CRTC 先开, 生成时钟/时序)
  bridge[N].pre_enable → ... → bridge[0].pre_enable  (下游先上电准备, 上游信号未到)
  encoder->enable                                (encoder 开始驱动)
  bridge[0].enable → ... → bridge[N].enable      (下游看到稳定信号后开始转发视频)
```

### 8.3 以 encoder → bridge_A → bridge_B (panel-bridge) 为例

```
Enable 序列:
  B.pre_enable  (panel prepare: 上电, 发初始化命令)
  A.pre_enable  (A 芯片上电准备)
  encoder.enable (DSI host 开始发送时钟/数据)
  A.enable       (A 芯片使能转发)
  B.enable       (panel enable: 开背光 → 画面出现)

Disable 序列:
  B.disable      (panel disable: 关背光)
  A.disable      (A 芯片停转发)
  encoder.disable (DSI host 停止发送)
  A.post_disable  (A 芯片断电)
  B.post_disable  (panel unprepare: 发 sleep 命令, 断电)
```

### 8.4 Check 阶段的 Encoder/Connector 验证

来自 `drm_atomic_helper_check_modeset()`（[drm_atomic_helper.c:586](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L586)）：

1. **CRTC 状态检测**（[L596-636](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L596)）：检测 `mode_changed`/`active_changed`/`connectors_changed` 标志
2. **Conflicting encoder 检测**（[L638](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L638)）：`handle_conflicting_encoders()` — 同一 encoder 不能被两个 connector 同时使用
3. **`update_connector_routing()`**（[L652-657](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L652)）：
   - 为每个 connector 选择 `best_encoder`（通过 `atomic_best_encoder` → `best_encoder` → `drm_atomic_helper_best_encoder`（[drm_atomic_helper.c:3624](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L3624)））
   - 如果 encoder 被其他 connector 占用，调用 `steal_encoder()`（[drm_atomic_helper.c:244](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L244)）
   - 通过 `set_best_encoder()`（[drm_atomic_helper.c:203](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L203)）更新 `crtc_state->encoder_mask`
4. **Connector atomic_check**（[L665-671](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L665)）
5. **拉入受影响的 connector 和 plane**（[L679-695](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L679)）
6. **Mode valid/fixup**（[L713-717](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L713)）：
   - `mode_valid_path()`（[drm_atomic_helper.c:479](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L479)）：encoder → bridge 逐级 `mode_valid`
   - `mode_fixup()`（[drm_atomic_helper.c:391](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L391)）：bridge `mode_fixup` → encoder `atomic_check`/`mode_fixup`

---

## 九、Panel 路径 vs Bridge 路径（Rockchip 的双轨设计）

Rockchip 的每个 encoder 驱动（LVDS/DSI/RGB）支持两条路径，**互斥选择**：

```
                      ┌─ Panel 路径 ─────────────────────────┐
                      │  1. encoder 驱动创建 drm_connector     │
drm_of_find_panel     │  2. drm_connector_attach_encoder()     │
_or_bridge(port@1) ──┤  3. drm_panel_attach(panel, connector)  │
                      │  4. 注册 rockchip_drm_sub_dev          │
                      │  (connector->get_modes → panel)        │
                      ├────────────────────────────────────────┤
                      │  1. drm_bridge_attach(encoder, bridge,  │
                      └─ Bridge 路径 ──────────────────────────┘
                         NULL)
                         (bridge 内部自己管 connector, 或用 panel_bridge)
```

**DSI 特殊处理**：当找到 bridge 时，DSI host 指针被传递给 bridge：
```c
dsi->bridge->driver_private = &dsi->host;   // [dw-mipi-dsi.c:1607]
```
这允许下游 bridge 通过 DSI host 接口发送 MIPI DCS 命令。

---

## 十、关键函数速查表

### DRM Core — Encoder

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_encoder_init()` | [drm_encoder.c:106](../../kernel/drivers/gpu/drm/drm_encoder.c#L106) | 分配/初始化 |
| `drm_encoder_register_all()` | [drm_encoder.c:66](../../kernel/drivers/gpu/drm/drm_encoder.c#L66) | 注册 |
| `drm_encoder_unregister_all()` | [drm_encoder.c:81](../../kernel/drivers/gpu/drm/drm_encoder.c#L81) | 注销 |
| `drm_encoder_cleanup()` | [drm_encoder.c:157](../../kernel/drivers/gpu/drm/drm_encoder.c#L157) | 销毁 |
| `drm_encoder_crtc_ok()` | [drm_encoder.h:347](../../kernel/include/drm/drm_encoder.h#L347) | CRTC 兼容性检查 |
| `drm_encoder_find()` | [drm_encoder.h:362](../../kernel/include/drm/drm_encoder.h#L362) | 按 ID 查找 encoder |
| `drm_for_each_encoder()` | [drm_encoder.h:394](../../kernel/include/drm/drm_encoder.h#L394) | 遍历所有 encoder |

### DRM Core — Bridge

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_bridge_add()` | [drm_bridge.c:71](../../kernel/drivers/gpu/drm/drm_bridge.c#L71) | 全局注册 |
| `drm_bridge_remove()` | [drm_bridge.c:84](../../kernel/drivers/gpu/drm/drm_bridge.c#L84) | 全局注销 |
| `drm_bridge_attach()` | [drm_bridge.c:110](../../kernel/drivers/gpu/drm/drm_bridge.c#L110) | 绑定到 encoder |
| `drm_bridge_detach()` | [drm_bridge.c:145](../../kernel/drivers/gpu/drm/drm_bridge.c#L145) | 从 encoder 解绑 |
| `of_drm_find_bridge()` | [drm_bridge.c:361](../../kernel/drivers/gpu/drm/drm_bridge.c#L361) | 按 DT 节点查找 |
| `drm_bridge_chain_disable()` | [drm_bridge.c:246](../../kernel/drivers/gpu/drm/drm_bridge.c#L246) | disable（尾→头） |
| `drm_bridge_chain_post_disable()` | [drm_bridge.c:268](../../kernel/drivers/gpu/drm/drm_bridge.c#L268) | post_disable（头→尾） |
| `drm_bridge_chain_pre_enable()` | [drm_bridge.c:317](../../kernel/drivers/gpu/drm/drm_bridge.c#L317) | pre_enable（尾→头） |
| `drm_bridge_chain_enable()` | [drm_bridge.c:339](../../kernel/drivers/gpu/drm/drm_bridge.c#L339) | enable（头→尾） |
| `drm_bridge_chain_mode_set()` | [drm_bridge.c:292](../../kernel/drivers/gpu/drm/drm_bridge.c#L292) | mode_set（头→尾） |
| `drm_bridge_chain_mode_fixup()` | [drm_bridge.c:185](../../kernel/drivers/gpu/drm/drm_bridge.c#L185) | mode_fixup（头→尾） |
| `drm_bridge_chain_mode_valid()` | [drm_bridge.c:218](../../kernel/drivers/gpu/drm/drm_bridge.c#L218) | mode_valid（头→尾） |

### DRM Core — Connector

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_connector_init()` | [drm_connector.c:192](../../kernel/drivers/gpu/drm/drm_connector.c#L192) | 分配/初始化 |
| `drm_connector_attach_encoder()` | [drm_connector.c:324](../../kernel/drivers/gpu/drm/drm_connector.c#L324) | 关联 encoder |
| `drm_connector_register()` | [drm_connector.c:447](../../kernel/drivers/gpu/drm/drm_connector.c#L447) | 注册（sysfs+debugfs） |
| `drm_connector_unregister()` | [drm_connector.c:494](../../kernel/drivers/gpu/drm/drm_connector.c#L494) | 注销 |
| `drm_connector_cleanup()` | [drm_connector.c:389](../../kernel/drivers/gpu/drm/drm_connector.c#L389) | 销毁 |
| `drm_connector_update_edid_property()` | [drm_connector.c:1436](../../kernel/drivers/gpu/drm/drm_connector.c#L1436) | 更新 EDID |
| `drm_connector_get_encoder()` | [drm_connector.c:1830](../../kernel/drivers/gpu/drm/drm_connector.c#L1830) | 获取当前 encoder（兼容 atomic+legacy） |

### DRM Core — Panel

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_panel_init()` | [drm_panel.c:48](../../kernel/drivers/gpu/drm/drm_panel.c#L48) | 初始化 |
| `drm_panel_add()` | [drm_panel.c:64](../../kernel/drivers/gpu/drm/drm_panel.c#L64) | 全局注册 |
| `drm_panel_remove()` | [drm_panel.c:80](../../kernel/drivers/gpu/drm/drm_panel.c#L80) | 全局注销 |
| `drm_panel_attach()` | [drm_panel.c:103](../../kernel/drivers/gpu/drm/drm_panel.c#L103) | 绑定到 connector |
| `drm_panel_detach()` | [drm_panel.c:127](../../kernel/drivers/gpu/drm/drm_panel.c#L127) | 从 connector 解绑 |
| `of_drm_find_panel()` | [drm_panel.c:151](../../kernel/drivers/gpu/drm/drm_panel.c#L151) | 按 DT 节点查找 |
| `drm_panel_prepare()` | [drm_panel.h:303](../../kernel/include/drm/drm_panel.h#L303) | 上电（inline helper） |
| `drm_panel_enable()` | [drm_panel.h:321](../../kernel/include/drm/drm_panel.h#L321) | 使能/背光（inline helper） |
| `drm_panel_disable()` | [drm_panel.h:285](../../kernel/include/drm/drm_panel.h#L285) | 禁用/关背光（inline helper） |
| `drm_panel_unprepare()` | [drm_panel.h:267](../../kernel/include/drm/drm_panel.h#L267) | 断电（inline helper） |
| `drm_panel_get_modes()` | [drm_panel.h:339](../../kernel/include/drm/drm_panel.h#L339) | 获取模式（inline helper） |
| `drm_panel_bridge_add()` | [bridge/panel.c:156](../../kernel/drivers/gpu/drm/bridge/panel.c#L156) | 包装 panel 为 bridge |
| `devm_drm_panel_bridge_add()` | [bridge/panel.c:213](../../kernel/drivers/gpu/drm/bridge/panel.c#L213) | devm 版本 |

### DRM Core — 全局流程

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_modeset_register_all()` | [drm_mode_config.c:30](../../kernel/drivers/gpu/drm/drm_mode_config.c#L30) | 注册入口 |
| `drm_modeset_unregister_all()` | [drm_mode_config.c:62](../../kernel/drivers/gpu/drm/drm_mode_config.c#L62) | 注销入口 |
| `drm_mode_config_cleanup()` | [drm_mode_config.c:431](../../kernel/drivers/gpu/drm/drm_mode_config.c#L431) | 全局销毁 |
| `drm_mode_config_reset()` | [drm_mode_config.c:176](../../kernel/drivers/gpu/drm/drm_mode_config.c#L176) | 状态重置 |

### DRM Core — Atomic Modeset

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_atomic_helper_check_modeset()` | [drm_atomic_helper.c:586](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L586) | Check 阶段验证 |
| `drm_atomic_helper_commit_modeset_disables()` | [drm_atomic_helper.c:1208](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L1208) | 关闭旧配置 |
| `drm_atomic_helper_commit_modeset_enables()` | [drm_atomic_helper.c:1254](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L1254) | 使能新配置 |
| `disable_outputs()` | [drm_atomic_helper.c:936](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L936) | 禁用的核心逻辑 |
| `update_connector_routing()` | [drm_atomic_helper.c:274](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L274) | 选择 best_encoder |
| `drm_atomic_helper_best_encoder()` | [drm_atomic_helper.c:3624](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L3624) | 默认 encoder 选择 |
| `drm_helper_probe_single_connector_modes()` | [drm_probe_helper.c:387](../../kernel/drivers/gpu/drm/drm_probe_helper.c#L387) | 模式探测+验证 |
| `drm_helper_hpd_irq_event()` | [drm_probe_helper.c:772](../../kernel/drivers/gpu/drm/drm_probe_helper.c#L772) | HPD 中断处理 |

### Rockchip — Init

| 函数 | 文件 |
|------|------|
| `rockchip_drm_bind()` | [rockchip_drm_drv.c:1957](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L1957) |
| `rockchip_lvds_bind()` | [rockchip_lvds.c:369](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L369) |
| `rockchip_rgb_bind()` | [rockchip_rgb.c:276](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L276) |
| `dw_mipi_dsi_bind()` | [dw-mipi-dsi.c:1537](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1537) |
| `vop2_create_crtc()` | [rockchip_drm_vop2.c:6716](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6716) |

### Rockchip — Runtime (Encoder 侧)

| 函数 | 文件 | 说明 |
|------|------|------|
| `rockchip_lvds_encoder_atomic_check` | [rockchip_lvds.c:217](../../kernel/drivers/gpu/drm/rockchip/rockchip_lvds.c#L217) | 设置 output_if=LVDS0/1 |
| `rockchip_rgb_encoder_atomic_check` | [rockchip_rgb.c:192](../../kernel/drivers/gpu/drm/rockchip/rockchip_rgb.c#L192) | 设置 output_if=RGB/BT656/BT1120 |
| `dw_mipi_dsi_encoder_atomic_check` | [dw-mipi-dsi.c:1358](../../kernel/drivers/gpu/drm/rockchip/dw-mipi-dsi.c#L1358) | 设置 output_if=MIPI0/1 |
| `vop2_crtc_atomic_enable` | [rockchip_drm_vop2.c:4565](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4565) | 读 output_if 配置 VOP2 mux |
| `rockchip_drm_sub_dev` 机制 | [rockchip_drm_drv.c:98-115](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L98) | connector 的 DT 节点查找 |

### 示例 Panel 驱动

| 文件 | 说明 |
|------|------|
| [panel-innolux-p079zca.c](../../kernel/drivers/gpu/drm/panel/panel-innolux-p079zca.c) | 完整 4 阶段回调（[L70-224](../../kernel/drivers/gpu/drm/panel/panel-innolux-p079zca.c#L70)）+ get_modes（[L408](../../kernel/drivers/gpu/drm/panel/panel-innolux-p079zca.c#L408)） |
| [panel-simple.c](../../kernel/drivers/gpu/drm/panel/panel-simple.c) | 通用 panel 驱动，多模式支持（[L379](../../kernel/drivers/gpu/drm/panel/panel-simple.c#L379), [L658](../../kernel/drivers/gpu/drm/panel/panel-simple.c#L658)） |

---

## 十一、核心要点总结

1. **四个组件是独立的 KMS 对象**，通过指针/链表/ID 数组相互引用，没有嵌入关系。Encoder→Bridge→Connector→Panel 构成清晰的单向数据流管道。

2. **Bridge 链条是单向链表**（`encoder->bridge → bridge->next → ... → NULL`），每个 bridge 的 `encoder` 指针都指向链首的 encoder。全局 `bridge_list` 和链式 `next` 是**两个独立的链表**。

3. **Bridge 遍历方向决定硬件时序**：disable 和 pre_enable 是**逆向**（connector 侧先动作），post_disable 和 enable 是**正向**（encoder 侧先动作）。这确保了信号和电源的正确时序。

4. **Panel 的生命周期是 prepare→enable→disable→unprepare**：prepare 在信号到来前上电，enable 在信号稳定后开背光；disable 在信号消失前关背光，unprepare 在信号停止后断电。

5. **Panel-Bridge 是透明适配器**：将 panel 回调映射到 bridge 回调（prepare→pre_enable, enable→enable, disable→disable, unprepare→post_disable），本质上是语义的精确对位。

6. **Rockchip 支持 Panel 路径和 Bridge 路径**，通过 `drm_of_find_panel_or_bridge()` 自动选择。Panel 路径下 encoder 驱动自己创建 connector；Bridge 路径下 bridge 内部处理 connector。

7. **Modeset 的顺序是精准不可变的**：disable 时 connector/bridge/encoder 先于 CRTC（输出先关，时钟后停）；enable 时 CRTC 先于 connector/bridge/encoder（时钟先开，输出后开）。

8. **`best_encoder` 是 atomic 模式的核心路由字段**：存储在 `connector_state->best_encoder`，由 `update_connector_routing()` 选择。Legacy 驱动使用 `connector->encoder`，但 atomic 驱动永远不用。

9. **Encoder→CRTC 的连线由 OF Graph 决定**：`drm_of_find_possible_crtcs()` 顺着设备树的 port@endpoint 拓扑反向查找到 VOP2 的 video port，返回位掩码。

10. **VOP2 的硬件路由有两层**：VOP2 内部 mux（`VOP_CTRL_SET(vop2, lvds0_mux, ...)`）+ SoC 级 GRF mux（各 encoder enable 时写 GRF 寄存器）。两层共同决定哪个 VP 的像素流路由到哪个物理接口。
