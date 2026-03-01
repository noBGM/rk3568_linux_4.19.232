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

#ifndef __DRM_MODE_CONFIG_H__
#define __DRM_MODE_CONFIG_H__

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/llist.h>

#include <drm/drm_modeset_lock.h>

struct drm_file;
struct drm_device;
struct drm_atomic_state;
struct drm_mode_fb_cmd2;
struct drm_format_info;
struct drm_display_mode;

/**
 * struct drm_mode_config_funcs - basic driver provided mode setting functions
 *
 * Some global (i.e. not per-CRTC, connector, etc) mode setting functions that
 * involve drivers.
 */
struct drm_mode_config_funcs {
	/**
	 * @fb_create:
	 *
	 * Create a new framebuffer object. The core does basic checks on the
	 * requested metadata, but most of that is left to the driver. See
	 * &struct drm_mode_fb_cmd2 for details.
	 *
	 * If the parameters are deemed valid and the backing storage objects in
	 * the underlying memory manager all exist, then the driver allocates
	 * a new &drm_framebuffer structure, subclassed to contain
	 * driver-specific information (like the internal native buffer object
	 * references). It also needs to fill out all relevant metadata, which
	 * should be done by calling drm_helper_mode_fill_fb_struct().
	 *
	 * The initialization is finalized by calling drm_framebuffer_init(),
	 * which registers the framebuffer and makes it accessible to other
	 * threads.
	 *
	 * RETURNS:
	 *
	 * A new framebuffer with an initial reference count of 1 or a negative
	 * error code encoded with ERR_PTR().
	 */
	struct drm_framebuffer *(*fb_create)(struct drm_device *dev,
					     struct drm_file *file_priv,
					     const struct drm_mode_fb_cmd2 *mode_cmd);

	/**
	 * @get_format_info:
	 *
	 * Allows a driver to return custom format information for special
	 * fb layouts (eg. ones with auxiliary compression control planes).
	 *
	 * RETURNS:
	 *
	 * The format information specific to the given fb metadata, or
	 * NULL if none is found.
	 */
	const struct drm_format_info *(*get_format_info)(const struct drm_mode_fb_cmd2 *mode_cmd);

	/**
	 * @output_poll_changed:
	 *
	 * Callback used by helpers to inform the driver of output configuration
	 * changes.
	 *
	 * Drivers implementing fbdev emulation with the helpers can call
	 * drm_fb_helper_hotplug_changed from this hook to inform the fbdev
	 * helper of output changes.
	 *
	 * FIXME:
	 *
	 * Except that there's no vtable for device-level helper callbacks
	 * there's no reason this is a core function.
	 */
	void (*output_poll_changed)(struct drm_device *dev);

	/**
	 * @mode_valid:
	 *
	 * Device specific validation of display modes. Can be used to reject
	 * modes that can never be supported. Only device wide constraints can
	 * be checked here. crtc/encoder/bridge/connector specific constraints
	 * should be checked in the .mode_valid() hook for each specific object.
	 */
	enum drm_mode_status (*mode_valid)(struct drm_device *dev,
					   const struct drm_display_mode *mode);

	/**
	 * @atomic_check:
	 *
	 * This is the only hook to validate an atomic modeset update. This
	 * function must reject any modeset and state changes which the hardware
	 * or driver doesn't support. This includes but is of course not limited
	 * to:
	 *
	 *  - Checking that the modes, framebuffers, scaling and placement
	 *    requirements and so on are within the limits of the hardware.
	 *
	 *  - Checking that any hidden shared resources are not oversubscribed.
	 *    This can be shared PLLs, shared lanes, overall memory bandwidth,
	 *    display fifo space (where shared between planes or maybe even
	 *    CRTCs).
	 *
	 *  - Checking that virtualized resources exported to userspace are not
	 *    oversubscribed. For various reasons it can make sense to expose
	 *    more planes, crtcs or encoders than which are physically there. One
	 *    example is dual-pipe operations (which generally should be hidden
	 *    from userspace if when lockstepped in hardware, exposed otherwise),
	 *    where a plane might need 1 hardware plane (if it's just on one
	 *    pipe), 2 hardware planes (when it spans both pipes) or maybe even
	 *    shared a hardware plane with a 2nd plane (if there's a compatible
	 *    plane requested on the area handled by the other pipe).
	 *
	 *  - Check that any transitional state is possible and that if
	 *    requested, the update can indeed be done in the vblank period
	 *    without temporarily disabling some functions.
	 *
	 *  - Check any other constraints the driver or hardware might have.
	 *
	 *  - This callback also needs to correctly fill out the &drm_crtc_state
	 *    in this update to make sure that drm_atomic_crtc_needs_modeset()
	 *    reflects the nature of the possible update and returns true if and
	 *    only if the update cannot be applied without tearing within one
	 *    vblank on that CRTC. The core uses that information to reject
	 *    updates which require a full modeset (i.e. blanking the screen, or
	 *    at least pausing updates for a substantial amount of time) if
	 *    userspace has disallowed that in its request.
	 *
	 *  - The driver also does not need to repeat basic input validation
	 *    like done for the corresponding legacy entry points. The core does
	 *    that before calling this hook.
	 *
	 * See the documentation of @atomic_commit for an exhaustive list of
	 * error conditions which don't have to be checked at the in this
	 * callback.
	 *
	 * See the documentation for &struct drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_check(), or one of the exported sub-functions of
	 * it.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EINVAL, if any of the above constraints are violated.
	 *
	 *  - -EDEADLK, when returned from an attempt to acquire an additional
	 *    &drm_modeset_lock through drm_modeset_lock().
	 *
	 *  - -ENOMEM, if allocating additional state sub-structures failed due
	 *    to lack of memory.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point all errors are
	 *    treated equally.
	 */
	int (*atomic_check)(struct drm_device *dev,
			    struct drm_atomic_state *state);

