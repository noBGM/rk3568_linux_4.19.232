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

#ifndef __DRM_PLANE_H__
#define __DRM_PLANE_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_color_mgmt.h>

struct drm_crtc;
struct drm_printer;
struct drm_modeset_acquire_ctx;

/**
 * struct drm_plane_state - mutable plane state
 *
 * Please not that the destination coordinates @crtc_x, @crtc_y, @crtc_h and
 * @crtc_w and the source coordinates @src_x, @src_y, @src_h and @src_w are the
 * raw coordinates provided by userspace. Drivers should use
 * drm_atomic_helper_check_plane_state() and only use the derived rectangles in
 * @src and @dst to program the hardware.
 */
struct drm_plane_state {
	/** @plane: backpointer to the plane */
	struct drm_plane *plane;

	/**
	 * @crtc:
	 *
	 * Currently bound CRTC, NULL if disabled. Do not this write directly,
	 * use drm_atomic_set_crtc_for_plane()
	 */
	struct drm_crtc *crtc;

	/**
	 * @fb:
	 *
	 * Currently bound framebuffer. Do not write this directly, use
	 * drm_atomic_set_fb_for_plane()
	 */
	struct drm_framebuffer *fb;

	/**
	 * @fence:
	 *
	 * Optional fence to wait for before scanning out @fb. The core atomic
	 * code will set this when userspace is using explicit fencing. Do not
	 * write this directly for a driver's implicit fence, use
	 * drm_atomic_set_fence_for_plane() to ensure that an explicit fence is
	 * preserved.
	 *
	 * Drivers should store any implicit fence in this from their
	 * &drm_plane_helper_funcs.prepare_fb callback. See drm_gem_fb_prepare_fb()
	 * and drm_gem_fb_simple_display_pipe_prepare_fb() for suitable helpers.
	 */
	struct dma_fence *fence;

	/**
	 * @crtc_x:
	 *
	 * Left position of visible portion of plane on crtc, signed dest
	 * location allows it to be partially off screen.
	 */

	int32_t crtc_x;
	/**
	 * @crtc_y:
	 *
	 * Upper position of visible portion of plane on crtc, signed dest
	 * location allows it to be partially off screen.
	 */
	int32_t crtc_y;

	/** @crtc_w: width of visible portion of plane on crtc */
	/** @crtc_h: height of visible portion of plane on crtc */
	uint32_t crtc_w, crtc_h;

	/**
	 * @src_x: left position of visible portion of plane within plane (in
	 * 16.16 fixed point).
	 */
	uint32_t src_x;
	/**
	 * @src_y: upper position of visible portion of plane within plane (in
	 * 16.16 fixed point).
	 */
	uint32_t src_y;
	/** @src_w: width of visible portion of plane (in 16.16) */
	/** @src_h: height of visible portion of plane (in 16.16) */
	uint32_t src_h, src_w;

	/**
	 * @alpha:
	 * Opacity of the plane with 0 as completely transparent and 0xffff as
	 * completely opaque. See drm_plane_create_alpha_property() for more
	 * details.
	 */
	u16 alpha;

	/**
	 * @pixel_blend_mode:
	 * The alpha blending equation selection, describing how the pixels from
	 * the current plane are composited with the background. Value can be
	 * one of DRM_MODE_BLEND_*
	 */
	uint16_t pixel_blend_mode;

	/**
	 * @rotation:
	 * Rotation of the plane. See drm_plane_create_rotation_property() for
	 * more details.
	 */
	unsigned int rotation;

	/**
	 * @zpos:
	 * Priority of the given plane on crtc (optional).
	 *
	 * Note that multiple active planes on the same crtc can have an
	 * identical zpos value. The rule to solving the conflict is to compare
	 * the plane object IDs; the plane with a higher ID must be stacked on
	 * top of a plane with a lower ID.
	 *
	 * See drm_plane_create_zpos_property() and
	 * drm_plane_create_zpos_immutable_property() for more details.
	 */
	unsigned int zpos;

	/**
	 * @normalized_zpos:
	 * Normalized value of zpos: unique, range from 0 to N-1 where N is the
	 * number of active planes for given crtc. Note that the driver must set
	 * &drm_mode_config.normalize_zpos or call drm_atomic_normalize_zpos() to
	 * update this before it can be trusted.
	 */
	unsigned int normalized_zpos;

	/**
	 * @color_encoding:
	 *
	 * Color encoding for non RGB formats
	 */
	enum drm_color_encoding color_encoding;

	/**
	 * @color_range:
	 *
	 * Color range for non RGB formats
	 */
	enum drm_color_range color_range;

	/** @src: clipped source coordinates of the plane (in 16.16) */
	/** @dst: clipped destination coordinates of the plane */
	struct drm_rect src, dst;

	/**
	 * @visible:
	 *
	 * Visibility of the plane. This can be false even if fb!=NULL and
	 * crtc!=NULL, due to clipping.
	 */
	bool visible;

