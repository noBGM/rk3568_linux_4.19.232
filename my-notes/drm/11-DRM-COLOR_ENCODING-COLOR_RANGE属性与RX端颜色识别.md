# DRM COLOR_ENCODING / COLOR_RANGE 属性与 RX 端颜色识别

基于 Linux 5.4.150 内核标准做法。

## 1. 背景：两个独立的色彩维度

视频信号的色彩有两个正交维度：

| 维度 | 属性名 | 管什么 | 决定什么 |
|---|---|---|---|
| 色彩空间 | `COLOR_ENCODING` | 颜色怎么编码 | YCbCr→RGB 的 **3x3 矩阵系数** |
| 量化范围 | `COLOR_RANGE` | 数值范围怎么映射 | YCbCr→RGB 的 **偏置量 (offset)** |

混淆会导致：

- Limited 当成 Full → 黑色发灰、白色不够亮
- Full 当成 Limited → 暗部死黑、亮部过曝

## 2. 5.4 内核的标准属性定义

### 2.1 头文件枚举 (drm_color_mgmt.h:54-65)

```c
enum drm_color_encoding {
    DRM_COLOR_YCBCR_BT601,   // 0 — SD 标清色彩矩阵
    DRM_COLOR_YCBCR_BT709,   // 1 — HD 高清色彩矩阵
    DRM_COLOR_YCBCR_BT2020,  // 2 — UHD/HDR 色彩矩阵
    DRM_COLOR_ENCODING_MAX,
};

enum drm_color_range {
    DRM_COLOR_YCBCR_LIMITED_RANGE,  // 0 — Y 16-235, CbCr 16-240
    DRM_COLOR_YCBCR_FULL_RANGE,     // 1 — Y/CbCr 0-255
    DRM_COLOR_RANGE_MAX,
};
```

### 2.2 Plane state 中的字段 (drm_plane.h:167-174)

```c
struct drm_plane_state {
    // ...
    enum drm_color_encoding color_encoding;  // 输入数据的色彩编码
    enum drm_color_range color_range;        // 输入数据的量化范围
    // ...
};
```

### 2.3 Plane 对象上的属性指针 (drm_plane.h:699-707)

```c
struct drm_plane {
    // ...
    struct drm_property *color_encoding_property;  // "COLOR_ENCODING" 枚举属性
    struct drm_property *color_range_property;     // "COLOR_RANGE" 枚举属性
};
```

## 3. 驱动侧实现（三步）

### 步骤一：创建并注册属性

在 plane 初始化函数中，调用标准 helper 即可。**只需要一行**：

```c
// 以 i915 为例 (intel_sprite.c:2508-2514)
drm_plane_create_color_properties(&plane->base,
    BIT(DRM_COLOR_YCBCR_BT601) |
    BIT(DRM_COLOR_YCBCR_BT709),                       // 支持的 encoding
    BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
    BIT(DRM_COLOR_YCBCR_FULL_RANGE),                  // 支持的 range
    DRM_COLOR_YCBCR_BT709,                            // 默认 encoding (HD)
    DRM_COLOR_YCBCR_LIMITED_RANGE);                   // 默认 range (电视标准)
```

参数说明：

| 参数 | 含义 | 常用值 |
|---|---|---|
| `supported_encodings` | 位掩码，硬件支持的 encoding | `BIT(BT601)\|BIT(BT709)\|BIT(BT2020)` |
| `supported_ranges` | 位掩码，硬件支持的 range | `BIT(LIMITED)\|BIT(FULL)` |
| `default_encoding` | 默认值，须在 supported_encodings 中 | `DRM_COLOR_YCBCR_BT709` |
| `default_range` | 默认值，须在 supported_ranges 中 | `DRM_COLOR_YCBCR_LIMITED_RANGE` |

这个函数内部做的事 (drm_color_mgmt.c:406-467)：

1. 遍历 supported_encodings → 组装 enum_list
2. 创建 `"COLOR_ENCODING"` 枚举属性 → 存到 `plane->color_encoding_property`
3. attach 到 plane，设 `plane->state->color_encoding = default_encoding`
4. 同上处理 `"COLOR_RANGE"` → `plane->color_range_property`

### 步骤二：驱动无需写 set/get 回调 —— DRM core 自动处理