	/**
	 * @atomic_commit:
	 *
	 * This is the only hook to commit an atomic modeset update. The core
	 * guarantees that @atomic_check has been called successfully before
	 * calling this function, and that nothing has been changed in the
	 * interim.
	 *
	 * See the documentation for &struct drm_atomic_state for how exactly
	 * an atomic modeset update is described.
	 *
	 * Drivers using the atomic helpers can implement this hook using
	 * drm_atomic_helper_commit(), or one of the exported sub-functions of
	 * it.
	 *
	 * Nonblocking commits (as indicated with the nonblock parameter) must
	 * do any preparatory work which might result in an unsuccessful commit
	 * in the context of this callback. The only exceptions are hardware
	 * errors resulting in -EIO. But even in that case the driver must
	 * ensure that the display pipe is at least running, to avoid
	 * compositors crashing when pageflips don't work. Anything else,
	 * specifically committing the update to the hardware, should be done
	 * without blocking the caller. For updates which do not require a
	 * modeset this must be guaranteed.
	 *
	 * The driver must wait for any pending rendering to the new
	 * framebuffers to complete before executing the flip. It should also
	 * wait for any pending rendering from other drivers if the underlying
	 * buffer is a shared dma-buf. Nonblocking commits must not wait for
	 * rendering in the context of this callback.
	 *
	 * An application can request to be notified when the atomic commit has
	 * completed. These events are per-CRTC and can be distinguished by the
	 * CRTC index supplied in &drm_event to userspace.
	 *
	 * The drm core will supply a &struct drm_event in each CRTC's
	 * &drm_crtc_state.event. See the documentation for
	 * &drm_crtc_state.event for more details about the precise semantics of
	 * this event.
	 *
	 * NOTE:
	 *
	 * Drivers are not allowed to shut down any display pipe successfully
	 * enabled through an atomic commit on their own. Doing so can result in
	 * compositors crashing if a page flip is suddenly rejected because the
	 * pipe is off.
	 *
	 * RETURNS:
	 *
	 * 0 on success or one of the below negative error codes:
	 *
	 *  - -EBUSY, if a nonblocking updated is requested and there is
	 *    an earlier updated pending. Drivers are allowed to support a queue
	 *    of outstanding updates, but currently no driver supports that.
	 *    Note that drivers must wait for preceding updates to complete if a
	 *    synchronous update is requested, they are not allowed to fail the
	 *    commit in that case.
	 *
	 *  - -ENOMEM, if the driver failed to allocate memory. Specifically
	 *    this can happen when trying to pin framebuffers, which must only
	 *    be done when committing the state.
	 *
	 *  - -ENOSPC, as a refinement of the more generic -ENOMEM to indicate
	 *    that the driver has run out of vram, iommu space or similar GPU
	 *    address space needed for framebuffer.
	 *
	 *  - -EIO, if the hardware completely died.
	 *
	 *  - -EINTR, -EAGAIN or -ERESTARTSYS, if the IOCTL should be restarted.
	 *    This can either be due to a pending signal, or because the driver
	 *    needs to completely bail out to recover from an exceptional
	 *    situation like a GPU hang. From a userspace point of view all errors are
	 *    treated equally.
	 *
	 * This list is exhaustive. Specifically this hook is not allowed to
	 * return -EINVAL (any invalid requests should be caught in
	 * @atomic_check) or -EDEADLK (this function must not acquire
	 * additional modeset locks).
	 */
	int (*atomic_commit)(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool nonblock);

	/**
	 * @atomic_state_alloc:
	 *
	 * This optional hook can be used by drivers that want to subclass struct
	 * &drm_atomic_state to be able to track their own driver-private global
	 * state easily. If this hook is implemented, drivers must also
	 * implement @atomic_state_clear and @atomic_state_free.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 *
	 * RETURNS:
	 *
	 * A new &drm_atomic_state on success or NULL on failure.
	 */
	struct drm_atomic_state *(*atomic_state_alloc)(struct drm_device *dev);