	/**
	 * @commit: Tracks the pending commit to prevent use-after-free conditions,
	 * and for async plane updates.
	 *
	 * May be NULL.
	 */
	struct drm_crtc_commit *commit;

	/** @state: backpointer to global drm_atomic_state */
	struct drm_atomic_state *state;
};

static inline struct drm_rect
drm_plane_state_src(const struct drm_plane_state *state)
{
	struct drm_rect src = {
		.x1 = state->src_x,
		.y1 = state->src_y,
		.x2 = state->src_x + state->src_w,
		.y2 = state->src_y + state->src_h,
	};
	return src;
}

static inline struct drm_rect
drm_plane_state_dest(const struct drm_plane_state *state)
{
	struct drm_rect dest = {
		.x1 = state->crtc_x,
		.y1 = state->crtc_y,
		.x2 = state->crtc_x + state->crtc_w,
		.y2 = state->crtc_y + state->crtc_h,
	};
	return dest;
}

/**
 * struct drm_plane_funcs - driver plane control functions
 */
struct drm_plane_funcs {
	/**
	 * @update_plane:
	 *
	 * This is the legacy entry point to enable and configure the plane for
	 * the given CRTC and framebuffer. It is never called to disable the
	 * plane, i.e. the passed-in crtc and fb paramters are never NULL.
	 *
	 * The source rectangle in frame buffer memory coordinates is given by
	 * the src_x, src_y, src_w and src_h parameters (as 16.16 fixed point
	 * values). Devices that don't support subpixel plane coordinates can
	 * ignore the fractional part.
	 *
	 * The destination rectangle in CRTC coordinates is given by the
	 * crtc_x, crtc_y, crtc_w and crtc_h parameters (as integer values).
	 * Devices scale the source rectangle to the destination rectangle. If
	 * scaling is not supported, and the source rectangle size doesn't match
	 * the destination rectangle size, the driver must return a
	 * -<errorname>EINVAL</errorname> error.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_update_plane() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*update_plane)(struct drm_plane *plane,
			    struct drm_crtc *crtc, struct drm_framebuffer *fb,
			    int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h,
			    struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @disable_plane:
	 *
	 * This is the legacy entry point to disable the plane. The DRM core
	 * calls this method in response to a DRM_IOCTL_MODE_SETPLANE IOCTL call
	 * with the frame buffer ID set to 0.  Disabled planes must not be
	 * processed by the CRTC.
	 *
	 * Drivers implementing atomic modeset should use
	 * drm_atomic_helper_disable_plane() to implement this hook.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*disable_plane)(struct drm_plane *plane,
			     struct drm_modeset_acquire_ctx *ctx);

	/**
	 * @destroy:
	 *
	 * Clean up plane resources. This is only called at driver unload time
	 * through drm_mode_config_cleanup() since a plane cannot be hotplugged
	 * in DRM.
	 */
	void (*destroy)(struct drm_plane *plane);

	/**
	 * @reset:
	 *
	 * Reset plane hardware and software state to off. This function isn't
	 * called by the core directly, only through drm_mode_config_reset().
	 * It's not a helper hook only for historical reasons.
	 *
	 * Atomic drivers can use drm_atomic_helper_plane_reset() to reset
	 * atomic state using this hook.
	 */
	void (*reset)(struct drm_plane *plane);

	/**
	 * @set_property:
	 *
	 * This is the legacy entry point to update a property attached to the
	 * plane.
	 *
	 * This callback is optional if the driver does not support any legacy
	 * driver-private properties. For atomic drivers it is not used because
	 * property handling is done entirely in the DRM core.
	 *
	 * RETURNS:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*set_property)(struct drm_plane *plane,
			    struct drm_property *property, uint64_t val);

	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current atomic state for this plane and return it.
	 * The core and helpers guarantee that any atomic state duplicated with
	 * this hook and still owned by the caller (i.e. not transferred to the
	 * driver by calling &drm_mode_config_funcs.atomic_commit) will be
	 * cleaned up by calling the @atomic_destroy_state hook in this
	 * structure.
	 *
	 * This callback is mandatory for atomic drivers.
	 *
	 * Atomic drivers which don't subclass &struct drm_plane_state should use
	 * drm_atomic_helper_plane_duplicate_state(). Drivers that subclass the
	 * state structure to extend it with driver-private state should use
	 * __drm_atomic_helper_plane_duplicate_state() to make sure shared state is
	 * duplicated in a consistent fashion across drivers.
	 *
	 * It is an error to call this hook before &drm_plane.state has been
	 * initialized correctly.
	 *
	 * NOTE:
	 *
	 * If the duplicate state references refcounted resources this hook must
	 * acquire a reference for each of them. The driver must release these
	 * references again in @atomic_destroy_state.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when the allocation failed.
	 */
	struct drm_plane_state *(*atomic_duplicate_state)(struct drm_plane *plane);