这是 5.4 与 4.19 最大的区别。`drm_atomic_plane_set_property()` 已经包含了对这两个属性的标准处理 (drm_atomic_uapi.c:574-577)：

```c
} else if (property == plane->color_encoding_property) {
    state->color_encoding = val;  // core 自动写入
} else if (property == plane->color_range_property) {
    state->color_range = val;     // core 自动写入
```

`drm_atomic_plane_get_property()` 同样 (drm_atomic_uapi.c:637-640)：

```c
} else if (property == plane->color_encoding_property) {
    *val = state->color_encoding;  // core 自动读取
} else if (property == plane->color_range_property) {
    *val = state->color_range;     // core 自动读取
```

**驱动 `atomic_set_property` 回调只处理自定义私有属性**，标准属性全部由 core 解析。

对比 4.19：4.19 没有这两个标准属性，Rockchip 用 `drm_property_create_range()` 创建了自定义的 `"COLOR_SPACE"`，且必须自己写 set/get 回调。

### 步骤三：在 atomic_update 中读取并配置硬件

```c
static void vop2_plane_atomic_update(struct drm_plane *plane,
                                      struct drm_plane_state *old_state)
{
    struct drm_plane_state *state = plane->state;
    struct drm_framebuffer *fb = state->fb;

    // 只对 YUV 格式有意义
    if (drm_format_info_is_yuv(fb->format)) {
        enum drm_color_encoding enc = state->color_encoding;
        enum drm_color_range range = state->color_range;

        // 1. 选择 3x3 矩阵系数
        switch (enc) {
        case DRM_COLOR_YCBCR_BT601:
            // SD:  Y  = 0.299R + 0.587G + 0.114B
            // 设置 VOP2 CSC 为 BT.601 模式
            break;
        case DRM_COLOR_YCBCR_BT709:
            // HD:  Y  = 0.2126R + 0.7152G + 0.0722B
            // 设置 VOP2 CSC 为 BT.709 模式
            break;
        case DRM_COLOR_YCBCR_BT2020:
            // UHD: Y  = 0.2627R + 0.6780G + 0.0593B
            // 设置 VOP2 CSC 为 BT.2020 模式
            break;
        }

        // 2. 选择量化范围偏移
        if (range == DRM_COLOR_YCBCR_LIMITED_RANGE) {
            // Limited: Y 需减 16, CbCr 需减 128 (即 -0.5 归一化)
            // 设置 VOP2 CSC 的偏移量寄存器
        } else {
            // Full: Y/CbCr 从 0-255 直接映射
            // 设置 VOP2 CSC 的偏移量寄存器
        }
    }
}
```

## 4. Userspace 用法

### 4.1 枚举属性 ID

```c
#include <xf86drm.h>
#include <xf86drmMode.h>

// 查找 COLOR_RANGE 属性的 ID
uint32_t find_prop_id(int fd, uint32_t plane_id, const char *name)
{
    drmModeObjectProperties *props =
        drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
        uint32_t id = 0;
        if (!strcmp(prop->name, name))
            id = prop->prop_id;
        drmModeFreeProperty(prop);
        if (id) {
            drmModeFreeObjectProperties(props);
            return id;
        }
    }
    drmModeFreeObjectProperties(props);
    return 0;
}
```

### 4.2 设置属性

```c
// 拿到属性 ID
uint32_t color_range_id  = find_prop_id(fd, plane_id, "COLOR_RANGE");
uint32_t color_enc_id    = find_prop_id(fd, plane_id, "COLOR_ENCODING");

// 构建 atomic request
drmModeAtomicReq *req = drmModeAtomicAlloc();

// 设置 COLOR_RANGE (enum 值)
drmModeAtomicAddProperty(req, plane_id, color_range_id,
                         DRM_COLOR_YCBCR_LIMITED_RANGE);  // 0

// 设置 COLOR_ENCODING (enum 值)
drmModeAtomicAddProperty(req, plane_id, color_enc_id,
                         DRM_COLOR_YCBCR_BT709);          // 1

// 提交
drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
drmModeAtomicFree(req);
```

### 4.3 查询当前值