	/**
	 * @atomic_state_clear:
	 *
	 * This hook must clear any driver private state duplicated into the
	 * passed-in &drm_atomic_state. This hook is called when the caller
	 * encountered a &drm_modeset_lock deadlock and needs to drop all
	 * already acquired locks as part of the deadlock avoidance dance
	 * implemented in drm_modeset_backoff().
	 *
	 * Any duplicated state must be invalidated since a concurrent atomic
	 * update might change it, and the drm atomic interfaces always apply
	 * updates as relative changes to the current state.
	 *
	 * Drivers that implement this must call drm_atomic_state_default_clear()
	 * to clear common state.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 */
	void (*atomic_state_clear)(struct drm_atomic_state *state);

	/**
	 * @atomic_state_free:
	 *
	 * This hook needs driver private resources and the &drm_atomic_state
	 * itself. Note that the core first calls drm_atomic_state_clear() to
	 * avoid code duplicate between the clear and free hooks.
	 *
	 * Drivers that implement this must call
	 * drm_atomic_state_default_release() to release common resources.
	 *
	 * Subclassing of &drm_atomic_state is deprecated in favour of using
	 * &drm_private_state and &drm_private_obj.
	 */
	void (*atomic_state_free)(struct drm_atomic_state *state);
};

/**
 * struct drm_mode_config - Mode configuration control structure
 * @min_width: minimum fb pixel width on this device
 * @min_height: minimum fb pixel height on this device
 * @max_width: maximum fb pixel width on this device
 * @max_height: maximum fb pixel height on this device
 * @funcs: core driver provided mode setting functions
 * @fb_base: base address of the framebuffer
 * @poll_enabled: track polling support for this device
 * @poll_running: track polling status for this device
 * @delayed_event: track delayed poll uevent deliver for this device
 * @output_poll_work: delayed work for polling in process context
 * @preferred_depth: preferred RBG pixel depth, used by fb helpers
 * @prefer_shadow: hint to userspace to prefer shadow-fb rendering
 * @cursor_width: hint to userspace for max cursor width
 * @cursor_height: hint to userspace for max cursor height
 * @helper_private: mid-layer private data
 *
 * Core mode resource tracking structure.  All CRTC, encoders, and connectors
 * enumerated by the driver are added here, as are global properties.  Some
 * global restrictions are also here, e.g. dimension restrictions.
 */

/**
 * struct drm_mode_config - DRM模式设置（modeset）核心配置结构体
 * drm_mode_config
 * 定义：KMS 子系统的全局资源管理器与配置中心，是整个 KMS 的 “大脑”，归属 struct drm_device 管理。
 * 核心作用：
 * 枚举并管理所有 KMS 硬件对象（CRTC/Plane/Encoder/Connector）；
 * 存储全局显示配置（如优选分辨率、帧率限制）；
 * 管理原子事务、vblank 事件、热插拔事件；
 * 提供全局锁保护 KMS 资源。
 */
struct drm_mode_config {
	/**
	 * @mutex: 模式设置全局互斥锁（兼容传统BKL大锁）
	 *
	 * 这是保护模式设置的“大而全”互斥锁，用于保护所有未被其他细粒度锁覆盖的资源，
	 * 其保护范围原本模糊且宽泛，内核社区推荐逐步将资源迁移到更细粒度的锁下，减少对该锁的依赖。
	 *
	 * 该锁最核心的保护对象是@acquire_ctx的使用，任何访问@acquire_ctx的操作都必须持有此锁。
	 * 注释原文：保护所有未被其他方式保护的资源，范围模糊，建议迁移到细粒度锁；核心保护@acquire_ctx的使用。
	 */
	struct mutex mutex;

	/**
	 * @connection_mutex: 连接拓扑互斥锁（模式设置专用锁）
	 *
	 * 保护连接器（connector）状态，以及“连接器→编码器（encoder）→CRTC”的路由链路，
	 * 是显示设备连接关系的核心保护锁。
	 *
	 * 对于原子模式（atomic）驱动，该锁专门保护&drm_connector.state（连接器状态结构体），
	 * 防止并发修改连接拓扑导致的逻辑错误。
	 */
	struct drm_modeset_lock connection_mutex;

	/**
	 * @acquire_ctx: 全局隐式模式设置获取上下文
	 *
	 * 原子驱动为兼容传统IOCTL接口而保留的全局隐式获取上下文，已被标记为废弃（Deprecated）。
	 * 原因是隐式锁上下文无法使用驱动私有&struct drm_modeset_lock，限制了锁机制的灵活性。
	 *
	 * 任何使用该上下文的操作都必须先持有@mutex锁，否则会导致竞态问题。
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;

	/**
	 * @idr_mutex: KMS ID分配互斥锁
	 *
	 * 用于保护KMS（Kernel Mode Setting）ID的分配与管理过程，具体保护@crtc_idr和@tile_idr两个IDR对象，
	 * 防止并发分配/释放ID导致的重复或悬空ID问题。
	 */
	struct mutex idr_mutex;

	/**
	 * @crtc_idr: KMS全局ID管理对象
	 *
	 * KMS子系统的主ID跟踪器，统一管理所有类型的显示资源ID：帧缓冲（fb）、CRTC、连接器、显示模式（modes）等，
	 * 内核设计为“单IDR管理所有资源”，简化ID分配逻辑，降低维护成本。
	 *
	 * IDR（Integer ID Resource）是内核用于将整数ID映射到指针的高效数据结构，替代传统数组/哈希表。
	 */
	struct idr crtc_idr;