	/**
	 * @atomic_destroy_state:
	 *
	 * Destroy a state duplicated with @atomic_duplicate_state and release
	 * or unreference all resources it references
	 *
	 * This callback is mandatory for atomic drivers.
	 */
	void (*atomic_destroy_state)(struct drm_plane *plane,
				     struct drm_plane_state *state);

	/**
	 * @atomic_set_property:
	 *
	 * Decode a driver-private property value and store the decoded value
	 * into the passed-in state structure. Since the atomic core decodes all
	 * standardized properties (even for extensions beyond the core set of
	 * properties which might not be implemented by all drivers) this
	 * requires drivers to subclass the state structure.
	 *
	 * Such driver-private properties should really only be implemented for
	 * truly hardware/vendor specific state. Instead it is preferred to
	 * standardize atomic extension and decode the properties used to expose
	 * such an extension in the core.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_plane_set_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * NOTE:
	 *
	 * This function is called in the state assembly phase of atomic
	 * modesets, which can be aborted for any reason (including on
	 * userspace's request to just check whether a configuration would be
	 * possible). Drivers MUST NOT touch any persistent state (hardware or
	 * software) or data structures except the passed in @state parameter.
	 *
	 * Also since userspace controls in which order properties are set this
	 * function must not do any input validation (since the state update is
	 * incomplete and hence likely inconsistent). Instead any such input
	 * validation must be done in the various atomic_check callbacks.
	 *
	 * RETURNS:
	 *
	 * 0 if the property has been found, -EINVAL if the property isn't
	 * implemented by the driver (which shouldn't ever happen, the core only
	 * asks for properties attached to this plane). No other validation is
	 * allowed by the driver. The core already checks that the property
	 * value is within the range (integer, valid enum value, ...) the driver
	 * set when registering the property.
	 */
	int (*atomic_set_property)(struct drm_plane *plane,
				   struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t val);

	/**
	 * @atomic_get_property:
	 *
	 * Reads out the decoded driver-private property. This is used to
	 * implement the GETPLANE IOCTL.
	 *
	 * Do not call this function directly, use
	 * drm_atomic_plane_get_property() instead.
	 *
	 * This callback is optional if the driver does not support any
	 * driver-private atomic properties.
	 *
	 * RETURNS:
	 *
	 * 0 on success, -EINVAL if the property isn't implemented by the
	 * driver (which should never happen, the core only asks for
	 * properties attached to this plane).
	 */
	int (*atomic_get_property)(struct drm_plane *plane,
				   const struct drm_plane_state *state,
				   struct drm_property *property,
				   uint64_t *val);
	/**
	 * @late_register:
	 *
	 * This optional hook can be used to register additional userspace
	 * interfaces attached to the plane like debugfs interfaces.
	 * It is called late in the driver load sequence from drm_dev_register().
	 * Everything added from this callback should be unregistered in
	 * the early_unregister callback.
	 *
	 * Returns:
	 *
	 * 0 on success, or a negative error code on failure.
	 */
	int (*late_register)(struct drm_plane *plane);

	/**
	 * @early_unregister:
	 *
	 * This optional hook should be used to unregister the additional
	 * userspace interfaces attached to the plane from
	 * @late_register. It is called from drm_dev_unregister(),
	 * early in the driver unload sequence to disable userspace access
	 * before data structures are torndown.
	 */
	void (*early_unregister)(struct drm_plane *plane);

	/**
	 * @atomic_print_state:
	 *
	 * If driver subclasses &struct drm_plane_state, it should implement
	 * this optional hook for printing additional driver specific state.
	 *
	 * Do not call this directly, use drm_atomic_plane_print_state()
	 * instead.
	 */
	void (*atomic_print_state)(struct drm_printer *p,
				   const struct drm_plane_state *state);

	/**
	 * @format_mod_supported:
	 *
	 * This optional hook is used for the DRM to determine if the given
	 * format/modifier combination is valid for the plane. This allows the
	 * DRM to generate the correct format bitmask (which formats apply to
	 * which modifier), and to valdiate modifiers at atomic_check time.
	 *
	 * If not present, then any modifier in the plane's modifier
	 * list is allowed with any of the plane's formats.
	 *
	 * Returns:
	 *
	 * True if the given modifier is valid for that format on the plane.
	 * False otherwise.
	 */
	bool (*format_mod_supported)(struct drm_plane *plane, uint32_t format,
				     uint64_t modifier);
};