```c
drmModeObjectProperties *props =
    drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);

for (uint32_t i = 0; i < props->count_props; i++) {
    drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
    if (!strcmp(prop->name, "COLOR_RANGE")) {
        uint64_t val = props->prop_values[i];
        // val == 0 → YCbCr limited range
        // val == 1 → YCbCr full range
    }
    drmModeFreeProperty(prop);
}
drmModeFreeObjectProperties(props);
```

## 5. 无线 RX 场景的完整链路

```
信号源 (Blu-ray/PC/手机)
    │
    │ HDMI / DP / 屏幕镜像
    ▼
TX 中继器 (WiFi 发送端)
    │ 从 HDMI RX 芯片获取 AVI InfoFrame
    │ 解析出: YQ[1:0], Q[1:0], C[1:0], EC[2:0]
    │
    │ WiFi Display / Miracast / 私有协议
    │ AVI InfoFrame 嵌入 MPEG2-TS 流
    ▼
  ════ 无线 ════
    │
    ▼
RX 中继器 (rk356x Linux)
    │
    │ WiFi 协议栈解析 MPEG2-TS → 提取 AVI InfoFrame
    │
    ├─→ Userspace daemon (wfd-sink / 私有 RX daemon)
    │       │
    │       │ 从 AVI InfoFrame 提取:
    │       │   YQ[1:0] → limited(0) / full(1)
    │       │   C[1:0], EC[2:0] → BT.601 / BT.709 / BT.2020
    │       │
    │       │ drmModeAtomicAddProperty(req, plane_id,
    │       │     color_range_id,  ycc_range);
    │       │ drmModeAtomicAddProperty(req, plane_id,
    │       │     color_enc_id,    colorspace);
    │       │
    │       │ drmModeAtomicCommit(fd, req, ...);
    │       │
    │       ▼
    │  DRM Core (drm_atomic_uapi.c)
    │       │
    │       │ state->color_range = DRM_COLOR_YCBCR_LIMITED_RANGE
    │       │ state->color_encoding = DRM_COLOR_YCBCR_BT709
    │       │
    │       ▼
    │  VOP2 驱动 (atomic_update)
    │       │ 读 state->color_range / state->color_encoding
    │       │ 配置 VOP2 CSC 寄存器 (矩阵系数 + 偏移量)
    │       │
    │       ▼
    │  VOP2 硬件 CSC → 正确 YCbCr→RGB 转换
    │
    ▼
  显示屏 (MIPI-DSI / LVDS / HDMI 本地输出)
```

### 5.1 AVI InfoFrame 关键位映射到 DRM 属性

| AVI InfoFrame 位 | 含义 | → DRM 属性 | 值 |
|---|---|---|---|
| Byte 2 bit 6-7 (YQ1, YQ0) | YCbCr 量化范围 | `COLOR_RANGE` | 0=Limited, 1=Full |
| Byte 3 bit 1-2 (Q1, Q0) | RGB 量化范围 | — (YUV 场景不使用) | — |
| Byte 3 bit 4-7 (C1, C0, M1, M0) | 色彩空间 / 色域 | `COLOR_ENCODING` | 1=BT.601, 2=BT.709, 3=xvYCC601... |
| Byte 3 bit 0 (EC2..EC0) | 扩展色域 | `COLOR_ENCODING` | 组合判断 BT.2020 等 |

### 5.2 RX daemon 伪代码

```c
struct avi_infoframe {
    uint8_t ycc_quant_range;  // 0=limited, 1=full
    uint8_t colorimetry;      // 1=BT.601, 2=BT.709, ...
};

void on_video_stream_start(struct avi_infoframe *avi)
{
    drmModeAtomicReq *req = drmModeAtomicAlloc();

    // 映射 YCC 量化范围
    int color_range = (avi->ycc_quant_range == 0)
        ? DRM_COLOR_YCBCR_LIMITED_RANGE   // 0
        : DRM_COLOR_YCBCR_FULL_RANGE;     // 1

    // 映射色彩空间
    int color_enc;
    switch (avi->colorimetry) {
    case 1: color_enc = DRM_COLOR_YCBCR_BT601;  break;
    case 2: color_enc = DRM_COLOR_YCBCR_BT709;  break;
    case 3: /* xvYCC601 */ break;
    // ... 见 drm_connector.h DRM_MODE_COLORIMETRY_* 定义
    }

    drmModeAtomicAddProperty(req, plane_id, g_color_range_id, color_range);
    drmModeAtomicAddProperty(req, plane_id, g_color_enc_id,  color_enc);

    drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
    drmModeAtomicFree(req);
}
```