	/**
	 * @tile_idr: 分块显示ID管理对象
	 *
	 * 专门为“分块接收器（tiled sinks）”分配新ID的IDR，典型场景是高分辨率DP MST（DisplayPort Multi-Stream Transport）屏幕，
	 * 这类屏幕通过分块方式显示内容，需要独立的ID空间管理，因此单独拆分出该IDR。
	 */
	struct idr tile_idr;

	/** @fb_lock: Mutex to protect fb the global @fb_list and @num_fb. */
	struct mutex fb_lock;
	/** @num_fb: Number of entries on @fb_list. */
	int num_fb;
	/** @fb_list: List of all &struct drm_framebuffer. */
	struct list_head fb_list;

	/**
	 * @connector_list_lock: 保护 @num_connector、@connector_list 和
	 * @connector_free_list 的自旋锁。
	 *
	 * 使用 spinlock（而非 mutex）的原因：
	 * connector 的引用计数归零可能发生在中断上下文（如 HPD 中断处理路径），
	 * 此时需要能在不可睡眠的环境下安全操作链表，spinlock 满足此要求。
	 * 内部使用 irqsave 变体（spin_lock_irqsave）防止中断嵌套死锁。
	 */
	spinlock_t connector_list_lock;

	/**
	 * @num_connector: 当前已注册的 connector 数量。
	 * 受 @connector_list_lock 保护。
	 * 每次 drm_connector_init() 时 +1，drm_connector_cleanup() 时 -1。
	 */
	int num_connector;

	/**
	 * @connector_ida: connector 索引的 ID 分配器。
	 *
	 * 为每个 connector 分配唯一的整型索引（connector->index），
	 * 该索引用于用户空间通过 ioctl 枚举 connector 时的标识。
	 * 与 drm_mode_object 的全局 ID（connector->base.id）不同：
	 *   - base.id：全局唯一，跨所有 KMS 对象类型
	 *   - index：仅在 connector 类型内唯一，从 0 起连续分配
	 */
	struct ida connector_ida;

	/**
	 * @connector_list: 所有已注册 connector 的主链表。
	 *
	 * 通过 drm_connector.head 成员挂入，受 @connector_list_lock 保护。
	 *
	 * ## 遍历规则（重要）
	 *
	 * 禁止直接用 list_for_each_entry 遍历此链表，必须使用：
	 *   drm_for_each_connector_iter(conn, &iter)
	 * 配合 struct drm_connector_list_iter 使用。
	 *
	 * 原因：遍历过程中可能发生 connector 的引用计数归零（如热拔出），
	 * drm_connector_list_iter 内部会安全地处理这种情况，防止访问已释放的内存。
	 * 直接遍历在持锁期间调用 destroy 会导致死锁或 use-after-free。
	 */
	struct list_head connector_list;

	/**
	 * @connector_free_list: 等待异步释放的 connector 延迟释放队列。
	 *
	 * ## 为什么需要延迟释放？
	 *
	 * connector 的释放（destroy 回调）可能需要睡眠（如释放 EDID、
	 * 注销 sysfs、unmap 内存等），但引用计数归零的时刻可能在持有
	 * @connector_list_lock（spinlock）的上下文中，spinlock 不允许睡眠。
	 *
	 * 解决方案（两阶段释放）：
	 *   阶段一（原子上下文，持锁中）：
	 *     __drm_connector_put_safe() 中引用计数归零时，
	 *     仅将 connector 通过 llist_add() 挂入此队列，
	 *     然后 schedule_work() 触发异步工作。
	 *
	 *   阶段二（工作队列，可睡眠）：
	 *     connector_free_work（drm_connector_free_work_fn）执行：
	 *       spin_lock_irqsave → llist_del_all → spin_unlock
	 *       然后对每个 connector 调用 destroy()，完成真正的资源释放。
	 *
	 * ## 为什么用 llist_head 而非 list_head？
	 *
	 * llist（lock-less list）是无锁单向链表，llist_add() 使用 cmpxchg
	 * 原子操作，**无需持有 spinlock 也能安全入队**。
	 * 虽然此处入队时实际已持锁，但使用 llist 使 schedule_work 后的
	 * 出队（llist_del_all）同样无锁，减少锁竞争。
	 */
	struct llist_head connector_free_list;

	/**
	 * @connector_free_work: 执行 @connector_free_list 中 connector 延迟释放的工作项。
	 *
	 * 工作函数为 drm_connector_free_work_fn()：
	 *   1. 持锁调用 llist_del_all() 原子摘走整个待释放队列
	 *   2. 释放锁后逐一调用 connector->funcs->destroy()
	 *   3. 调用 drm_mode_object_unregister() 注销 KMS 对象 ID
	 *
	 * 系统退出时 drm_mode_config_cleanup() 调用 flush_work() 等待
	 * 所有延迟释放完成，确保无内存泄漏。
	 */
	struct work_struct connector_free_work;