/**
 * enum drm_plane_type - uapi plane type enumeration
 *
 * For historical reasons not all planes are made the same. This enumeration is
 * used to tell the different types of planes apart to implement the different
 * uapi semantics for them. For userspace which is universal plane aware and
 * which is using that atomic IOCTL there's no difference between these planes
 * (beyong what the driver and hardware can support of course).
 *
 * For compatibility with legacy userspace, only overlay planes are made
 * available to userspace by default. Userspace clients may set the
 * DRM_CLIENT_CAP_UNIVERSAL_PLANES client capability bit to indicate that they
 * wish to receive a universal plane list containing all plane types. See also
 * drm_for_each_legacy_plane().
 *
 * WARNING: The values of this enum is UABI since they're exposed in the "type"
 * property.
 */
enum drm_plane_type {
	/**
	 * @DRM_PLANE_TYPE_OVERLAY:
	 *
	 * Overlay planes represent all non-primary, non-cursor planes. Some
	 * drivers refer to these types of planes as "sprites" internally.
	 */
	DRM_PLANE_TYPE_OVERLAY,

	/**
	 * @DRM_PLANE_TYPE_PRIMARY:
	 *
	 * Primary planes represent a "main" plane for a CRTC.  Primary planes
	 * are the planes operated upon by CRTC modesetting and flipping
	 * operations described in the &drm_crtc_funcs.page_flip and
	 * &drm_crtc_funcs.set_config hooks.
	 */
	DRM_PLANE_TYPE_PRIMARY,

	/**
	 * @DRM_PLANE_TYPE_CURSOR:
	 *
	 * Cursor planes represent a "cursor" plane for a CRTC.  Cursor planes
	 * are the planes operated upon by the DRM_IOCTL_MODE_CURSOR and
	 * DRM_IOCTL_MODE_CURSOR2 IOCTLs.
	 */
	DRM_PLANE_TYPE_CURSOR,
};


/**
 * struct drm_plane - DRM 显示层（扫描输出硬件单元）控制结构体
 *
 * ## 什么是 Plane？
 *
 * Plane（显示层）是显示硬件中负责"图像搬运与合成"的扫描输出单元。
 * 它从帧缓冲（drm_framebuffer）读取像素数据，经过格式转换、缩放、色彩管理后，
 * 输送给 CRTC（drm_crtc）进行最终的时序扫描输出。
 *
 * 在 Rockchip VOP2 中，每个 Video Port（VP，即 CRTC）下挂载若干个 Win（窗口层），
 * 每个 Win 对应一个 Plane，VOP2 硬件在 VBlank 消隐期将多个 Win 的像素数据
 * 按 z-order 叠加合成，最终输出完整的一帧画面。
 *
 * ## Plane 在显示管道中的位置
 *
 *   DRAM（帧缓冲）
 *       │  DMA 搬运像素数据
 *       ▼
 *   drm_plane（Win）── 格式转换、缩放、旋转、色彩空间转换
 *       │
 *       ▼
 *   drm_crtc（VP）── 多 Plane 叠加合成 → 生成时序信号
 *       │
 *       ▼
 *   drm_encoder → drm_connector → 物理屏幕
 *
 * ## 三种 Plane 类型（enum drm_plane_type）
 *
 *   DRM_PLANE_TYPE_PRIMARY（主层）
 *     每个 CRTC 必须有且仅有一个 Primary Plane，是旧式 modeset ioctl
 *     操作的对象（set_config / page_flip 直接操作它）。
 *     通常承载全屏背景内容（桌面壁纸、视频主画面）。
 *
 *   DRM_PLANE_TYPE_OVERLAY（叠加层 / Sprite）
 *     可选的额外硬件图层，用于在 Primary 上方叠加视频、弹幕、UI 控件等，
 *     避免 CPU/GPU 软件合成，节省带宽和功耗。
 *
 *   DRM_PLANE_TYPE_CURSOR（光标层）
 *     专用于鼠标光标，通过 DRM_IOCTL_MODE_CURSOR 独立控制，
 *     移动光标时只更新光标 Plane 的位置，无需重新提交整个帧，延迟极低。
 *
 * ## "不可变描述符" vs "可变状态"
 *
 * drm_plane 本身是**不可变的硬件描述符**，描述硬件能力（支持哪些格式、
 * 能绑定哪些 CRTC、有哪些属性）。
 *
 * 每帧的实际配置（当前绑定的帧缓冲、位置、缩放比例等）存储在
 * drm_plane_state 中，每次原子提交都会创建新的 state 对象，
 * 通过 drm_atomic_helper_swap_state() 原子替换。
 */
struct drm_plane {
	/**
	 * @dev: 此 Plane 归属的 DRM 设备。
	 * 反向引用父设备，用于访问 drm_mode_config 等全局配置。
	 */
	struct drm_device *dev;

	/**
	 * @head: 挂入全局 Plane 链表的节点。
	 *
	 * 链表头：drm_mode_config.plane_list
	 *
	 * 系统中所有 Plane 都通过此节点串联，链表结构在 @dev 生命周期内
	 * 不可变（注册后不会增删），因此遍历时无需加锁。
	 * 可通过 drm_for_each_plane() 宏遍历所有 Plane。
	 */
	struct list_head head;

