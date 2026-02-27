/*
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef __DRM_ENCODER_H__
#define __DRM_ENCODER_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mode.h>
#include <drm/drm_mode_object.h>

struct drm_encoder;

/**
 * struct drm_encoder_funcs - encoder controls
 *
 * Encoders sit between CRTCs and connectors.
 */
struct drm_encoder_funcs {
	/**
	 * @reset:
	 *
	 * Reset encoder hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 */
	void (*reset)(struct drm_encoder *encoder);

	/**
	 * @destroy:
	 *
	 * Clean up encoder resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since an encoder cannot be
	 * hotplugged in DRM.
	 */
	void (*destroy)(struct drm_encoder *encoder);

	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the encoder like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_encoder *encoder);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the encoder from
	 * @late_register. It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_encoder *encoder);
};

/**
 * struct drm_encoder - 中央 DRM 编码器（encoder）结构体
 * @dev: 父 DRM 设备
 * @head: 链表管理节点
 * @base: 基础 KMS 对象
 * @name: 人类可读的名称，驱动可覆盖
 * @bridge: 与此编码器关联的桥接器（bridge）
 * @funcs: 控制函数集
 * @helper_private: 中间层私有数据
 *
 * CRTC 驱动像素到编码器，编码器将像素信号转换为适合特定连接器（或连接器组）的信号格式。
 *
 * 显示管道：Framebuffer → Plane → CRTC → Encoder → Connector → 物理显示器
 *                                     ↑
 *                                 信号转换层
 */
struct drm_encoder {
	/*
	 * dev: 指向父 DRM 设备（drm_device）的指针。
	 * 所有 DRM 对象都通过这个字段关联到设备实例。
	 */
	struct drm_device *dev;

	/*
	 * head: 链表节点，将此 encoder 挂到 drm_mode_config.encoder_list 上。
	 * 系统通过遍历这个链表枚举所有编码器。
	 */
	struct list_head head;

	/*
	 * base: DRM 模式对象基类，包含：
	 *   - id: 用户空间可见的唯一 ID（通过 ioctl 查询）
	 *   - type: 对象类型（DRM_MODE_OBJECT_ENCODER）
	 *   - properties: 属性链表
	 */
	struct drm_mode_object base;

	/*
	 * name: 编码器的字符串名称，例如 "HDMI-A-1"、"DSI-1"。
	 * 驱动可通过 drm_encoder_init() 自动生成或手动设置。
	 */
	char *name;

	/**
	 * @encoder_type: 编码器类型标识
	 *
	 * 定义在 drm_mode.h 中的 DRM_MODE_ENCODER_<foo> 枚举值之一。
	 * 目前已定义的类型：
	 *
	 * - DRM_MODE_ENCODER_DAC:
	 *     VGA 和 DVI-I/DVI-A 的模拟输出（数模转换器）
	 *
	 * - DRM_MODE_ENCODER_TMDS:
	 *     DVI、HDMI 和（嵌入式）DisplayPort 的数字输出
	 *     TMDS = Transition Minimized Differential Signaling（过渡最小化差分信号）
	 *
	 * - DRM_MODE_ENCODER_LVDS:
	 *     显示面板，或任何使用专有并行连接器的面板
	 *     LVDS = Low-Voltage Differential Signaling（低压差分信号）
	 *
	 * - DRM_MODE_ENCODER_TVDAC:
	 *     电视输出（复合视频、S-Video、分量视频、SCART）
	 *
	 * - DRM_MODE_ENCODER_VIRTUAL:
	 *     虚拟机显示器（如 virtio-gpu、QXL）
	 *
	 * - DRM_MODE_ENCODER_DSI:
	 *     通过 DSI 串行总线连接的面板
	 *     DSI = Display Serial Interface（MIPI 规范）
	 *
	 * - DRM_MODE_ENCODER_DPI:
	 *     通过 DPI 并行总线连接的面板
	 *     DPI = Display Parallel Interface
	 *
	 * - DRM_MODE_ENCODER_DPMST:
	 *     特殊的"假"编码器，用于允许多个 DP MST 流共享一个物理编码器
	 *     MST = Multi-Stream Transport（DP 1.2+ 的多屏拓扑）
	 */
	int encoder_type;

	/**
	 * @index: 在 mode_config.encoder_list 中的位置索引
	 *
	 * 可用作数组下标，在编码器生命周期内保持不变（从注册到注销）。
	 * 通过 drm_encoder_index(encoder) 获取此值。
	 */
	unsigned index;