	/**
	 * @num_encoder:
	 *
	 * Number of encoders on this device. This is invariant over the
	 * lifetime of a device and hence doesn't need any locks.
	 */
	int num_encoder;
	/**
	 * @encoder_list:
	 *
	 * List of encoder objects linked with &drm_encoder.head. This is
	 * invariant over the lifetime of a device and hence doesn't need any
	 * locks.
	 */
	struct list_head encoder_list;

	/**
	 * @num_total_plane:
	 *
	 * Number of universal (i.e. with primary/curso) planes on this device.
	 * This is invariant over the lifetime of a device and hence doesn't
	 * need any locks.
	 */
	int num_total_plane;
	/**
	 * @plane_list:
	 *
	 * List of plane objects linked with &drm_plane.head. This is invariant
	 * over the lifetime of a device and hence doesn't need any locks.
	 */
	struct list_head plane_list;

	/**
	 * @num_crtc:
	 *
	 * Number of CRTCs on this device linked with &drm_crtc.head. This is invariant over the lifetime
	 * of a device and hence doesn't need any locks.
	 */
	int num_crtc;
	/**
	 * @crtc_list:
	 *
	 * List of CRTC objects linked with &drm_crtc.head. This is invariant
	 * over the lifetime of a device and hence doesn't need any locks.
	 */
	struct list_head crtc_list;

	/**
	 * @property_list:
	 *
	 * List of property type objects linked with &drm_property.head. This is
	 * invariant over the lifetime of a device and hence doesn't need any
	 * locks.
	 */
	struct list_head property_list;

	int min_width, min_height;
	int max_width, max_height;
	const struct drm_mode_config_funcs *funcs;
	resource_size_t fb_base;
    /* output poll support */
	/*
	 * 输出轮询（Output Poll）支持字段
	 *
	 * ## 背景：两种显示器热插拔检测方式
	 *
	 * 显示器插拔检测有两条路径，取决于硬件能力：
	 *
	 * 路径 1 — HPD 中断（DRM_CONNECTOR_POLL_HPD）：
	 *   硬件具备专用的热插拔检测引脚（Hot Plug Detect），插拔时触发硬件中断，
	 *   驱动在中断处理函数中调用 drm_kms_helper_hotplug_event() 立即通知上层。
	 *   延迟极低（毫秒级），是 HDMI、DisplayPort 等接口的标准方式。
	 *
	 * 路径 2 — 软件轮询（DRM_CONNECTOR_POLL_CONNECT / POLL_DISCONNECT）：
	 *   硬件没有专用 HPD 引脚，或驱动无法使用 HPD 中断，
	 *   内核通过定时工作队列（delayed_work）周期性地主动探测 Connector 状态。
	 *   VGA 接口是典型场景（无 HPD 引脚，需要主动读取 DDC 或检测电压）。
	 *   轮询周期约 10 秒（DRM_OUTPUT_POLL_PERIOD），存在感知延迟。
	 *
	 * 下面四个字段共同管理路径 2 的软件轮询机制：
	 *
	 * ## 整体工作流程
	 *
	 *  drm_kms_helper_poll_init(dev)
	 *      → 初始化 output_poll_work（执行函数：output_poll_execute）
	 *      → poll_enabled = true
	 *
	 *  drm_kms_helper_poll_enable(dev)
	 *      → 检查是否有 Connector 设置了 POLL_CONNECT 或 POLL_DISCONNECT
	 *      → 若有，schedule_delayed_work(&output_poll_work, 10s)
	 *
	 *  output_poll_execute()  ← 每 10 秒在工作队列中执行一次
	 *      → 遍历所有需要轮询的 Connector，调用 connector->funcs->detect()
	 *      → 若状态变化（插入/拔出），设置 delayed_event = true，
	 *        并立即 schedule_delayed_work(..., 0) 触发下一次执行
	 *      → 下一次执行时读取 delayed_event，
	 *        调用 drm_kms_helper_hotplug_event() 通知用户空间
	 *      → 若还有需要轮询的 Connector，继续 schedule 下一轮（10s 后）
	 *
	 * ## 为什么状态变化后要"延迟一次"才通知用户空间？
	 *
	 * output_poll_execute() 在持有 mode_config.mutex 的情况下调用 detect()，
	 * 而 fb helpers（用户空间响应热插拔时的回调）也需要获取同一把锁。
	 * 为了避免死锁，状态变化后不在当前执行上下文中直接通知，
	 * 而是设置 delayed_event = true，重新投递一次 work（delay = 0），
	 * 在下一次执行（不持锁的情况下）再调用 drm_kms_helper_hotplug_event()。
	 */

	/**
	 * @poll_enabled: 输出轮询机制的总开关。
	 *
	 * true  → 轮询机制已初始化，output_poll_work 可以被调度执行。
	 *         由 drm_kms_helper_poll_init() 在驱动加载时设置为 true。
	 *
	 * false → 轮询机制未初始化或已被禁用（drm_kms_helper_poll_fini() 调用后）。
	 *         output_poll_execute() 在函数入口检查此标志，为 false 则直接返回，
	 *         防止设备关闭后 work 继续执行。
	 *
	 * 注意：此标志控制"轮询是否被初始化"，与 @poll_running 区分：
	 * 即使 poll_enabled=true，若所有 Connector 都用 HPD 中断，
	 * output_poll_work 也不会被调度（无需轮询）。
	 */
	bool poll_enabled;