## 6. CSC 矩阵参数：Limited vs Full

### 6.1 BT.709 Limited Range (广播电视标准)

```
Y  ∈ [16, 235]   → 归一化为 [0, 1]:  Y_norm  = (Y - 16) / 219
Cb ∈ [16, 240]   → 归一化为 [-0.5, 0.5]: Cb_norm = (Cb - 128) / 224
Cr ∈ [16, 240]   → 归一化为 [-0.5, 0.5]: Cr_norm = (Cr - 128) / 224

然后:
R = Y_norm + 1.5748 * Cr_norm
G = Y_norm - 0.1873 * Cb_norm - 0.4681 * Cr_norm
B = Y_norm + 1.8556 * Cb_norm
```

### 6.2 BT.709 Full Range (JPEG / 部分摄像头)

```
Y  ∈ [0, 255]    → 归一化为 [0, 1]:  Y_norm  = Y / 255
Cb ∈ [0, 255]    → 归一化为 [-0.5, 0.5]: Cb_norm = (Cb - 128) / 255
Cr ∈ [0, 255]    → 归一化为 [-0.5, 0.5]: Cr_norm = (Cr - 128) / 255

3x3 矩阵跟 Limited 一样，区别仅在于:
  - 偏置 (offset) 不同: 16/219 vs 0/255
  - 缩放 (scale)  不同: 219 vs 255
```

VOP2 CSC 硬件通常用定点数表示这些系数，`color_range` 决定偏置量和缩放因子的取值。

## 7. 对比 4.19 → 5.4 迁移要点

| | 4.19 (当前 RK356x) | 5.4 (标准做法) |
|---|---|---|
| 标准属性 | 无 | `COLOR_ENCODING` + `COLOR_RANGE` |
| 属性类型 | 自定义 `drm_property_create_range` | `drm_property_create_enum` |
| 属性创建 | 手动写 | `drm_plane_create_color_properties()` |
| set/get | 驱动必须写回调 | **DRM core 自动处理** |
| 值的位置 | 驱动私有 state | `drm_plane_state.color_{encoding,range}` |
| Userspace 枚举值 | 自定义数字 | 标准 enum 字符串 |

### 7.1 4.19 上直接移植 5.4 标准的要点

如果要在 4.19 内核上使用 5.4 的标准属性，需要 backport 三个补丁才能让 DRM core 自动处理：

- `drm_color_mgmt.c` — `drm_plane_create_color_properties()` 及 name 数组
- `drm_atomic_uapi.c` — `drm_atomic_plane_set_property()` / `get_property()` 中加入 `color_encoding_property` / `color_range_property` 分支
- `drm_plane.h` — `drm_plane_state` 中加入 `color_encoding` / `color_range` 字段，`drm_plane` 中加入两个 property 指针

不想 backport 也可以走捷径 —— 仿照现有 `COLOR_SPACE` 的写法，再创建一个自定义的 `COLOR_RANGE` 属性 (range 0-1)，驱动自己写 set/get。功能一样，只是不是通用标准属性。

## 8. 关键文件索引 (5.4)

| 文件 | 内容 |
|---|---|
| [include/drm/drm_color_mgmt.h](include/drm/drm_color_mgmt.h) | 枚举定义 + `drm_plane_create_color_properties()` 声明 |
| [drivers/gpu/drm/drm_color_mgmt.c](drivers/gpu/drm/drm_color_mgmt.c) | 属性创建实现 (L406-467), name 数组 (L351-360) |
| [include/drm/drm_plane.h](include/drm/drm_plane.h) | `drm_plane_state` 字段 (L167-174), `drm_plane` 属性指针 (L699-707) |
| [drivers/gpu/drm/drm_atomic_uapi.c](drivers/gpu/drm/drm_atomic_uapi.c) | core 自动 set (L574-577) / get (L637-640) |
| [include/drm/drm_connector.h](include/drm/drm_connector.h) | 连接器的 `colorspace_property` 与 DP MSA 枚举 (L308-338) |