	/**
	 * @name: 此 Plane 的可读名称字符串。
	 *
	 * 默认由 DRM 核心根据类型和索引自动生成（如 "primary-0"、"overlay-1"），
	 * 驱动可以覆盖为更有意义的名称（如 Rockchip 的 "win0-0"、"win2-2"）。
	 * 主要用于 debugfs 输出和日志，方便调试时识别具体硬件层。
	 */
	char *name;

	/**
	 * @mutex: 保护 Plane 模式设置状态的锁。
	 *
	 * 保护范围：
	 *   - 当 Plane 处于活跃状态、正在激活或正在禁用时，
	 *     与其绑定的 drm_crtc.mutex 联合保护整个状态变更序列
	 *   - 对于原子驱动，专门保护 @state 指针的读写
	 *
	 * 非阻塞原子提交的无锁访问（重要）：
	 *   非阻塞提交在 commit_work 工作队列中访问 Plane 状态时，
	 *   并不持有此锁，而是依赖以下两种无锁安全机制：
	 *   1. 通过 drm_atomic_state 的快照指针访问（old/new state 已固化，不会再变）
	 *      遍历宏：for_each_oldnew_plane_in_state()
	 *              for_each_old_plane_in_state()
	 *              for_each_new_plane_in_state()
	 *   2. 依赖 drm_crtc_commit 的提交顺序保证（前一次提交完成后，
	 *      @state 才会被下一次提交修改，形成隐式的顺序保护）
	 */
	struct drm_modeset_lock mutex;

	/**
	 * @base: 继承自 drm_mode_object 的基类。
	 *
	 * 使 Plane 成为标准 KMS 对象，拥有全局唯一的对象 ID。
	 * 用户空间通过此 ID 在原子 ioctl 中引用该 Plane，
	 * 并查询/设置其属性（DRM_IOCTL_MODE_OBJ_GETPROPERTIES 等）。
	 */
	struct drm_mode_object base;

	/**
	 * @possible_crtcs: 此 Plane 可以绑定的 CRTC 位掩码。
	 *
	 * 每一位对应一个 CRTC，由 drm_crtc_mask(crtc) 生成。
	 * 硬件上并非所有 Plane 都能连接到任意 CRTC：
	 *
	 * 在 Rockchip VOP2 中：
	 *   - Win0/Win1 是全功能层，可接 VP0/VP1/VP2 中的任意一个
	 *   - Win2/Win3 是简化层，只支持 RGB 格式，也可接任意 VP
	 *   - 具体哪个 Win 连哪个 VP 由原子提交时的 "CRTC_ID" 属性决定
	 *
	 * 合成器在绑定 Plane 到 CRTC 前，必须检查此掩码：
	 *   if (possible_crtcs & drm_crtc_mask(target_crtc)) → 允许绑定
	 */
	uint32_t possible_crtcs;

	/**
	 * @format_types: 此 Plane 硬件支持的像素格式数组（fourcc 格式码）。
	 *
	 * 数组中每个元素是一个 fourcc 格式码（如 DRM_FORMAT_XRGB8888、
	 * DRM_FORMAT_NV12、DRM_FORMAT_YUYV 等），由驱动在注册 Plane 时填充。
	 * 用户空间通过 DRM_IOCTL_MODE_GETPLANE 查询此列表，
	 * 在为 Plane 绑定帧缓冲前确认格式兼容性。
	 *
	 * Rockchip VOP2 全功能 Win 通常支持 RGB、YUV 4:2:0、YUV 4:2:2 等多种格式，
	 * 简化 Win（Win2/Win3）通常只支持 RGB 系列格式。
	 */
	uint32_t *format_types;

	/** @format_count: @format_types 数组的元素数量。 */
	unsigned int format_count;

	/**
	 * @format_default: 驱动未提供格式列表的标志位。
	 *
	 * true 表示驱动使用旧式 drm_plane_init() 注册时未指定格式列表，
	 * DRM 核心将使用内置的默认格式集合。
	 * 仅用于旧式驱动兼容包装层，现代驱动不应依赖此标志。
	 */
	bool format_default;

	/**
	 * @modifiers: 此 Plane 支持的帧缓冲修饰符（modifier）数组。
	 *
	 * Modifier 描述帧缓冲在内存中的特殊布局方式，超出标准线性格式的范围：
	 *   DRM_FORMAT_MOD_LINEAR        → 标准线性布局（逐行存储）
	 *   DRM_FORMAT_MOD_ROCKCHIP_AFBC → ARM AFBC（自适应帧缓冲压缩）布局
	 *   DRM_FORMAT_MOD_ARM_AFBC(...)  → 带各种标志的 AFBC 变体
	 *
	 * GPU 在渲染时可以直接输出 AFBC 压缩格式，VOP2 的硬件解压单元
	 * 可以直接读取并实时解压，减少 DDR 带宽消耗约 30-50%。
	 * 驱动通过 funcs->format_mod_supported() 回调进一步校验
	 * 特定格式与特定 modifier 的组合是否被硬件支持。
	 */
	uint64_t *modifiers;