	/**
	 * @poll_running: 上一次 output_poll_execute() 执行时全局轮询开关的状态。
	 *
	 * 记录上次工作队列执行时 drm_kms_helper_poll（全局开关模块参数）的值，
	 * 用于检测全局开关是否在两次执行之间发生了变化：
	 *
	 *   if (drm_kms_helper_poll != poll_running)
	 *       drm_kms_helper_poll_enable(dev);  // 全局开关状态变了，重新评估
	 *   poll_running = drm_kms_helper_poll;   // 同步记录当前值
	 *
	 * 场景：用户通过 sysfs 将 drm_kms_helper_poll 从 0 改回 1 时，
	 * 下一次 output_poll_execute() 执行会检测到此变化，重新启动轮询调度。
	 * 反之，若全局开关被关闭，则停止调度。
	 */
	bool poll_running;

	/**
	 * @delayed_event: 待处理的热插拔事件标志（两阶段通知机制的中间状态）。
	 *
	 * ## 为什么需要两阶段通知？
	 *
	 * output_poll_execute() 检测到 Connector 状态变化后，不能立即调用
	 * drm_kms_helper_hotplug_event()，因为：
	 *   - hotplug_event 内部会调用 fb helpers
	 *   - fb helpers 需要获取 mode_config.mutex
	 *   - 而 output_poll_execute() 此时可能持有 mode_config.mutex
	 *   - 直接调用会造成死锁
	 *
	 * ## 两阶段通知流程（触发场景：用户空间主动调用 probe）
	 *
	 * 第一阶段（在 drm_helper_probe_single_connector_modes() 持锁区内）：
	 *   用户空间通过 ioctl 触发 probe，函数持有 mode_config.mutex，
	 *   检测到 connector->status 变化后：
	 *   → delayed_event = true          // 标记"有待处理事件"
	 *   → schedule_delayed_work(..., 0) // 延迟 0，立即投递 output_poll_work
	 *   此处不能直接调用 hotplug_event，因为 fb helpers 需要同一把锁，会死锁。
	 *
	 * 第二阶段（output_poll_execute() 在工作队列中执行，不持 mode_config.mutex）：
	 *   changed = delayed_event         // 读取标志（此时已无锁）
	 *   delayed_event = false           // 清除标志
	 *   ...（继续尝试轮询其他 Connector）...
	 *   if (changed) drm_kms_helper_hotplug_event(dev)  // 安全通知用户空间
	 *
	 * 注意：output_poll_execute() 自身检测到状态变化时（poll 路径），
	 * 不需要走两阶段，直接在函数末尾的 out 标签处调用 hotplug_event，
	 * 因为此时已通过 mutex_trylock 获取锁并在通知前主动释放了。
	 */
	bool delayed_event;

	/**
	 * @output_poll_work: 输出状态轮询的延迟工作队列项。
	 *
	 * 执行函数：output_poll_execute()（drm_probe_helper.c）
	 *
	 * 调度时机：
	 *   - drm_kms_helper_poll_enable()：首次启动，延迟 10s（DRM_OUTPUT_POLL_PERIOD）
	 *     若存在 delayed_event，则使用 1s 延迟（避免 Optimus/nouveau 的兼容问题）
	 *   - drm_helper_probe_single_connector_modes()：用户空间主动探测时检测到状态变化，
	 *     设置 delayed_event=true 后延迟 0 立即投递，让 hotplug_event 在无锁上下文执行
	 *   - output_poll_execute() 内部：还有 Connector 需要继续轮询，延迟 10s 投递
	 *
	 * 取消时机：
	 *   - drm_kms_helper_poll_disable()：cancel_delayed_work_sync() 同步取消
	 *   - drm_kms_helper_poll_fini()：彻底停止，poll_enabled = false 后取消
	 */
	struct delayed_work output_poll_work;

	/**
	 * @blob_lock:
	 *
	 * Mutex for blob property allocation and management, protects
	 * @property_blob_list and &drm_file.blobs.
	 */
	struct mutex blob_lock;

	/**
	 * @property_blob_list:
	 *
	 * List of all the blob property objects linked with
	 * &drm_property_blob.head. Protected by @blob_lock.
	 */
	struct list_head property_blob_list;

	/* pointers to standard properties */