	/**
	 * @possible_crtcs: 可能的 CRTC 绑定掩码
	 *
	 * 位掩码，指示此编码器可以连接到哪些 CRTC。
	 * 位索引对应 drm_crtc_index(crtc)。
	 *
	 * 例如：
	 *   possible_crtcs = 0b0011  → 可连接 CRTC 0 和 CRTC 1
	 *   possible_crtcs = 0b0100  → 只能连接 CRTC 2
	 *   possible_crtcs = 0b1111  → 可连接所有 4 个 CRTC
	 *
	 * 驱动必须在调用 drm_encoder_init() 之前设置此字段。
	 *
	 * 注意：
	 * 1. 实际上几乎所有驱动都会设置错误的值（通常是全 1，声称能连所有 CRTC）
	 * 2. 由于 CRTC 不支持热插拔，索引在系统启动后固定
	 */
	uint32_t possible_crtcs;

	/**
	 * @possible_clones: 可以"一起工作"的编码器位掩码（克隆/镜像模式）
	 *
	 * ## 什么是克隆（Cloning）？
	 *
	 * 克隆是指：**一个 CRTC 的像素流同时输出给多个 Encoder**，
	 * 让多个显示器显示完全相同的内容（镜像模式）。
	 *
	 * 硬件场景举例：
	 *   ┌──────┐
	 *   │CRTC 0│─┬→ Encoder A (HDMI) → 显示器 1  ┐
	 *   └──────┘ │                                ├─ 显示相同画面
	 *            └→ Encoder B (VGA)  → 显示器 2  ┘
	 *
	 * 不是所有 encoder 都能一起克隆，因为：
	 * - 硬件限制：某些 encoder 不支持同时驱动
	 * - 时序冲突：两个 encoder 的时序参数可能不兼容
	 *
	 * ## 位掩码如何设置？
	 *
	 * 位索引 = drm_encoder_index(encoder)
	 *
	 * 假设系统有 4 个 encoder（索引 0-3）：
	 *
	 *   Encoder 0 (HDMI):   index=0
	 *   Encoder 1 (VGA):    index=1
	 *   Encoder 2 (MIPI):   index=2
	 *   Encoder 3 (eDP):    index=3
	 *
	 * 硬件能力：HDMI 和 VGA 可以克隆，但 MIPI/eDP 只能独立工作。
	 *
	 * 正确的设置（关键：双向对称）：
	 *
	 *   Encoder 0 (HDMI):  possible_clones = 0b0011  (bit0自己 + bit1的VGA)
	 *   Encoder 1 (VGA):   possible_clones = 0b0011  (bit0的HDMI + bit1自己)
	 *   Encoder 2 (MIPI):  possible_clones = 0b0100  (只有 bit2 自己)
	 *   Encoder 3 (eDP):   possible_clones = 0b1000  (只有 bit3 自己)
	 *
	 * ## 为什么需要"双向"设置？
	 *
	 * 因为用户可能选择"以 HDMI 为主克隆到 VGA"，
	 * 也可能选择"以 VGA 为主克隆到 HDMI"。
	 * 两个方向都必须在对方的 possible_clones 里声明才能生效。
	 *
	 * ## 实际情况（警告）
	 *
	 * 大多数驱动直接设置 possible_clones = 0xFFFFFFFF（全 1），
	 * 声称"所有 encoder 都能克隆"，但这通常不是真的。
	 * 用户空间的合成器（compositor）不会依赖这个字段做决策。
	 *
	 * ## 注意事项
	 *
	 * - 驱动必须在调用 drm_encoder_init() **之前**设置此字段
	 * - 必须包含"自己"的位（bit N 代表 encoder N 自己）
	 * - Encoder 不支持热插拔，索引固定，所以初始化时就能确定位掩码
	 */
	uint32_t possible_clones;

	/**
	 * @loader_protect: Bootloader logo 保护状态
	 *
	 * 指示编码器是否正在显示 bootloader 传递的 logo（启动画面）。
	 * 如果为 true，驱动应避免打断当前显示状态，确保无缝过渡。
	 *
	 * Rockchip 特有字段，用于"无闪烁启动"场景：
	 * Bootloader → Kernel 继承显示配置，不黑屏。
	 */
	bool loader_protect;