	/** @modifier_count: @modifiers 数组的元素数量。 */
	unsigned int modifier_count;

	/**
	 * @crtc: 当前绑定的 CRTC（仅旧式非原子驱动使用）。
	 *
	 * 对于原子驱动，此字段**强制为 NULL**，运行时的绑定关系
	 * 统一存储在 drm_plane_state.crtc 中，通过原子状态机制管理。
	 *
	 * 旧式驱动直接读写此字段来记录 Plane 当前连接到哪个 CRTC，
	 * 但这种方式无法保证原子性，已被原子驱动模型取代。
	 */
	struct drm_crtc *crtc;

	/**
	 * @fb: 当前绑定的帧缓冲（仅旧式非原子驱动使用）。
	 *
	 * 对于原子驱动，此字段**强制为 NULL**，当前帧缓冲
	 * 统一存储在 drm_plane_state.fb 中，通过原子提交切换。
	 *
	 * 旧式 page flip 直接修改此字段，无法保证帧边界原子性。
	 */
	struct drm_framebuffer *fb;

	/**
	 * @old_fb: modeset 进行期间临时保存旧帧缓冲的指针（仅旧式驱动使用）。
	 *
	 * 旧式 modeset 序列中，在新帧缓冲生效前临时保存旧帧缓冲指针，
	 * 以便 modeset 完成后释放旧帧缓冲的引用计数。
	 * 原子驱动强制为 NULL，旧帧缓冲的释放通过 drm_atomic_state
	 * 的 old_state 机制统一处理（drm_atomic_helper_cleanup_planes）。
	 */
	struct drm_framebuffer *old_fb;

	/**
	 * @funcs: Plane 控制函数表（drm_plane_funcs）。
	 *
	 * 包含 Plane 的核心操作回调，主要有：
	 *   update_plane()        → 旧式非原子更新（旧驱动）
	 *   disable_plane()       → 旧式禁用（旧驱动）
	 *   destroy()             → 释放 Plane 资源
	 *   reset()               → 重置为初始状态（初始化或 GPU 复位时）
	 *   atomic_duplicate_state() → 深拷贝当前 state，供原子提交使用
	 *   atomic_destroy_state()   → 释放一个 drm_plane_state 对象
	 *   atomic_set_property()    → 写驱动私有属性到 state
	 *   atomic_get_property()    → 从 state 读取驱动私有属性
	 *   format_mod_supported()   → 校验 format+modifier 组合是否支持
	 */
	const struct drm_plane_funcs *funcs;

	/**
	 * @properties: 此 Plane 已附加的属性集合。
	 *
	 * 存储所有通过 drm_object_attach_property() 附加到此 Plane 的属性，
	 * 包括 DRM 核心的标准属性和驱动私有属性：
	 *
	 * 标准属性（原子驱动自动具备）：
	 *   "FB_ID"      → 绑定的帧缓冲 ID（Object 类型）
	 *   "CRTC_ID"    → 绑定的 CRTC ID（Object 类型）
	 *   "SRC_X/Y/W/H"→ 帧缓冲裁剪区域（16.16 定点数）
	 *   "CRTC_X/Y/W/H"→ 在 CRTC 上的目标显示区域
	 *   "type"       → Plane 类型（PRIMARY/OVERLAY/CURSOR，只读）
	 *
	 * 可选标准属性（驱动按需创建）：
	 *   见下方 @alpha_property、@zpos_property 等字段
	 */
	struct drm_object_properties properties;

	/**
	 * @type: Plane 类型（PRIMARY / OVERLAY / CURSOR）。
	 *
	 * 见 enum drm_plane_type 的详细说明。
	 * 此值是 UABI 的一部分，通过 "type" 属性暴露给用户空间，一旦注册不可更改。
	 *
	 * 历史兼容性说明：
	 *   旧式用户空间（不支持 Universal Planes）默认只能看到 OVERLAY 类型的 Plane。
	 *   用户空间需设置 DRM_CLIENT_CAP_UNIVERSAL_PLANES 能力位后，
	 *   才能看到 PRIMARY 和 CURSOR 类型的 Plane，从而通过原子 ioctl 控制它们。
	 */
	enum drm_plane_type type;

	/**
	 * @index: 此 Plane 在 mode_config 全局列表中的位置索引。
	 *
	 * 从 0 开始连续编号，在 Plane 整个生命周期内不变。
	 * 可直接用作数组下标，例如：
	 *   drm_atomic_state.planes[plane->index]  → 快速访问此 Plane 的原子状态
	 * 避免了每次访问都需要遍历链表查找的开销。
	 */
	unsigned index;