	/**
	 * @edid_property: Default connector property to hold the EDID of the
	 * currently connected sink, if any.
	 */
	struct drm_property *edid_property;
	/**
	 * @dpms_property: Default connector property to control the
	 * connector's DPMS state.
	 */
	struct drm_property *dpms_property;
	/**
	 * @path_property: Default connector property to hold the DP MST path
	 * for the port.
	 */
	struct drm_property *path_property;
	/**
	 * @tile_property: Default connector property to store the tile
	 * position of a tiled screen, for sinks which need to be driven with
	 * multiple CRTCs.
	 */
	struct drm_property *tile_property;
	/**
	 * @link_status_property: Default connector property for link status
	 * of a connector
	 */
	struct drm_property *link_status_property;
	/**
	 * @plane_type_property: Default plane property to differentiate
	 * CURSOR, PRIMARY and OVERLAY legacy uses of planes.
	 */
	struct drm_property *plane_type_property;
	/**
	 * @prop_src_x: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_x;
	/**
	 * @prop_src_y: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_y;
	/**
	 * @prop_src_w: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_w;
	/**
	 * @prop_src_h: Default atomic plane property for the plane source
	 * position in the connected &drm_framebuffer.
	 */
	struct drm_property *prop_src_h;
	/**
	 * @prop_crtc_x: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is is being shown on.
	 */
	struct drm_property *prop_crtc_x;
	/**
	 * @prop_crtc_y: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is is being shown on.
	 */
	struct drm_property *prop_crtc_y;
	/**
	 * @prop_crtc_w: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is is being shown on.
	 */
	struct drm_property *prop_crtc_w;
	/**
	 * @prop_crtc_h: Default atomic plane property for the plane destination
	 * position in the &drm_crtc is is being shown on.
	 */
	struct drm_property *prop_crtc_h;
	/**
	 * @prop_fb_id: Default atomic plane property to specify the
	 * &drm_framebuffer.
	 */
	struct drm_property *prop_fb_id;
	/**
	 * @prop_in_fence_fd: Sync File fd representing the incoming fences
	 * for a Plane.
	 */
	struct drm_property *prop_in_fence_fd;
	/**
	 * @prop_out_fence_ptr: Sync File fd pointer representing the
	 * outgoing fences for a CRTC. Userspace should provide a pointer to a
	 * value of type s32, and then cast that pointer to u64.
	 */
	struct drm_property *prop_out_fence_ptr;
	/**
	 * @prop_crtc_id: Default atomic plane property to specify the
	 * &drm_crtc.
	 */
	struct drm_property *prop_crtc_id;
	/**
	 * @prop_active: Default atomic CRTC property to control the active
	 * state, which is the simplified implementation for DPMS in atomic
	 * drivers.
	 */
	struct drm_property *prop_active;
	/**
	 * @prop_mode_id: Default atomic CRTC property to set the mode for a
	 * CRTC. A 0 mode implies that the CRTC is entirely disabled - all
	 * connectors must be of and active must be set to disabled, too.
	 */
	struct drm_property *prop_mode_id;

	/**
	 * @dvi_i_subconnector_property: Optional DVI-I property to
	 * differentiate between analog or digital mode.
	 */
	struct drm_property *dvi_i_subconnector_property;
	/**
	 * @dvi_i_select_subconnector_property: Optional DVI-I property to
	 * select between analog or digital mode.
	 */
	struct drm_property *dvi_i_select_subconnector_property;

	/**
	 * @tv_subconnector_property: Optional TV property to differentiate
	 * between different TV connector types.
	 */
	struct drm_property *tv_subconnector_property;
	/**
	 * @tv_select_subconnector_property: Optional TV property to select
	 * between different TV connector types.
	 */
	struct drm_property *tv_select_subconnector_property;
	/**
	 * @tv_mode_property: Optional TV property to select
	 * the output TV mode.
	 */
	struct drm_property *tv_mode_property;
	/**
	 * @tv_left_margin_property: Optional TV property to set the left
	 * margin.
	 */
	struct drm_property *tv_left_margin_property;
	/**
	 * @tv_right_margin_property: Optional TV property to set the right
	 * margin.
	 */
	struct drm_property *tv_right_margin_property;
	/**
	 * @tv_top_margin_property: Optional TV property to set the right
	 * margin.
	 */
	struct drm_property *tv_top_margin_property;
	/**
	 * @tv_bottom_margin_property: Optional TV property to set the right
	 * margin.
	 */
	struct drm_property *tv_bottom_margin_property;
	/**
	 * @tv_brightness_property: Optional TV property to set the
	 * brightness.
	 */
	struct drm_property *tv_brightness_property;
	/**
	 * @tv_contrast_property: Optional TV property to set the
	 * contrast.
	 */
	struct drm_property *tv_contrast_property;
	/**
	 * @tv_flicker_reduction_property: Optional TV property to control the
	 * flicker reduction mode.
	 */
	struct drm_property *tv_flicker_reduction_property;
	/**
	 * @tv_overscan_property: Optional TV property to control the overscan
	 * setting.
	 */
	struct drm_property *tv_overscan_property;
	/**
	 * @tv_saturation_property: Optional TV property to set the
	 * saturation.
	 */
	struct drm_property *tv_saturation_property;
	/**
	 * @tv_hue_property: Optional TV property to set the hue.
	 */
	struct drm_property *tv_hue_property;

