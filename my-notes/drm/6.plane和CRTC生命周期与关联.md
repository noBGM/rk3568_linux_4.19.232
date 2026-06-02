# DRM Plane 与 CRTC 的生命周期与关联

> 基于 Linux DRM 核心框架 + Rockchip VOP2 驱动实例，每个关键函数标注可跳转的文件地址。

---

## 一、结构体定义与关联字段

### 1.1 `struct drm_plane` — 图层对象

**定义位置:** [drm_plane.h:584](../../kernel/include/drm/drm_plane.h#L584)

关键字段（plane→CRTC 方向）：

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `possible_crtcs` | `uint32_t` | [drm_plane.h:658](../../kernel/include/drm/drm_plane.h#L658) | **静态能力掩码**：此 plane 能连接到哪些 CRTC（init 时设定，不变） |
| `crtc` | `struct drm_crtc *` | [drm_plane.h:712](../../kernel/include/drm/drm_plane.h#L712) | **Legacy only**：当前绑定的 CRTC。atomic 驱动下此字段为 NULL |
| `fb` | `struct drm_framebuffer *` | [drm_plane.h:722](../../kernel/include/drm/drm_plane.h#L722) | **Legacy only**：当前绑定的 framebuffer。atomic 驱动下为 NULL |
| `state` | `struct drm_plane_state *` | [drm_plane.h:830](../../kernel/include/drm/drm_plane.h#L830) | 指向当前 atomic 状态快照；`state->crtc` 持有运行时的 CRTC 绑定 |
| `type` | `enum drm_plane_type` | [drm_plane.h:779](../../kernel/include/drm/drm_plane.h#L779) | PRIMARY / OVERLAY / CURSOR |
| `index` | `unsigned int` | [drm_plane.h:789](../../kernel/include/drm/drm_plane.h#L789) | 全局 plane_list 中的位置索引（不变） |
| `head` | `struct list_head` | [drm_plane.h:600](../../kernel/include/drm/drm_plane.h#L600) | mode_config.plane_list 链表节点 |

### 1.2 `struct drm_plane_state` — 图层运行时状态

**定义位置:** [drm_plane.h:44](../../kernel/include/drm/drm_plane.h#L44)

| 字段 | 位置 | 含义 |
|------|------|------|
| `crtc` | [drm_plane.h:54](../../kernel/include/drm/drm_plane.h#L54) | **atomic 运行时的 CRTC 绑定**。NULL = plane 未连接到任何 CRTC |
| `fb` | [drm_plane.h:62](../../kernel/include/drm/drm_plane.h#L62) | 当前 framebuffer |
| `src_x/y/w/h` | — | 源裁剪区（16.16 定点数） |
| `crtc_x/y/w/h` | — | 目标显示区域 |
| `visible` | — | plane 是否可见（由 helper 裁剪后设置） |
| `commit` | — | 指向关联的 `drm_crtc_commit`，用于非阻塞提交的引用跟踪 |

### 1.3 `struct drm_crtc` — 显示管道对象

**定义位置:** [drm_crtc.h:947](../../kernel/include/drm/drm_crtc.h#L947)

CRTC→Plane 方向的关键字段：

| 字段 | 类型 | 位置 | 含义 |
|------|------|------|------|
| `primary` | `struct drm_plane *` | [drm_crtc.h:1023](../../kernel/include/drm/drm_crtc.h#L1023) | **主图层指针**（legacy SETCRTC/PAGE_FLIP 使用） |
| `cursor` | `struct drm_plane *` | [drm_crtc.h:1033](../../kernel/include/drm/drm_crtc.h#L1033) | **光标图层指针**（legacy CURSOR ioctl 使用） |
| `index` | `unsigned int` | — | crtc_list 中的位置索引 |
| `head` | `struct list_head` | — | mode_config.crtc_list 链表节点 |

### 1.4 `struct drm_crtc_state` — CRTC 运行时状态

**定义位置:** [drm_crtc.h:98](../../kernel/include/drm/drm_crtc.h#L98)

| 字段 | 位置 | 含义 |
|------|------|------|
| `plane_mask` | [drm_crtc.h:199](../../kernel/include/drm/drm_crtc.h#L199) | **派生字段**：bit N = 1 表示 plane[N] 连接到本 CRTC |
| `planes_changed : 1` | [drm_crtc.h:128](../../kernel/include/drm/drm_crtc.h#L128) | 是否有 plane 变更（指导 commit 流程） |
| `active` / `enable` | — | CRTC 是否激活/使能 |
| `mode_changed` / `active_changed` | — | 是否需要 full modeset |

### 1.5 Rockchip VOP2 关键结构体

**文件:** [rockchip_drm_vop2.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c)

| 结构体 | 位置 | 说明 |
|--------|------|------|
| `struct vop2_win` | [vop2.c:272](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L272) | 硬件图层窗口，**内嵌** `struct drm_plane base`；包含 phys_id、vp_mask、zpos 等硬件参数 |
| `struct vop2_video_port` | [vop2.c:412](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L412) | 视频端口/CRTC，**内嵌** `struct drm_crtc crtc`；包含 win_mask、plane_mask 等 |
| `struct vop2` | [vop2.c:560](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L560) | 顶层 VOP2 设备，包含 `vps[4]` 数组和 `win[]` 动态数组 |
| `struct vop2_win_data` | [rockchip_drm_vop.h:651](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop.h#L651) | 静态硬件描述符（编译时配置） |

辅助宏（[vop2.c:126](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L126)）：
```c
#define to_vop2_video_port(c)  container_of(c, struct vop2_video_port, crtc)
#define to_vop2_win(x)         container_of(x, struct vop2_win, base)
#define to_vop2_plane_state(x) container_of(x, struct vop2_plane_state, base)
```

---

## 二、Plane-CRTC 关联的两种模式

### 核心设计原则

```
┌──────────────────────────────────────────────────┐
│  Legacy 模式：                                    │
│    plane->crtc   ← 直接写 CRTC 指针                │
│    plane->fb     ← 直接写 FB 指针                  │
│    crtc->primary ← 固定指向主图层                   │
│    crtc->cursor  ← 固定指向光标图层                  │
├──────────────────────────────────────────────────┤
│  Atomic 模式：                                    │
│    plane->crtc   = NULL  (总是 NULL !)             │
│    plane->fb     = NULL                            │
│    plane->state->crtc ← 运行时的 CRTC 绑定          │
│    crtc_state->plane_mask ← 派生位掩码              │
│    crtc->primary / crtc->cursor ← 仅供 legacy IOCTL │
└──────────────────────────────────────────────────┘
```

**VOP2 是 atomic 驱动**，因此所有绑定都通过 `plane_state->crtc` 和 `crtc_state->plane_mask`。

---

## 三、Plane 的完整生命周期

### 阶段全景图

```
分配/初始化 → 状态重置 → 延迟注册 → 运行时操作 → 提前注销 → 销毁/清理
```

### 3.1 分配与初始化

#### `drm_universal_plane_init()` — 核心初始化函数

**文件:** [drm_plane.c:164](../../kernel/drivers/gpu/drm/drm_plane.c#L164)

```c
int drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
                             uint32_t possible_crtcs,
                             const struct drm_plane_funcs *funcs,
                             const uint32_t *formats, unsigned int format_count,
                             const uint64_t *format_modifiers,
                             enum drm_plane_type type,
                             const char *name, ...);
```

**内部步骤：**

1. 限制全局 plane 总数 ≤ 32（[L177](../../kernel/drivers/gpu/drm/drm_plane.c#L177)）
2. 注册为 KMS mode object：`drm_mode_object_add()`（[L184](../../kernel/drivers/gpu/drm/drm_plane.c#L184)）
3. 初始化 modeset 锁 mutex（[L188](../../kernel/drivers/gpu/drm/drm_plane.c#L188)）
4. 设置 `plane->dev`、`plane->funcs`、复制格式/修饰符数组（[L190-251](../../kernel/drivers/gpu/drm/drm_plane.c#L190)）
5. 设置 `plane->possible_crtcs` 和 `plane->type`（[L250](../../kernel/drivers/gpu/drm/drm_plane.c#L250)）
6. 插入 `config->plane_list`，分配 `plane->index`（[L253](../../kernel/drivers/gpu/drm/drm_plane.c#L253)）
7. 附加"type"不可变属性（[L256](../../kernel/drivers/gpu/drm/drm_plane.c#L256)）
8. 对于 atomic 驱动，附加标准属性：`CRTC_ID`、`FB_ID`、`SRC_X/Y/W/H`、`CRTC_X/Y/W/H`、`IN_FENCE_FD`（[L260-272](../../kernel/drivers/gpu/drm/drm_plane.c#L260)）
9. 如支持 modifier，创建 IN_FORMATS blob 属性（[L274](../../kernel/drivers/gpu/drm/drm_plane.c#L274)）

#### `drm_plane_init()` — Legacy 包装函数（已废弃）

**文件:** [drm_plane.c:323](../../kernel/drivers/gpu/drm/drm_plane.c#L323)

仅将 `is_primary` bool 转换为 plane type，然后调用 `drm_universal_plane_init()`。新驱动不应使用。

#### VOP2 中的 Plane 初始化：`vop2_plane_init()`

**文件:** [rockchip_drm_vop2.c:6494](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6494)

```c
static int vop2_plane_init(struct vop2 *vop2, struct vop2_win *win,
                           unsigned long possible_crtcs)
```

调用链：
```
vop2_plane_init()                              [vop2.c:6494]
  ├─ drm_universal_plane_init(dev, &win->base,  [vop2.c:6518]
  │      possible_crtcs, &vop2_plane_funcs,
  │      win->formats, win->nformats,
  │      win->format_modifiers, win->type, win->name)
  ├─ drm_plane_helper_add(&win->base,            [vop2.c:6526]
  │      &vop2_plane_helper_funcs)
  └─ 附加 Rockchip 自定义属性                  [vop2.c:6528]
      (eotf, color_space, async_commit, share_id, zpos, rotation, alpha, blend_mode)
```

`vop2_plane_funcs`（[vop2.c:3810](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3810)）：
```c
static const struct drm_plane_funcs vop2_plane_funcs = {
    .update_plane           = rockchip_atomic_helper_update_plane,
    .disable_plane          = rockchip_atomic_helper_disable_plane,
    .destroy                = vop2_plane_destroy,
    .reset                  = vop2_atomic_plane_reset,
    .atomic_duplicate_state = vop2_atomic_plane_duplicate_state,
    .atomic_destroy_state   = vop2_atomic_plane_destroy_state,
    .atomic_set_property    = vop2_atomic_plane_set_property,
    .atomic_get_property    = vop2_atomic_plane_get_property,
    .format_mod_supported   = rockchip_vop2_mod_supported,
};
```

### 3.2 状态重置

#### `drm_mode_config_reset()`

**文件:** [drm_mode_config.c:176](../../kernel/drivers/gpu/drm/drm_mode_config.c#L176)

驱动加载和 GPU reset/resume 时调用。遍历所有 objects：
1. **先遍历所有 plane** → `plane->funcs->reset(plane)`（[L184-186](../../kernel/drivers/gpu/drm/drm_mode_config.c#L184)）
2. 再遍历所有 CRTC → `crtc->funcs->reset(crtc)`（[L189](../../kernel/drivers/gpu/drm/drm_mode_config.c#L189)）
3. 然后 encoder、connector

对于 atomic 驱动，`reset` 回调通常指向 `drm_atomic_helper_plane_reset()` / `drm_atomic_helper_crtc_reset()`。

Rockchip 调用位置：[rockchip_drm_drv.c:2183](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L2183)。

#### VOP2 的 reset 回调

`vop2_atomic_plane_reset`（[vop2.c:3785](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3785)）：分配 `struct vop2_plane_state`（包含扩展的 `drm_plane_state`），初始化硬件参数为默认值。

### 3.3 注册（late_register）

#### `drm_modeset_register_all()`

**文件:** [drm_mode_config.c:30](../../kernel/drivers/gpu/drm/drm_mode_config.c#L30)

调用自 `drm_dev_register()`（[drm_drv.c:957](../../kernel/drivers/gpu/drm/drm_drv.c#L957)）。注册顺序：
1. `drm_plane_register_all(dev)` — 为每个 plane 调用 `late_register`（[drm_plane.c:281](../../kernel/drivers/gpu/drm/drm_plane.c#L281)）
2. `drm_crtc_register_all(dev)` — 为每个 CRTC 调用 `late_register` + 创建 debugfs（[drm_crtc.c:157](../../kernel/drivers/gpu/drm/drm_crtc.c#L157)）
3. encoder → connector

**Plane 先于 CRTC 注册**，这是有意的依赖顺序。

### 3.4 运行时操作

#### 帧缓冲引用管理

- **设置 FB**：[`drm_atomic_set_fb_for_plane()`](../../kernel/drivers/gpu/drm/drm_atomic.c#L1659)（[drm_atomic.c:1659](../../kernel/drivers/gpu/drm/drm_atomic.c#L1659)）→ `drm_framebuffer_assign()` 处理引用计数
- **Pin FB**：[`drm_atomic_helper_prepare_planes()`](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2277)（[drm_atomic_helper.c:2277](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2277)）→ 驱动 `prepare_fb()` callback
- **Unpin FB**：[`drm_atomic_helper_cleanup_planes()`](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2567)（[drm_atomic_helper.c:2567](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2567)）→ 驱动 `cleanup_fb()` callback

#### VOP2 Plane 运行时回调

**`vop2_plane_helper_funcs`**（[vop2.c:3549](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3549)）：
```c
static const struct drm_plane_helper_funcs vop2_plane_helper_funcs = {
    .atomic_check   = vop2_plane_atomic_check,
    .atomic_update  = vop2_plane_atomic_update,
    .atomic_disable = vop2_plane_atomic_disable,
};
```

**`vop2_plane_atomic_check()`**（[vop2.c:3051](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3051)）：
- 从 `state->crtc` 获取目标 CRTC（[L3069](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3069)）
- 若无 CRTC 或无 FB → 标记 `visible = false`，返回 0
- 调用 [`drm_atomic_helper_check_plane_state()`](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L722) 验证裁剪、缩放限制（[L3093](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3093)）
- 验证 AFBC 格式限制、输入尺寸限制（[L3132-3156](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3132)）
- **预计算 DMA 地址**（`yrgb_mst`, `uv_mst`）存储在 `vpstate` 中（[L3180-3196](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3180)）

**`vop2_plane_atomic_update()`**（[vop2.c:3281](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3281)）：
- 从 `pstate->crtc` 知道自己在哪个 CRTC 上（[L3284](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3284)）
- 在 `spin_lock(&vop2->reg_lock)` 下写硬件寄存器（[L3409-3516](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3409)）
- 配置：格式、stride、DMA 地址、位置、缩放、CSC、AFBC、颜色键
- 最后设置 `VOP_WIN_SET(vop2, win, enable, 1)` 使能窗口（[L3507](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3507)）
- **不调用 `vop2_cfg_done()`** — 这留给 `atomic_flush` 统一触发

**`vop2_plane_atomic_disable()`**（[vop2.c:3201](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3201)）：
- 从 `old_state->crtc` 知道之前在哪（[L3207](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3207)）
- 调用 `vop2_win_disable(win)` 关闭窗口，清除 yuv_clip

### 3.5 销毁与清理

#### `drm_mode_config_cleanup()`

**文件:** [drm_mode_config.c:431](../../kernel/drivers/gpu/drm/drm_mode_config.c#L431)

销毁顺序（与 init 相反）：
1. Encoder → Connector → Property
2. **Plane**：遍历 `plane_list`，调用 `plane->funcs->destroy(plane)`（[L470-473](../../kernel/drivers/gpu/drm/drm_mode_config.c#L470)）
3. **CRTC**：遍历 `crtc_list`，调用 `crtc->funcs->destroy(crtc)`（[L475-477](../../kernel/drivers/gpu/drm/drm_mode_config.c#L475)）
4. Property blob → Framebuffer → IDR/IDA

#### `drm_plane_cleanup()`

**文件:** [drm_plane.c:346](../../kernel/drivers/gpu/drm/drm_plane.c#L346)

由驱动的 `->destroy` 回调调用：
1. 销毁 modeset 锁（[L350](../../kernel/drivers/gpu/drm/drm_plane.c#L350)）
2. 释放 `format_types` 和 `modifiers`（[L352-353](../../kernel/drivers/gpu/drm/drm_plane.c#L352)）
3. 注销 KMS object：`drm_mode_object_unregister()`（[L354](../../kernel/drivers/gpu/drm/drm_plane.c#L354)）
4. 从 `plane_list` 移除（[L363-364](../../kernel/drivers/gpu/drm/drm_plane.c#L363)）
5. 销毁 atomic state（[L366-368](../../kernel/drivers/gpu/drm/drm_plane.c#L366)）
6. 释放 name（[L370](../../kernel/drivers/gpu/drm/drm_plane.c#L370)）
7. **memset 清零整个 struct**，防止 use-after-free（[L372](../../kernel/drivers/gpu/drm/drm_plane.c#L372)）

#### 注销（Unregister）

##### `drm_modeset_unregister_all()`

**文件:** [drm_mode_config.c:62](../../kernel/drivers/gpu/drm/drm_mode_config.c#L62)

调用自 `drm_dev_unregister()`，注销顺序：connector → encoder → CRTC → **plane 最后**（与注册顺序相反）。

---

## 四、CRTC 的完整生命周期

### 4.1 分配与初始化

#### `drm_crtc_init_with_planes()` — 核心初始化函数

**文件:** [drm_crtc.c:266](../../kernel/drivers/gpu/drm/drm_crtc.c#L266)

```c
int drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
                              struct drm_plane *primary,
                              struct drm_plane *cursor,
                              const struct drm_crtc_funcs *funcs,
                              const char *name, ...);
```

**内部步骤（执行顺序）：**

1. 验证 primary 是 PRIMARY 类型，cursor 是 CURSOR 类型（[L275-276](../../kernel/drivers/gpu/drm/drm_crtc.c#L275)）
2. CRTC 数量上限 32（32-bit 位掩码限制）（[L276-278](../../kernel/drivers/gpu/drm/drm_crtc.c#L276)）
3. atomic 驱动必须有 `atomic_destroy_state` 和 `atomic_duplicate_state`（[L280-284](../../kernel/drivers/gpu/drm/drm_crtc.c#L280)）
4. 设置 `crtc->dev` 和 `crtc->funcs`（[L286-287](../../kernel/drivers/gpu/drm/drm_crtc.c#L286)）
5. 初始化 commit 跟踪：`commit_list`、`commit_lock`（[L289-290](../../kernel/drivers/gpu/drm/drm_crtc.c#L289)）
6. 初始化 modeset 锁 mutex（[L292](../../kernel/drivers/gpu/drm/drm_crtc.c#L292)）
7. 注册为 KMS mode object（[L293](../../kernel/drivers/gpu/drm/drm_crtc.c#L293)）
8. 格式化名称（[L297-310](../../kernel/drivers/gpu/drm/drm_crtc.c#L297)）
9. 分配 fence context（[L312-315](../../kernel/drivers/gpu/drm/drm_crtc.c#L312)）
10. 插入 `crtc_list`，分配 `crtc->index`（[L319-320](../../kernel/drivers/gpu/drm/drm_crtc.c#L319)）
11. **绑定 primary / cursor plane**（[L322-327](../../kernel/drivers/gpu/drm/drm_crtc.c#L322)）：
    ```c
    crtc->primary = primary;
    crtc->cursor  = cursor;
    if (primary && !primary->possible_crtcs)
        primary->possible_crtcs = drm_crtc_mask(crtc);
    if (cursor && !cursor->possible_crtcs)
        cursor->possible_crtcs = drm_crtc_mask(crtc);
    ```
    只有当 plane 的 `possible_crtcs` 尚未设置时（=0），才自动填入当前 CRTC 的掩码。
12. 初始化 CRC 捕获（[L329](../../kernel/drivers/gpu/drm/drm_crtc.c#L329)）
13. 附加 atomic 属性：ACTIVE、MODE_ID、OUT_FENCE_PTR（[L335-340](../../kernel/drivers/gpu/drm/drm_crtc.c#L335)）

#### `drm_crtc_init()` — Legacy 包装函数（已废弃）

**文件:** [drm_modeset_helper.c:152](../../kernel/drivers/gpu/drm/drm_modeset_helper.c#L152)

内部创建通用 primary plane（安全子集格式：XRGB8888/ARGB8888/RGB565），然后调用 `drm_crtc_init_with_planes()`。新驱动不应使用。

#### VOP2 中的 CRTC 初始化

**文件:** [rockchip_drm_vop2.c:6859](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6859)

```c
ret = drm_crtc_init_with_planes(drm_dev, crtc, plane, cursor,
                                 &vop2_crtc_funcs, "video_port%d", vp->id);
```

其中 `crtc = &vp->crtc`（嵌入在 `vop2_video_port` 中的 `drm_crtc`）。`vop2_crtc_funcs`（[vop2.c:6196](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6196)）包含：`gamma_set`, `set_config`, `page_flip`, `destroy`, `reset`, `atomic_duplicate_state`, `atomic_destroy_state`, `enable_vblank`, `disable_vblank`, `set_crc_source`, `verify_crc_source`。之后调用 `drm_crtc_helper_add()`（[L6866](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6866)）。

**`vop2_crtc_helper_funcs`**（[vop2.c:5868](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5868)）：
```c
static const struct drm_crtc_helper_funcs vop2_crtc_helper_funcs = {
    .mode_fixup    = vop2_crtc_mode_fixup,
    .atomic_check  = vop2_crtc_atomic_check,     // 空函数！vop2.c:4825
    .atomic_begin  = vop2_crtc_atomic_begin,     // ★ 核心编排函数
    .atomic_flush  = vop2_crtc_atomic_flush,
    .atomic_enable = vop2_crtc_atomic_enable,
    .atomic_disable = vop2_crtc_atomic_disable,
};
```

#### `vop2_crtc_atomic_begin()` — 最关键的 plane-CRTC 交互函数

**文件:** [rockchip_drm_vop2.c:5390](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5390)

在 [`drm_atomic_helper_commit_planes()`](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2361) 中，调用顺序是：
```
for each CRTC:
    atomic_begin(crtc)    ← 先调用
    for each plane on CRTC:
        atomic_update(plane) 或 atomic_disable(plane)
    atomic_flush(crtc)    ← 最后调用
```

`atomic_begin` 对 CRTC 的所有 plane 做**三次遍历**：

**Pass 1**（[L5471-5491](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5471)）：配置 Cluster 双窗口模式
- 遍历 `drm_atomic_crtc_for_each_plane(plane, crtc)`
- 对 CLUSTER_SUB 窗口找配对 main，设置 `two_win_mode`

**Pass 2**（[L5531-5561](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5531)）：将 plane 分配到 VP，收集 zpos
- 将每个 win 从旧 VP 解绑、绑定到当前 VP
- 更新 `vp->win_mask` 和 `win->vp_mask`
- 收集 `vop2_zpos[]` 数组、计数 `nr_layers`

**Pass 2 和 3 之间**（[L5570-5615](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5570)）：排序和硬件配置
- 按 zpos 升序排序
- [`vop2_setup_layer_mixer_for_vp()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5235) — 写 OVL_LAYER_SEL 寄存器
- [`vop2_setup_hdr10()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5593) — HDR tone mapping
- [`vop2_setup_alpha()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5099) — 逐层 alpha 混合
- [`vop2_setup_dly_for_vp()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5325) / [`vop2_setup_dly_for_window()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5358) — pipeline 延迟补偿

**Pass 3**（[L5630-5640](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5630)）：单窗口 Cluster alpha 处理

Primary plane 和 overlay plane 在此函数中**没有区别对待** — 所有 plane 一起参与 zpos 排序和 layer mixer 分配。

#### `vop2_crtc_atomic_flush()` — 硬件锁存触发器

**文件:** [rockchip_drm_vop2.c:5775](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5775)

核心操作：
1. `vop2_cfg_update()` — 写 overlay_mode、背景色、TV 配置（[L5786](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5786)）
2. IOMMU 处理（[L5788-5801](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5788)）— 失败时调用 [`vop2_disable_all_planes_for_crtc()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L2090)
3. Gamma/Cubic LUT 更新（[L5804-5818](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5804)）
4. **`vop2_cfg_done(crtc)`**（[L5827](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5827)）— **硬件锁存触发器**！
5. `vop2_wait_for_irq_handler()`（[L5836](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5836)）
6. 移动 page flip event 到 `vp->event`（[L5844-5852](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5844)）
7. 将旧 framebuffer 加入延迟释放队列 `vp->fb_unref_work`（[L5854-5865](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5854)）

#### `vop2_crtc_atomic_enable()` — CRTC 使能

**文件:** [rockchip_drm_vop2.c:4565](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4565)

- 只配置 CRTC/VP 级硬件：输出接口、时序参数、DCLK 分频
- **不遍历 plane**，不编程任何 plane 寄存器
- 调用 `vop2_cfg_done(crtc)` 锁存时序配置（[L4790](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4790)）
- 调用 `VOP_MODULE_SET(vop2, vp, standby, 0)` 释放 VP（[L4807](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4807)）
- 调用 `drm_crtc_vblank_on(crtc)` 使能 vblank 中断（[L4809](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4809)）

#### `vop2_crtc_atomic_disable()` — CRTC 关闭

**文件:** [rockchip_drm_vop2.c:3001](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3001)

- 调用 `drm_crtc_vblank_off(crtc)`（[L3011](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3011)）
- **调用 [`vop2_disable_all_planes_for_crtc(crtc)`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L2090)**（[L3012](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3012)）— 关闭 CRTC 前先关闭所有 plane
- 设置 VP 为 standby，等待 `dsp_hold` 完成（[L3021-3034](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3021)）

#### `vop2_disable_all_planes_for_crtc()` — CRTC 关闭时强制关闭所有 plane

**文件:** [rockchip_drm_vop2.c:2090](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L2090)

使用 `vp->win_mask`（在 `atomic_begin` 中维护）来确定哪些物理窗口属于此 VP：
```c
for_each_set_bit(phys_id, &win_mask, ROCKCHIP_MAX_LAYER) {
    win = vop2_find_win_by_phys_id(vop2, phys_id);
    vop2_win_disable(win);
}
vop2_cfg_done(crtc);
// poll 等待所有窗口确认关闭（最长 500ms）
```

### 4.2 CRTC 销毁

#### `drm_crtc_cleanup()`

**文件:** [drm_crtc.c:354](../../kernel/drivers/gpu/drm/drm_crtc.c#L354)

由驱动的 `->destroy` 回调调用，执行：
1. 释放 CRC 资源（[L363](../../kernel/drivers/gpu/drm/drm_crtc.c#L363)）
2. 释放 gamma_store（[L365-366](../../kernel/drivers/gpu/drm/drm_crtc.c#L365)）
3. 销毁 modeset 锁（[L368](../../kernel/drivers/gpu/drm/drm_crtc.c#L368)）
4. 注销 KMS object（[L370](../../kernel/drivers/gpu/drm/drm_crtc.c#L370)）
5. 从 crtc_list 移除（[L371](../../kernel/drivers/gpu/drm/drm_crtc.c#L371)）
6. 销毁 atomic state（[L374-376](../../kernel/drivers/gpu/drm/drm_crtc.c#L374)）
7. 释放 name（[L378](../../kernel/drivers/gpu/drm/drm_crtc.c#L378)）
8. memset 清零（[L380](../../kernel/drivers/gpu/drm/drm_crtc.c#L380)）

---

## 五、VOP2 初始化完整流程

### 总览

```
rockchip_drm_bind()                              [rockchip_drm_drv.c]
  └─ component_bind_all()
       └─ vop2_bind(dev, master, drm_dev)        [vop2.c:7083]
            ├─ of_device_get_match_data()         → 获取 SoC 静态数据
            ├─ vop2_win_init(vop2)               [vop2.c:6955]
            │    └─ 从 vop2_data->win[] 填充运行时 win[] 数组
            ├─ 解析 DT "ports" 节点：
            │    rockchip,plane-mask
            │    rockchip,primary-plane           → vp->primary_plane_phy_id
            ├─ 映射 IO、获取 clocks、IRQ
            ├─ vop2_create_crtc(vop2)            [vop2.c:6716]  ★ 核心
            │    ├─ for each VP:
            │    │    ├─ 找/分配 primary win
            │    │    ├─ vop2_plane_init()        [vop2.c:6494]
            │    │    │    └─ drm_universal_plane_init()  [vop2.c:6518]
            │    │    ├─ vop2_cursor_plane_init() [vop2.c:6593]  (可选)
            │    │    ├─ drm_crtc_init_with_planes()  [vop2.c:6859]  ★
            │    │    │    └─ crtc->primary = plane
            │    │    │    └─ plane->possible_crtcs = drm_crtc_mask(crtc)
            │    │    └─ drm_crtc_helper_add()
            │    ├─ 降级未使用的 PRIMARY → OVERLAY  [vop2.c:6894]
            │    └─ for each overlay win:
            │         └─ vop2_plane_init()        [vop2.c:6933]
            ├─ vop2_gamma_init()、vop2_wb_connector_init()
            └─ pm_runtime_enable()
```

### 关键顺序约束

1. **Plane 先于 CRTC 初始化**（`vop2_plane_init` → `drm_crtc_init_with_planes`）
2. **Primary plane 先于 overlay plane**（在创建 CRTC 时先绑定 primary）
3. **未用的 PRIMARY 窗口降级为 OVERLAY**（[vop2.c:6894](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6894)，发生在所有 primary 分配完成后）
4. **Overlay plane 最后创建**（此时所有 CRTC 已注册，可以进行 `possible_crtcs` 分配）

### RK3568 静态 win_data

**文件:** [rockchip_vop2_reg.c:1006](../../kernel/drivers/gpu/drm/rockchip/rockchip_vop2_reg.c#L1006)

`rk3568_vop_win_data[]` 数组有 8 个条目：

| 名称 | phys_id | 静态类型 | 特性 |
|------|---------|----------|------|
| Smart0-win0 | SMART0 | PRIMARY | MULTI_AREA |
| Smart1-win0 | SMART1 | PRIMARY | MIRROR, MULTI_AREA |
| Esmart1-win0 | ESMART1 | PRIMARY | MIRROR, MULTI_AREA |
| Esmart0-win0 | ESMART0 | OVERLAY | MULTI_AREA |
| Cluster0-win0 | CLUSTER0 | OVERLAY | AFBDC, CLUSTER_MAIN |
| Cluster0-win1 | CLUSTER0 | OVERLAY | AFBDC, CLUSTER_SUB |
| Cluster1-win0 | CLUSTER1 | OVERLAY | AFBDC, CLUSTER_MAIN |
| Cluster1-win1 | CLUSTER1 | OVERLAY | AFBDC, CLUSTER_SUB |

---

## 六、Atomic Commit 路径中的 Plane-CRTC 交互

### 6.1 `drm_atomic_set_crtc_for_plane()` — 唯一的绑定修改函数

**文件:** [drm_atomic.c:1609](../../kernel/drivers/gpu/drm/drm_atomic.c#L1609)

```c
int drm_atomic_set_crtc_for_plane(struct drm_plane_state *plane_state,
                                  struct drm_crtc *crtc);
```

**执行逻辑：**

1. 相同 CRTC → 无操作返回（[L1616](../../kernel/drivers/gpu/drm/drm_atomic.c#L1616)）
2. 从旧 CRTC **解绑**（[L1618-1625](../../kernel/drivers/gpu/drm/drm_atomic.c#L1618)）：`crtc_state->plane_mask &= ~drm_plane_mask(plane)`
3. 设置 `plane_state->crtc = crtc`（[L1627](../../kernel/drivers/gpu/drm/drm_atomic.c#L1627)）
4. 绑定到新 CRTC（[L1629-1635](../../kernel/drivers/gpu/drm/drm_atomic.c#L1629)）：`crtc_state->plane_mask |= drm_plane_mask(plane)`

**重要副作用**：调用 `drm_atomic_get_crtc_state()` 会将新旧 CRTC 都拉入 atomic 事务中 — 这是级联依赖。

**触发点**：用户空间通过 atomic ioctl 设置 `CRTC_ID` 属性 → [`drm_atomic_plane_set_property()`](../../kernel/drivers/gpu/drm/drm_atomic.c#L892)。

### 6.2 Core 层面的验证

#### `drm_atomic_plane_check()`

**文件:** [drm_atomic.c:1036](../../kernel/drivers/gpu/drm/drm_atomic.c#L1036)

强制的不变量：

1. **CRTC 和 FB 必须同时为 NULL 或同时非 NULL**（[L1043-1051](../../kernel/drivers/gpu/drm/drm_atomic.c#L1043)）
2. `possible_crtcs` 掩码必须包含目标 CRTC（[L1058-1062](../../kernel/drivers/gpu/drm/drm_atomic.c#L1058)）
3. 像素格式必须支持（[L1066](../../kernel/drivers/gpu/drm/drm_atomic.c#L1066)）
4. 坐标不能溢出（[L1079-1088](../../kernel/drivers/gpu/drm/drm_atomic.c#L1079)）
5. **禁止 plane 在活跃时直接切换 CRTC**（[L1109-1113](../../kernel/drivers/gpu/drm/drm_atomic.c#L1109)）— 必须经过中间禁用状态

最后的 `plane_switching_crtc()` 检查（[drm_atomic.c:1007](../../kernel/drivers/gpu/drm/drm_atomic.c#L1007)）：
```c
// old_state->crtc 和 new_state->crtc 不能同时非 NULL 且不同
```

### 6.3 完整的 Commit 阶段

```
Userspace ioctl (DRM_IOCTL_MODE_ATOMIC)
  │
  ├─ 阶段A: 状态组装
  │   drm_atomic_get_plane_state()             [drm_atomic.c:807]
  │   ├─ duplicate: plane->funcs->atomic_duplicate_state()
  │   ├─ old_state = plane->state (当前的)
  │   ├─ new_state = 克隆的状态 (目标的)
  │   └─ 如果 new_state->crtc 已设置，auto-pull CRTC state
  │
  ├─ 阶段B: 核心验证
  │   drm_atomic_check_only()                  [drm_atomic.c:1950]
  │   ├─ drm_atomic_plane_check()              [drm_atomic.c:1036]
  │   ├─ drm_atomic_crtc_check()
  │   └─ config->funcs->atomic_check()         → 驱动层检查
  │       ├─ drm_atomic_helper_check_modeset()  [drm_atomic_helper.c:909]
  │       └─ drm_atomic_helper_check_planes()   [drm_atomic_helper.c:833]
  │           └─ vop2_crtc_atomic_check()       [vop2.c:4825] (空函数)
  │           └─ vop2_plane_atomic_check()      [vop2.c:3051]
  │
  ├─ 阶段C: Commit 设置
  │   drm_atomic_helper_setup_commit()         [drm_atomic_helper.c:1959]
  │   ├─ 为每个 CRTC 分配 drm_crtc_commit
  │   └─ 设置 new_plane_state->commit = CRTC 的 commit (或 fake_commit)
  │
  ├─ 阶段D: FB 准备 (pin)
  │   drm_atomic_helper_prepare_planes()       [drm_atomic_helper.c:2277]
  │   └─ 对每个 new_plane_state 调用 funcs->prepare_fb()
  │       (VOP2 无 prepare_fb — 在 atomic_flush 中处理)
  │
  ├─ 阶段E: 状态交换 (point of no return)
  │   drm_atomic_helper_swap_state()           [drm_atomic_helper.c:2630]
  │   交换前:  plane->state = old_state
  │   交换后:  plane->state = new_state (现在是权威的当前状态)
  │   (state->planes[i].state 保存 old_state 供清理)
  │
  ├─ 阶段F: Commit Tail (硬件编程)
  │   drm_atomic_helper_commit_tail()          [drm_atomic_helper.c:1490]
  │   ├─ drm_atomic_helper_commit_modeset_disables()
  │   │    └─ vop2_crtc_atomic_disable()       [vop2.c:3001]
  │   ├─ drm_atomic_helper_commit_planes()     [drm_atomic_helper.c:2361]
  │   │    ├─ 每个 CRTC: atomic_begin()
  │   │    │    └─ vop2_crtc_atomic_begin()    [vop2.c:5390]  ★
  │   │    │        ├─ 三层遍历 planes: Cluster / zpos+layer_mixer / alpha
  │   │    │        └─ 排序 planes, 写 OVL_LAYER_SEL 寄存器
  │   │    ├─ 每个 plane: atomic_update() / atomic_disable()
  │   │    │    └─ vop2_plane_atomic_update()   [vop2.c:3281]
  │   │    │    └─ vop2_plane_atomic_disable()  [vop2.c:3201]
  │   │    └─ 每个 CRTC: atomic_flush()
  │   │         └─ vop2_crtc_atomic_flush()    [vop2.c:5775]
  │   │              └─ vop2_cfg_done()         ★ 硬件锁存触发器
  │   ├─ drm_atomic_helper_commit_modeset_enables()
  │   │    └─ vop2_crtc_atomic_enable()        [vop2.c:4565]
  │   ├─ drm_atomic_helper_commit_hw_done()
  │   └─ drm_atomic_helper_wait_for_vblanks()
  │
  ├─ 阶段G: 清理
  │   drm_atomic_helper_cleanup_planes()       [drm_atomic_helper.c:2567]
  │   └─ 对 old_plane_state 调用 cleanup_fb() (unpin)
  │
  └─ 阶段H: 释放
      drm_atomic_state_put()
      └─ atomic_destroy_state() 释放所有状态对象、FB 引用
```

### 6.4 VOP2 的 `vop2_cfg_done()` — 硬件锁存详解

**文件:** [rockchip_drm_vop2.c:1353](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L1353)

VOP2 使用**双缓冲（shadow register）**机制：
- `atomic_begin`、`atomic_update`、`atomic_flush` 中所有寄存器写入 = 写入 **shadow** 寄存器
- `cfg_done` 写 `REG_CFG_DONE` → 硬件在**下一个 vblank** 时原子性地将所有 shadow 寄存器拷贝到 active 寄存器
- 保证了帧级的原子性——不会出现上半帧新配置、下半帧旧配置的情况

如果 `layer_sel` 变了（plane 从 VP A 移到 VP B），还需要等待 frame start 才能让 commit 安全进行。

---

## 七、VOP2 特有的两层关联：`possible_crtcs` vs `vp_mask/win_mask`

VOP2 有两层独立的 plane-CRTC 关联：

| 概念 | 设置时机 | 含义 | 层级 |
|------|----------|------|------|
| `plane->possible_crtcs` | [`drm_universal_plane_init()`](../../kernel/drivers/gpu/drm/drm_plane.c#L164) / [`drm_crtc_init_with_planes()`](../../kernel/drivers/gpu/drm/drm_crtc.c#L266) | plane **能**连接哪些 CRTC（DRM 能力） | 软件层 |
| `win->vp_mask` | [`vop2_setup_layer_mixer_for_vp()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5235) 在 atomic commit 时 | 窗口**当前**路由到哪个 VP | 硬件层 |
| `vp->win_mask` | 同上 | VP**当前**激活了哪些物理窗口 | 硬件层 |
| `vp->plane_mask` | 设备树 `rockchip,plane-mask` / bootloader | 哪些窗口**允许**连接此 VP | 硬件能力 |

DRM core 在 [`drm_atomic_plane_check()`](../../kernel/drivers/gpu/drm/drm_atomic.c#L1036) 中校验 `possible_crtcs`。如果用户空间将 plane 绑定到不在 `possible_crtcs` 中的 CRTC，atomic commit 会在 check 阶段被拒绝。

硬件层面的 `vp_mask`/`win_mask` 动态路由则支持 plane 在不同 commit 之间切换到不同 CRTC（只要 `possible_crtcs` 允许），通过 `OVL_PORT_SEL` 硬件寄存器编程实现。

### 硬件路由切换的细节

当 plane 从 VP0 移到 VP1（[`vop2_setup_layer_mixer_for_vp()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5235)）：
```c
// 如果 win 的当前 vp_mask 与需要的不同:
old_vp->win_mask &= ~BIT(win->phys_id);   // 从旧 VP 移除
vp->win_mask     |=  BIT(win->phys_id);   // 加入新 VP
win->vp_mask      =  BIT(vp->id);         // 记录新的绑定
```

然后通过写 `OVL_PORT_SEL` 寄存器将这个映射编程到硬件交叉开关中。

---

## 八、Primary Plane vs Overlay Plane

### DRM Core 层面的区别

- `crtc->primary`：每个 CRTC 必须有且只有一个 primary plane
- Legacy IOCTL（SETCRTC、PAGE_FLIP）通过 `crtc->primary` 隐式操作
- `crtc->cursor`：可选的 cursor plane
- 用户空间的 `drmModeSetCrtc()` 自动选择 `crtc->primary`

### VOP2 中的处理

- 在 [`vop2_create_crtc()`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6716) 中，每个 VP 先分配 primary plane，再分配 overlay plane
- Primary plane 默认 zpos = 0（最底层）
- **运行时没有任何区别**：`atomic_check`、`atomic_begin`、`atomic_update`、`atomic_flush` 中对 primary 和 overlay 一视同仁
- static `win_data` 中有 3 个 PRIMARY 类型的窗口（Smart0/1, Esmart1），未被 VP 认领为 primary 的自动降级为 OVERLAY（[vop2.c:6894](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6894)）

---

## 九、VOP2 中 Plane 的 `vp_mask` 动态更新全景

```
设备树/bootloader:
  vp->plane_mask = 该 VP 允许的 win phys_id 掩码
  vp->primary_plane_phy_id = 主图层物理 ID
                    │
                    ▼
vop2_create_crtc():
  根据 plane_mask 为每个 VP 分配 primary win
  调用 drm_crtc_init_with_planes() → crtc->primary = win->base
                                 → win->possible_crtcs = drm_crtc_mask(crtc)
  调用 vop2_plane_init() → win->vp_mask 初始为 0
                    │
                    ▼
每个 Atomic Commit:
  vop2_crtc_atomic_begin():
    drm_atomic_crtc_for_each_plane(plane, crtc):
      win = to_vop2_win(plane)
      // 将 win 从旧 VP 解绑、绑定到新 VP
      vp->win_mask     |=  BIT(win->phys_id)
      win->vp_mask      =  BIT(vp->id)
      // 收集 zpos 用于排序
    // 排序，写 OVL_LAYER_SEL、OVL_PORT_SEL 寄存器
                    │
                    ▼
CRTC 关闭 (vop2_crtc_atomic_disable):
  vop2_disable_all_planes_for_crtc(crtc):
    for_each_set_bit(phys_id, &vp->win_mask, ...):
      vop2_win_disable(win)
    // vp->win_mask 代表的窗口全部关闭
```

---

## 十、关键函数速查表

### DRM Core — Plane

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_universal_plane_init()` | [drm_plane.c:164](../../kernel/drivers/gpu/drm/drm_plane.c#L164) | 分配/初始化 |
| `drm_plane_init()` | [drm_plane.c:323](../../kernel/drivers/gpu/drm/drm_plane.c#L323) | 分配/初始化 (Legacy) |
| `drm_plane_register_all()` | [drm_plane.c:281](../../kernel/drivers/gpu/drm/drm_plane.c#L281) | 注册 |
| `drm_plane_unregister_all()` | [drm_plane.c:296](../../kernel/drivers/gpu/drm/drm_plane.c#L296) | 注销 |
| `drm_plane_cleanup()` | [drm_plane.c:346](../../kernel/drivers/gpu/drm/drm_plane.c#L346) | 销毁 |
| `drm_plane_force_disable()` | [drm_plane.c:412](../../kernel/drivers/gpu/drm/drm_plane.c#L412) | 强制关闭 (Legacy) |
| `__setplane_internal()` | [drm_plane.c:643](../../kernel/drivers/gpu/drm/drm_plane.c#L643) | 运行时绑定 (Legacy) |
| `drm_mode_cursor_universal()` | [drm_plane.c:822](../../kernel/drivers/gpu/drm/drm_plane.c#L822) | Cursor plane 操作 |

### DRM Core — CRTC

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_crtc_init_with_planes()` | [drm_crtc.c:266](../../kernel/drivers/gpu/drm/drm_crtc.c#L266) | 分配/初始化 |
| `drm_crtc_init()` | [drm_modeset_helper.c:152](../../kernel/drivers/gpu/drm/drm_modeset_helper.c#L152) | 分配/初始化 (Legacy) |
| `drm_crtc_register_all()` | [drm_crtc.c:157](../../kernel/drivers/gpu/drm/drm_crtc.c#L157) | 注册 |
| `drm_crtc_unregister_all()` | [drm_crtc.c:176](../../kernel/drivers/gpu/drm/drm_crtc.c#L176) | 注销 |
| `drm_crtc_cleanup()` | [drm_crtc.c:354](../../kernel/drivers/gpu/drm/drm_crtc.c#L354) | 销毁 |
| `drm_crtc_force_disable()` | [drm_crtc.c:102](../../kernel/drivers/gpu/drm/drm_crtc.c#L102) | 强制关闭 (Legacy) |
| `__drm_mode_set_config_internal()` | [drm_crtc.c:456](../../kernel/drivers/gpu/drm/drm_crtc.c#L456) | 运行时绑定 (Legacy) |

### DRM Core — 全局流程

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_mode_config_reset()` | [drm_mode_config.c:176](../../kernel/drivers/gpu/drm/drm_mode_config.c#L176) | 状态重置 (post-init) |
| `drm_modeset_register_all()` | [drm_mode_config.c:30](../../kernel/drivers/gpu/drm/drm_mode_config.c#L30) | 注册入口 |
| `drm_modeset_unregister_all()` | [drm_mode_config.c:62](../../kernel/drivers/gpu/drm/drm_mode_config.c#L62) | 注销入口 |
| `drm_mode_config_cleanup()` | [drm_mode_config.c:431](../../kernel/drivers/gpu/drm/drm_mode_config.c#L431) | 全局销毁 (unload) |

### DRM Core — Atomic

| 函数 | 文件 | 阶段 |
|------|------|------|
| `drm_atomic_set_crtc_for_plane()` | [drm_atomic.c:1609](../../kernel/drivers/gpu/drm/drm_atomic.c#L1609) | CRTC 绑定 (唯一切入点) |
| `drm_atomic_set_fb_for_plane()` | [drm_atomic.c:1659](../../kernel/drivers/gpu/drm/drm_atomic.c#L1659) | FB 绑定 |
| `drm_atomic_get_plane_state()` | [drm_atomic.c:792](../../kernel/drivers/gpu/drm/drm_atomic.c#L792) | 状态组装 |
| `drm_atomic_plane_check()` | [drm_atomic.c:1036](../../kernel/drivers/gpu/drm/drm_atomic.c#L1036) | Core 验证 |
| `drm_atomic_helper_check_plane_state()` | [drm_atomic_helper.c:722](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L722) | 驱动侧验证 helper |
| `drm_atomic_helper_prepare_planes()` | [drm_atomic_helper.c:2277](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2277) | FB pin |
| `drm_atomic_helper_swap_state()` | [drm_atomic_helper.c:2630](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2630) | 状态交换 |
| `drm_atomic_helper_commit_planes()` | [drm_atomic_helper.c:2361](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2361) | 硬件编程 |
| `drm_atomic_helper_cleanup_planes()` | [drm_atomic_helper.c:2567](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L2567) | FB unpin |
| `drm_atomic_helper_crtc_reset()` | [drm_atomic_helper.c:3659](../../kernel/drivers/gpu/drm/drm_atomic_helper.c#L3659) | CRTC 状态重置 |

### Rockchip VOP2 — Init

| 函数 | 文件 |
|------|------|
| `vop2_bind()` | [rockchip_drm_vop2.c:7083](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L7083) |
| `vop2_win_init()` | [rockchip_drm_vop2.c:6955](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6955) |
| `vop2_create_crtc()` | [rockchip_drm_vop2.c:6716](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6716) |
| `vop2_plane_init()` | [rockchip_drm_vop2.c:6494](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6494) |
| `vop2_cursor_plane_init()` | [rockchip_drm_vop2.c:6593](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6593) |
| `drm_crtc_init_with_planes()` 调用处 | [rockchip_drm_vop2.c:6859](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L6859) |

### Rockchip VOP2 — Runtime

| 函数 | 文件 | 说明 |
|------|------|------|
| `vop2_crtc_atomic_check()` | [vop2.c:4825](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4825) | 空函数 |
| `vop2_crtc_atomic_begin()` | [vop2.c:5390](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5390) | ★ 核心编排：三层遍历 planes |
| `vop2_crtc_atomic_flush()` | [vop2.c:5775](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5775) | cfg_done 硬件锁存 |
| `vop2_crtc_atomic_enable()` | [vop2.c:4565](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L4565) | CRTC 使能，不遍历 plane |
| `vop2_crtc_atomic_disable()` | [vop2.c:3001](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3001) | CRTC 关闭，先关所有 plane |
| `vop2_plane_atomic_check()` | [vop2.c:3051](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3051) | 验证 + 预计算 DMA 地址 |
| `vop2_plane_atomic_update()` | [vop2.c:3281](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3281) | 写所有 plane 寄存器 |
| `vop2_plane_atomic_disable()` | [vop2.c:3201](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3201) | 关闭单个 plane |
| `vop2_disable_all_planes_for_crtc()` | [vop2.c:2090](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L2090) | 关闭 CRTC 的所有 plane |
| `vop2_cfg_done()` | [vop2.c:1353](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L1353) | 硬件锁存触发器 |
| `vop2_setup_layer_mixer_for_vp()` | [vop2.c:5235](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5235) | 硬件层路由配置 |
| `vop2_setup_alpha()` | [vop2.c:5099](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5099) | 逐层 alpha 混合配置 |
| `vop2_find_crtc_by_plane_mask()` | [vop2.c:1077](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L1077) | phys_id → CRTC 映射 |

---

## 十一、核心要点总结

1. **Atomic 驱动中** `plane->crtc` 和 `plane->fb` **永远为 NULL**，所有绑定通过 `plane->state->crtc` 和 `crtc_state->plane_mask`。

2. **`possible_crtcs` 是静态能力掩码**，`plane_mask` 是运行时派生状态。前者在 init 时设定不变，后者随 atomic commit 动态更新。

3. **`drm_crtc_init_with_planes()` 是 CRTC 初始化的核心**（[drm_crtc.c:266](../../kernel/drivers/gpu/drm/drm_crtc.c#L266)），它设定 `crtc->primary`/`crtc->cursor` 指针，并自动填写 primary plane 的 `possible_crtcs`（如果未设置）。

4. **CRTC 的 `primary` 指针只为 legacy IOCTL 服务**。在 atomic 模式下，用户空间通过 `CRTC_ID` 属性显式指定 plane 绑定到哪个 CRTC。

5. **VOP2 有两层 plane-CRTC 关联**：软件层的 `possible_crtcs`（DRM core 校验）和硬件层的 `vp_mask`/`win_mask`（通过 `OVL_PORT_SEL` 寄存器编程）。

6. **`vop2_crtc_atomic_begin()` 是 plane-CRTC 交互的核心**（[vop2.c:5390](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5390)）：它收集 CRTC 的所有 plane、按 zpos 排序、配置 layer mixer 和 alpha 混合。

7. **硬件锁存发生在 `vop2_cfg_done()`**（[vop2.c:1353](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L1353)）：所有 shadow 寄存器在下个 vblank 原子性地锁存到 active 寄存器，保证帧级原子性。

8. **Plane 不能直接从一个 CRTC 切换到另一个**：必须经过中间禁用状态（[`plane_switching_crtc()`](../../kernel/drivers/gpu/drm/drm_atomic.c#L1007) 检查）。

9. **销毁顺序是 init 的逆序**：plane 先于 CRTC 销毁（[`drm_mode_config_cleanup()`](../../kernel/drivers/gpu/drm/drm_mode_config.c#L431)），但注册时 plane 也是先于 CRTC 注册。

10. **VOP2 中 primary 和 overlay plane 在运行时没有区别对待**：都参与 zpos 排序、layer mixer 分配、alpha 混合。