	/**
	 * @crtc: 当前绑定的 CRTC
	 *
	 * 指向当前连接的 CRTC 指针，仅在**非原子驱动**中有意义。
	 *
	 * 原子驱动（Atomic KMS）应该检查 drm_connector_state.crtc，
	 * 因为原子模式下的连接关系通过 state 对象管理，而非直接修改对象指针。
	 *
	 * 对于非原子驱动，此字段在 drm_crtc_helper_set_mode() 中更新。
	 */
	struct drm_crtc *crtc;

	/*
	 * bridge: 关联的 DRM 桥接器（drm_bridge）。
	 *
	 * 桥接器是一种中间层抽象，用于连接编码器和连接器之间的外部芯片。
	 * 例如：HDMI 转 MIPI、LVDS 转 eDP 转换芯片。
	 *
	 * 可以形成桥接链：encoder → bridge1 → bridge2 → connector
	 */
	struct drm_bridge *bridge;

	/*
	 * funcs: 编码器操作函数指针集（drm_encoder_funcs）。
	 *
	 * 必需的回调：
	 *   - destroy: 销毁编码器对象
	 *
	 * 可选的回调：
	 *   - reset: 重置编码器状态
	 *   - atomic_duplicate_state / atomic_destroy_state: 原子状态管理
	 */
	const struct drm_encoder_funcs *funcs;

	/*
	 * helper_private: 辅助层私有数据（drm_encoder_helper_funcs）。
	 *
	 * 辅助层提供更高级的操作接口：
	 *   - mode_set: 设置显示模式（分辨率、刷新率）
	 *   - enable / disable: 开关编码器
	 *   - atomic_check / atomic_commit: 原子模式检查和提交
	 *
	 * 驱动通过 drm_encoder_helper_add() 设置此字段。
	 */
	const struct drm_encoder_helper_funcs *helper_private;
};

#define obj_to_encoder(x) container_of(x, struct drm_encoder, base)

__printf(5, 6)
int drm_encoder_init(struct drm_device *dev,
		     struct drm_encoder *encoder,
		     const struct drm_encoder_funcs *funcs,
		     int encoder_type, const char *name, ...);

/**
 * drm_encoder_index - find the index of a registered encoder
 * @encoder: encoder to find index for
 *
 * Given a registered encoder, return the index of that encoder within a DRM
 * device's list of encoders.
 */
static inline unsigned int drm_encoder_index(const struct drm_encoder *encoder)
{
	return encoder->index;
}

/**
 * drm_encoder_mask - find the mask of a registered ENCODER
 * @encoder: encoder to find mask for
 *
 * Given a registered encoder, return the mask bit of that encoder for an
 * encoder's possible_clones field.
 */
static inline u32 drm_encoder_mask(const struct drm_encoder *encoder)
{
	return 1 << drm_encoder_index(encoder);
}

/**
 * drm_encoder_crtc_ok - can a given crtc drive a given encoder?
 * @encoder: encoder to test
 * @crtc: crtc to test
 *
 * Returns false if @encoder can't be driven by @crtc, true otherwise.
 */
static inline bool drm_encoder_crtc_ok(struct drm_encoder *encoder,
				       struct drm_crtc *crtc)
{
	return !!(encoder->possible_crtcs & drm_crtc_mask(crtc));
}

/**
 * drm_encoder_find - find a &drm_encoder
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: encoder id
 *
 * Returns the encoder with @id, NULL if it doesn't exist. Simple wrapper around
 * drm_mode_object_find().
 */
static inline struct drm_encoder *drm_encoder_find(struct drm_device *dev,
						   struct drm_file *file_priv,
						   uint32_t id)
{
	struct drm_mode_object *mo;

	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_ENCODER);

	return mo ? obj_to_encoder(mo) : NULL;
}

void drm_encoder_cleanup(struct drm_encoder *encoder);

/**
 * drm_for_each_encoder_mask - iterate over encoders specified by bitmask
 * @encoder: the loop cursor
 * @dev: the DRM device
 * @encoder_mask: bitmask of encoder indices
 *
 * Iterate over all encoders specified by bitmask.
 */
#define drm_for_each_encoder_mask(encoder, dev, encoder_mask) \
	list_for_each_entry((encoder), &(dev)->mode_config.encoder_list, head) \
		for_each_if ((encoder_mask) & drm_encoder_mask(encoder))

/**
 * drm_for_each_encoder - iterate over all encoders
 * @encoder: the loop cursor
 * @dev: the DRM device
 *
 * Iterate over all encoders of @dev.
 */
#define drm_for_each_encoder(encoder, dev) \
	list_for_each_entry(encoder, &(dev)->mode_config.encoder_list, head)

#endif