	/**
	 * @scaling_mode_property: Optional connector property to control the
	 * upscaling, mostly used for built-in panels.
	 */
	struct drm_property *scaling_mode_property;
	/**
	 * @aspect_ratio_property: Optional connector property to control the
	 * HDMI infoframe aspect ratio setting.
	 */
	struct drm_property *aspect_ratio_property;
	/**
	 * @content_type_property: Optional connector property to control the
	 * HDMI infoframe content type setting.
	 */
	struct drm_property *content_type_property;
	/**
	 * @degamma_lut_property: Optional CRTC property to set the LUT used to
	 * convert the framebuffer's colors to linear gamma.
	 */
	struct drm_property *degamma_lut_property;
	/**
	 * @degamma_lut_size_property: Optional CRTC property for the size of
	 * the degamma LUT as supported by the driver (read-only).
	 */
	struct drm_property *degamma_lut_size_property;
	/**
	 * @ctm_property: Optional CRTC property to set the
	 * matrix used to convert colors after the lookup in the
	 * degamma LUT.
	 */
	struct drm_property *ctm_property;
	/**
	 * @gamma_lut_property: Optional CRTC property to set the LUT used to
	 * convert the colors, after the CTM matrix, to the gamma space of the
	 * connected screen.
	 */
	struct drm_property *gamma_lut_property;
	/**
	 * @gamma_lut_size_property: Optional CRTC property for the size of the
	 * gamma LUT as supported by the driver (read-only).
	 */
	struct drm_property *gamma_lut_size_property;

	/**
	 * @cubic_lut_property: Optional CRTC property to set the 3D LUT used to
	 * convert color spaces.
	 */
	struct drm_property *cubic_lut_property;
	/**
	 * @cubic_lut_size_property: Optional CRTC property for the size of the
	 * 3D LUT as supported by the driver (read-only).
	 */
	struct drm_property *cubic_lut_size_property;

	/**
	 * @suggested_x_property: Optional connector property with a hint for
	 * the position of the output on the host's screen.
	 */
	struct drm_property *suggested_x_property;
	/**
	 * @suggested_y_property: Optional connector property with a hint for
	 * the position of the output on the host's screen.
	 */
	struct drm_property *suggested_y_property;

	/**
	 * @non_desktop_property: Optional connector property with a hint
	 * that device isn't a standard display, and the console/desktop,
	 * should not be displayed on it.
	 */
	struct drm_property *non_desktop_property;

	/**
	 * @panel_orientation_property: Optional connector property indicating
	 * how the lcd-panel is mounted inside the casing (e.g. normal or
	 * upside-down).
	 */
	struct drm_property *panel_orientation_property;

	/**
	 * @writeback_fb_id_property: Property for writeback connectors, storing
	 * the ID of the output framebuffer.
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_fb_id_property;

	/**
	 * @writeback_pixel_formats_property: Property for writeback connectors,
	 * storing an array of the supported pixel formats for the writeback
	 * engine (read-only).
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_pixel_formats_property;
	/**
	 * @writeback_out_fence_ptr_property: Property for writeback connectors,
	 * fd pointer representing the outgoing fences for a writeback
	 * connector. Userspace should provide a pointer to a value of type s32,
	 * and then cast that pointer to u64.
	 * See also: drm_writeback_connector_init()
	 */
	struct drm_property *writeback_out_fence_ptr_property;

	/**
	 * hdr_output_metadata_property: Connector property containing hdr
	 * metatda. This will be provided by userspace compositors based
	 * on HDR content
	 */
	struct drm_property *hdr_output_metadata_property;

	/* dumb ioctl parameters */
	uint32_t preferred_depth, prefer_shadow;

	/**
	 * @async_page_flip: Does this device support async flips on the primary
	 * plane?
	 */
	bool async_page_flip;

	/**
	 * @allow_fb_modifiers:
	 *
	 * Whether the driver supports fb modifiers in the ADDFB2.1 ioctl call.
	 */
	bool allow_fb_modifiers;

	/**
	 * @normalize_zpos:
	 *
	 * If true the drm core will call drm_atomic_normalize_zpos() as part of
	 * atomic mode checking from drm_atomic_helper_check()
	 */
	bool normalize_zpos;

	/**
	 * @modifiers_property: Plane property to list support modifier/format
	 * combination.
	 */
	struct drm_property *modifiers_property;

	/* cursor size */
	uint32_t cursor_width, cursor_height;

	/**
	 * @suspend_state:
	 *
	 * Atomic state when suspended.
	 * Set by drm_mode_config_helper_suspend() and cleared by
	 * drm_mode_config_helper_resume().
	 */
	struct drm_atomic_state *suspend_state;

	/**
	 * @helper_private: 原子提交的"尾部执行"钩子。
	 *
	 * 这是挂载在 drm_mode_config.helper_private 上的全局辅助函数表，
	 * 提供设备级别（而非单个 CRTC/Plane/Connector 级别）的原子提交定制点。
	 * 目前只有一个回调：@atomic_commit_tail，是整个原子提交流程的核心执行入口。
	 */
	const struct drm_mode_config_helper_funcs *helper_private;
};

void drm_mode_config_init(struct drm_device *dev);
void drm_mode_config_reset(struct drm_device *dev);
void drm_mode_config_cleanup(struct drm_device *dev);

#endif