	/**
	 * @helper_private: 中间层辅助函数表（drm_plane_helper_funcs）。
	 *
	 * 由 drm_plane_helper_add() 设置，是驱动实现原子提交的核心接口：
	 *   prepare_fb()    → 在提交前 pin 帧缓冲显存、设置 in-fence（隐式同步）
	 *   cleanup_fb()    → 提交完成后 unpin 旧帧缓冲显存
	 *   atomic_check()  → 校验新 state 的参数合法性（缩放比例、格式等）
	 *   atomic_update() → 将新 state 写入硬件寄存器（DMA 地址、位置、格式）
	 *   atomic_disable()→ 禁用此 Plane（关闭 DMA 搬运）
	 */
	const struct drm_plane_helper_funcs *helper_private;

	/**
	 * @state: 此 Plane 当前生效的原子状态（drm_plane_state）。
	 *
	 * ## 原子提交与 @state 的关系（核心逻辑）
	 *
	 * 原子提交流程中，@state 的切换发生在 drm_atomic_helper_swap_state()：
	 *
	 *   提交前：plane->state → 旧状态（当前硬件正在扫描的配置）
	 *                          drm_atomic_state.planes[i].new_state → 新状态
	 *
	 *   swap_state() 执行后：
	 *     plane->state           → 新状态（目标配置）
	 *     drm_atomic_state 里    → 旧状态（供清理使用）
	 *
	 *   atomic_commit_tail() 执行期间，驱动读取 plane->state 来写硬件寄存器，
	 *   读取 drm_atomic_state 里的旧状态来释放旧帧缓冲。
	 *
	 * ## 并发保护
	 *
	 * 正常路径：持有 @mutex 后读写 @state
	 *
	 * 非阻塞提交的无锁路径（commit_work 工作队列中）：
	 *   不持有 @mutex，但通过以下方式保证安全：
	 *   - 使用 drm_atomic_state 快照中的 old/new state 指针（已固化，不再变化）
	 *   - 依赖 drm_crtc_commit 的完成顺序：下一次提交必须等上一次 commit_hw_done
	 *     之后才能开始，所以 commit_work 运行期间 @state 不会被并发修改
	 */
	struct drm_plane_state *state;

	/**
	 * @alpha_property: 全局透明度属性（可选）。
	 *
	 * 控制整个 Plane 的透明度（0 = 完全透明，0xFFFF = 完全不透明）。
	 * 与像素自身的 alpha 通道不同，这是一个"全局乘数"，
	 * 叠加在像素 alpha 之上进行混合计算。
	 * 通过 drm_plane_create_alpha_property() 创建，对应 "alpha" 属性。
	 */
	struct drm_property *alpha_property;

	/**
	 * @zpos_property: Z 轴叠加顺序属性（可选）。
	 *
	 * 控制多个 Plane 叠加时的上下层次关系（z-order）。
	 * z-pos 值越大，越靠近用户（在上方）；值越小，越靠近背景（在下方）。
	 * 硬件上对应 VOP2 Win 的优先级配置寄存器。
	 * 通过 drm_plane_create_zpos_property() 创建，
	 * 可以是固定顺序（immutable）或可动态调整（mutable）。
	 */
	struct drm_property *zpos_property;

	/**
	 * @rotation_property: 旋转/翻转属性（可选）。
	 *
	 * 控制 Plane 内容的旋转和镜像翻转，值为位掩码组合：
	 *   DRM_MODE_ROTATE_0/90/180/270   → 顺时针旋转角度
	 *   DRM_MODE_REFLECT_X/Y           → 水平/垂直翻转
	 * 由硬件在扫描读取像素时实时完成，无需 GPU 参与。
	 * 通过 drm_plane_create_rotation_property() 创建。
	 */
	struct drm_property *rotation_property;

	/**
	 * @blend_mode_property: 像素混合模式属性（可选）。
	 *
	 * 控制此 Plane 与其下方内容（背景/其他 Plane）的 Alpha 混合方程：
	 *   "None"              → 忽略 alpha，直接覆盖（src over，alpha=1）
	 *   "Pre-multiplied"    → 源像素已预乘 alpha（标准 Porter-Duff "src over"）
	 *                          result = src_alpha * src + (1 - src_alpha) * dst
	 *   "Coverage"          → 源像素未预乘 alpha（需要额外乘法）
	 *                          result = src_alpha * src + (1 - src_alpha) * dst
	 *                          （与 Pre-multiplied 计算公式相同，但输入像素格式不同）
	 * 通过 drm_plane_create_blend_mode_property() 创建，
	 * 对应 "pixel blend mode" 属性。
	 */
	struct drm_property *blend_mode_property;

	/**
	 * @color_encoding_property: 非 RGB 格式的色彩编码标准属性（可选）。
	 *
	 * 指定 YUV 像素数据所使用的色彩编码标准（色彩空间矩阵系数）：
	 *   DRM_COLOR_YCBCR_BT601  → SD 标准清晰度视频（SDTV）
	 *   DRM_COLOR_YCBCR_BT709  → HD 高清视频（HDTV）
	 *   DRM_COLOR_YCBCR_BT2020 → UHD 超高清 / HDR 视频
	 *
	 * VOP2 的 CSC（色彩空间转换）单元根据此属性选择对应的 YCbCr→RGB 转换矩阵，
	 * 确保 YUV 视频画面的色彩被正确解码还原。
	 * 对纯 RGB 格式的 Plane 无意义。
	 * 通过 drm_plane_create_color_properties() 创建，对应 "COLOR_ENCODING" 属性。
	 */
	struct drm_property *color_encoding_property;

	/**
	 * @color_range_property: 非 RGB 格式的色彩值域范围属性（可选）。
	 *
	 * 指定 YUV 像素数据的量化范围（值域）：
	 *   DRM_COLOR_YCBCR_LIMITED_RANGE → 受限范围（Studio Swing）
	 *     Y: 16-235，CbCr: 16-240，是广播电视和视频编解码的默认标准
	 *   DRM_COLOR_YCBCR_FULL_RANGE    → 全范围（Full Swing）
	 *     Y/CbCr: 0-255，部分摄像头和 JPEG 使用此范围
	 *
	 * 若范围设置错误，会导致画面偏色（颜色偏淡或偏浓）或色阶丢失（黑不够黑、白不够白）。
	 * VOP2 的 CSC 单元需要结合 @color_encoding_property 和此属性
	 * 共同确定 YCbCr→RGB 的完整转换参数。
	 * 通过 drm_plane_create_color_properties() 创建，对应 "COLOR_RANGE" 属性。
	 */
	struct drm_property *color_range_property;
};

#define obj_to_plane(x) container_of(x, struct drm_plane, base)

__printf(9, 10)
int drm_universal_plane_init(struct drm_device *dev,
			     struct drm_plane *plane,
			     uint32_t possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats,
			     unsigned int format_count,
			     const uint64_t *format_modifiers,
			     enum drm_plane_type type,
			     const char *name, ...);
int drm_plane_init(struct drm_device *dev,
		   struct drm_plane *plane,
		   uint32_t possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, unsigned int format_count,
		   bool is_primary);
void drm_plane_cleanup(struct drm_plane *plane);

/**
 * drm_plane_index - find the index of a registered plane
 * @plane: plane to find index for
 *
 * Given a registered plane, return the index of that plane within a DRM
 * device's list of planes.
 */
static inline unsigned int drm_plane_index(const struct drm_plane *plane)
{
	return plane->index;
}

/**
 * drm_plane_mask - find the mask of a registered plane
 * @plane: plane to find mask for
 */
static inline u32 drm_plane_mask(const struct drm_plane *plane)
{
	return 1 << drm_plane_index(plane);
}

struct drm_plane * drm_plane_from_index(struct drm_device *dev, int idx);
void drm_plane_force_disable(struct drm_plane *plane);

int drm_mode_plane_set_obj_prop(struct drm_plane *plane,
				       struct drm_property *property,
				       uint64_t value);

/**
 * drm_plane_find - find a &drm_plane
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: plane id
 *
 * Returns the plane with @id, NULL if it doesn't exist. Simple wrapper around
 * drm_mode_object_find().
 */
static inline struct drm_plane *drm_plane_find(struct drm_device *dev,
		struct drm_file *file_priv,
		uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_PLANE);
	return mo ? obj_to_plane(mo) : NULL;
}

/**
 * drm_for_each_plane_mask - iterate over planes specified by bitmask
 * @plane: the loop cursor
 * @dev: the DRM device
 * @plane_mask: bitmask of plane indices
 *
 * Iterate over all planes specified by bitmask.
 */
#define drm_for_each_plane_mask(plane, dev, plane_mask) \
	list_for_each_entry((plane), &(dev)->mode_config.plane_list, head) \
		for_each_if ((plane_mask) & drm_plane_mask(plane))

/**
 * drm_for_each_legacy_plane - iterate over all planes for legacy userspace
 * @plane: the loop cursor
 * @dev: the DRM device
 *
 * Iterate over all legacy planes of @dev, excluding primary and cursor planes.
 * This is useful for implementing userspace apis when userspace is not
 * universal plane aware. See also &enum drm_plane_type.
 */
#define drm_for_each_legacy_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head) \
		for_each_if (plane->type == DRM_PLANE_TYPE_OVERLAY)

/**
 * drm_for_each_plane - iterate over all planes
 * @plane: the loop cursor
 * @dev: the DRM device
 *
 * Iterate over all planes of @dev, include primary and cursor planes.
 */
#define drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)


#endif
