/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2014 Intel Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#ifndef DRM_ATOMIC_H_
#define DRM_ATOMIC_H_

#include <drm/drm_crtc.h>

/**
 * struct drm_crtc_commit - track modeset commits on a CRTC
 *
 * This structure is used to track pending modeset changes and atomic commit on
 * a per-CRTC basis. Since updating the list should never block this structure
 * is reference counted to allow waiters to safely wait on an event to complete,
 * without holding any locks.
 *
 * It has 3 different events in total to allow a fine-grained synchronization
 * between outstanding updates::
 *
 *	atomic commit thread			hardware
 *
 * 	write new state into hardware	---->	...
 * 	signal hw_done
 * 						switch to new state on next
 * 	...					v/hblank
 *
 *	wait for buffers to show up		...
 *
 *	...					send completion irq
 *						irq handler signals flip_done
 *	cleanup old buffers
 *
 * 	signal cleanup_done
 *
 * 	wait for flip_done		<----
 * 	clean up atomic state
 *
 * The important bit to know is that cleanup_done is the terminal event, but the
 * ordering between flip_done and hw_done is entirely up to the specific driver
 * and modeset state change.
 *
 * For an implementation of how to use this look at
 * drm_atomic_helper_setup_commit() from the atomic helper library.
 */
struct drm_crtc_commit {
	/**
	 * @crtc:
	 *
	 * DRM CRTC for this commit.
	 */
	struct drm_crtc *crtc;

	/**
	 * @ref:
	 *
	 * Reference count for this structure. Needed to allow blocking on
	 * completions without the risk of the completion disappearing
	 * meanwhile.
	 */
	struct kref ref;

	/**
	 * @flip_done:
	 *
	 * Will be signaled when the hardware has flipped to the new set of
	 * buffers. Signals at the same time as when the drm event for this
	 * commit is sent to userspace, or when an out-fence is singalled. Note
	 * that for most hardware, in most cases this happens after @hw_done is
	 * signalled.
	 */
	struct completion flip_done;

	/**
	 * @hw_done:
	 *
	 * Will be signalled when all hw register changes for this commit have
	 * been written out. Especially when disabling a pipe this can be much
	 * later than than @flip_done, since that can signal already when the
	 * screen goes black, whereas to fully shut down a pipe more register
	 * I/O is required.
	 *
	 * Note that this does not need to include separately reference-counted
	 * resources like backing storage buffer pinning, or runtime pm
	 * management.
	 */
	struct completion hw_done;

	/**
	 * @cleanup_done:
	 *
	 * Will be signalled after old buffers have been cleaned up by calling
	 * drm_atomic_helper_cleanup_planes(). Since this can only happen after
	 * a vblank wait completed it might be a bit later. This completion is
	 * useful to throttle updates and avoid hardware updates getting ahead
	 * of the buffer cleanup too much.
	 */
	struct completion cleanup_done;

	/**
	 * @commit_entry:
	 *
	 * Entry on the per-CRTC &drm_crtc.commit_list. Protected by
	 * $drm_crtc.commit_lock.
	 */
	struct list_head commit_entry;

	/**
	 * @event:
	 *
	 * &drm_pending_vblank_event pointer to clean up private events.
	 */
	struct drm_pending_vblank_event *event;

	/**
	 * @abort_completion:
	 *
	 * A flag that's set after drm_atomic_helper_setup_commit takes a second
	 * reference for the completion of $drm_crtc_state.event. It's used by
	 * the free code to remove the second reference if commit fails.
	 */
	bool abort_completion;
};

struct __drm_planes_state {
	struct drm_plane *ptr;
	struct drm_plane_state *state, *old_state, *new_state;
};

struct __drm_crtcs_state {
	struct drm_crtc *ptr;
	struct drm_crtc_state *state, *old_state, *new_state;

	/**
	 * @commit:
	 *
	 * A reference to the CRTC commit object that is kept for use by
	 * drm_atomic_helper_wait_for_flip_done() after
	 * drm_atomic_helper_commit_hw_done() is called. This ensures that a
	 * concurrent commit won't free a commit object that is still in use.
	 */
	struct drm_crtc_commit *commit;

	s32 __user *out_fence_ptr;
	u64 last_vblank_count;
};

struct __drm_connnectors_state {
	struct drm_connector *ptr;
	struct drm_connector_state *state, *old_state, *new_state;
	/**
	 * @out_fence_ptr:
	 *
	 * User-provided pointer which the kernel uses to return a sync_file
	 * file descriptor. Used by writeback connectors to signal completion of
	 * the writeback.
	 */
	s32 __user *out_fence_ptr;
};

struct drm_private_obj;
struct drm_private_state;

/**
 * struct drm_private_state_funcs - atomic state functions for private objects
 *
 * These hooks are used by atomic helpers to create, swap and destroy states of
 * private objects. The structure itself is used as a vtable to identify the
 * associated private object type. Each private object type that needs to be
 * added to the atomic states is expected to have an implementation of these
 * hooks and pass a pointer to it's drm_private_state_funcs struct to
 * drm_atomic_get_private_obj_state().
 */
struct drm_private_state_funcs {
	/**
	 * @atomic_duplicate_state:
	 *
	 * Duplicate the current state of the private object and return it. It
	 * is an error to call this before obj->state has been initialized.
	 *
	 * RETURNS:
	 *
	 * Duplicated atomic state or NULL when obj->state is not
	 * initialized or allocation failed.
	 */
	struct drm_private_state *(*atomic_duplicate_state)(struct drm_private_obj *obj);

	/**
	 * @atomic_destroy_state:
	 *
	 * Frees the private object state created with @atomic_duplicate_state.
	 */
	void (*atomic_destroy_state)(struct drm_private_obj *obj,
				     struct drm_private_state *state);
};

/**
 * struct drm_private_obj - base struct for driver private atomic object
 *
 * A driver private object is initialized by calling
 * drm_atomic_private_obj_init() and cleaned up by calling
 * drm_atomic_private_obj_fini().
 *
 * Currently only tracks the state update functions and the opaque driver
 * private state itself, but in the future might also track which
 * &drm_modeset_lock is required to duplicate and update this object's state.
 */
struct drm_private_obj {
	/**
	 * @state: Current atomic state for this driver private object.
	 */
	struct drm_private_state *state;

	/**
	 * @funcs:
	 *
	 * Functions to manipulate the state of this driver private object, see
	 * &drm_private_state_funcs.
	 */
	const struct drm_private_state_funcs *funcs;
};

/**
 * struct drm_private_state - base struct for driver private object state
 * @state: backpointer to global drm_atomic_state
 *
 * Currently only contains a backpointer to the overall atomic update, but in
 * the future also might hold synchronization information similar to e.g.
 * &drm_crtc.commit.
 */
struct drm_private_state {
	struct drm_atomic_state *state;
};

struct __drm_private_objs_state {
	struct drm_private_obj *ptr;
	struct drm_private_state *state, *old_state, *new_state;
};

/**
 * struct drm_atomic_state - 原子提交的"换景总清单"
 *
 * ## 核心设计思想：原子性
 *
 * 这是 DRM 原子模式设置（Atomic KMS）的核心数据结构，代表一次完整的
 * "换景"请求——将显示系统从当前状态切换到目标状态的全部变更信息打包在一起。
 *
 * "原子性"意味着：
 *   - 要么清单里的**所有变更**一次性全部生效；
 *   - 要么任意一处校验失败，**所有变更全部回滚**，系统保持原来的状态不变。
 *   - 绝不会出现"Plane 位置变了但 CRTC 时序还是旧的"这种半吊子状态。
 *
 * ## 生命周期
 *
 * 1. **创建**：drm_atomic_state_alloc() 分配，@ref 引用计数初始化为 1
 * 2. **填充**：用户空间（合成器）通过 ioctl 逐步填充清单
 *    drm_atomic_get_crtc_state()      → 加入 CRTC 变更
 *    drm_atomic_get_plane_state()     → 加入 Plane 变更（帧缓冲、位置、缩放等）
 *    drm_atomic_get_connector_state() → 加入 Connector 变更（连接关系、色彩空间等）
 * 3. **校验**：drm_atomic_check_only() 全量校验所有参数，任意不合法则整体拒绝
 * 4. **提交**：校验通过后，在 VBlank 消隐期原子写入硬件寄存器
 * 5. **释放**：drm_atomic_state_put() 减引用，归零后 drm_atomic_state_free() 释放
 *
 * ## 数据组织方式
 *
 * 清单里的变更按硬件对象分类存储，每种对象都保存"旧状态"和"新状态"的指针：
 *
 *   planes[]    → 每个 Plane 的 {ptr, old_state, new_state}
 *   crtcs[]     → 每个 CRTC 的  {ptr, old_state, new_state, commit}
 *   connectors[]→ 每个 Connector 的 {ptr, old_state, new_state}
 *
 * 同时保留"旧状态"的原因：校验失败时可以精确回滚到旧状态，
 * 提交成功后也可以用旧状态做差量对比（只重配发生变化的硬件）。
 *
 * States are added to an atomic update by calling drm_atomic_get_crtc_state(),
 * drm_atomic_get_plane_state(), drm_atomic_get_connector_state(), or for
 * private state structures, drm_atomic_get_private_obj_state().
 */
struct drm_atomic_state {
	/**
	 * @ref: 引用计数。
	 *
	 * 此对象在以下情况下被多处持有：
	 *   - 合成器提交后，DRM 驱动持有一份引用，等待 VBlank 消隐期；
	 *   - 非阻塞提交（async commit）时，commit_work 工作队列持有一份引用；
	 *   - drm_atomic_helper_wait_for_flip_done() 等待 page flip 完成时持有。
	 * 只有所有持有者都调用 drm_atomic_state_put() 后，清单才会被释放。
	 */
	struct kref ref;

	/**
	 * @dev: 此原子状态归属的 DRM 设备。
	 * 所有 CRTC、Plane、Connector 都属于同一个 drm_device。
	 */
	struct drm_device *dev;

	/**
	 * @allow_modeset: 是否允许全量 modeset（完整的显示模式切换）。
	 *
	 * modeset 是指切换 CRTC 的分辨率、刷新率、时序参数等重量级操作，
	 * 代价很高：需要关闭 CRTC → 重配时序 → 重新上电 → 链路重新训练。
	 *
	 * false（默认）：只允许轻量级的 page flip 和 Plane 属性变更，
	 *   如果本次 atomic_check 发现必须做 modeset，直接返回 -EINVAL。
	 *   这是合成器日常渲染帧时的模式，绝不允许意外触发耗时的 modeset。
	 *
	 * true：允许全量 modeset，用于用户主动切换分辨率、首次初始化、
	 *   热插拔后配置新屏幕等场景。
	 */
	bool allow_modeset : 1;

	/**
	 * @legacy_cursor_update: 旧式光标更新提示位。
	 *
	 * 用于让内核以旧的 DRM_IOCTL_MODE_CURSOR 语义处理此次提交：
	 *   - 跳过部分原子校验（光标更新不需要严格的时序保证）
	 *   - 允许跨越 VBlank 更新（不强制等消隐期，减少光标延迟）
	 *   - 对应硬件 Cursor Plane 的独立异步更新路径
	 *
	 * 现代合成器（Wayland）不应设置此标志，它仅用于兼容旧的 X11 光标 ioctl。
	 * 设置此标志意味着此次提交不保证完整的原子性。
	 */
	bool legacy_cursor_update : 1;

	/**
	 * @async_update: 异步 Plane 更新提示位。
	 *
	 * true 表示此次提交可以异步执行，不需要等待 VBlank 消隐期。
	 * 适用于不涉及时序变更的 Plane 属性修改（如更新帧缓冲地址），
	 * 可以减少延迟，但不保证严格的帧边界对齐。
	 *
	 * 注意：即使设置了此标志，实际是否异步由驱动实现决定，
	 * 并非所有硬件都支持真正的异步 Plane 更新。
	 */
	bool async_update : 1;

	/**
	 * @planes: 本次提交涉及的所有 Plane 的状态数组。
	 *
	 * 数组大小固定为 drm_mode_config.num_total_plane（系统 Plane 总数），
	 * 未参与本次提交的 Plane，其 ptr 为 NULL，跳过处理。
	 *
	 * 每个元素（__drm_planes_state）包含：
	 *   ptr       → 指向 drm_plane 对象
	 *   old_state → 提交前的状态快照（帧缓冲地址、位置、格式、旋转等）
	 *   new_state → 本次提交的目标状态
	 *   state     → 当前生效状态（提交成功后 = new_state）
	 *
	 * 保留 old_state 的用途：
	 *   - 校验失败时，精确恢复到 old_state
	 *   - 提交成功后，通过 old_state 找到需要释放的旧帧缓冲（drm_framebuffer_put）
	 *   - 驱动可以对比 old/new 做差量更新，只重配发生变化的寄存器
	 */
	struct __drm_planes_state *planes;

	/**
	 * @crtcs: 本次提交涉及的所有 CRTC 的状态数组。
	 *
	 * 数组大小固定为 drm_mode_config.num_crtc（系统 CRTC 总数）。
	 *
	 * 每个元素（__drm_crtcs_state）包含：
	 *   ptr            → 指向 drm_crtc 对象（VP/Video Port）
	 *   old_state      → 提交前的状态（时序参数、Plane 叠加配置、active 等）
	 *   new_state      → 本次提交的目标状态
	 *   commit         → 关联的 drm_crtc_commit 对象（Fence 机制的载体）
	 *   out_fence_ptr  → 用户空间传入的指针，内核将 out-fence fd 写回此处
	 *   last_vblank_count → 提交时的 VBlank 计数，用于检测 flip 是否完成
	 *
	 * commit 对象的作用：
	 *   当此 CRTC 的提交被硬件实际执行后，drm_atomic_helper_commit_hw_done()
	 *   会调用 complete(&commit->hw_done)，所有等待此 CRTC 完成的调用者
	 *   通过这个 completion 得到通知，是实现非阻塞提交的核心机制。
	 */
	struct __drm_crtcs_state *crtcs;

	/**
	 * @num_connector: @connectors 数组的实际元素数量。
	 *
	 * 与 planes/crtcs 不同，connectors 数组是动态大小的（因为支持热插拔，
	 * Connector 数量可能运行时变化），所以需要单独记录当前数量。
	 */
	int num_connector;

	/**
	 * @connectors: 本次提交涉及的所有 Connector 的状态数组。
	 *
	 * 每个元素（__drm_connnectors_state）包含：
	 *   ptr          → 指向 drm_connector 对象（HDMI/DSI/eDP 口岸）
	 *   old_state    → 提交前的状态（绑定的 encoder/crtc、色彩空间、HDCP 状态等）
	 *   new_state    → 本次提交的目标状态
	 *   out_fence_ptr → writeback connector 的完成信号 fence 输出指针
	 *
	 * Connector 状态变更的典型场景：
	 *   - 热插拔后将新屏幕的 connector 绑定到某个 CRTC
	 *   - 更改输出色彩空间（如从 sRGB 切换到 BT.2020）
	 *   - 更改 HDCP 内容保护状态
	 */
	struct __drm_connnectors_state *connectors;

	/**
	 * @num_private_objs: @private_objs 数组的实际元素数量。
	 */
	int num_private_objs;

	/**
	 * @private_objs: 本次提交涉及的驱动私有对象状态数组。
	 *
	 * 驱动可以定义自己的私有原子对象（drm_private_obj），
	 * 用于管理 DRM 标准对象（CRTC/Plane/Connector）以外的硬件状态，
	 * 例如：Rockchip 的 VOP2 全局寄存器配置、共享 PLL 参数等。
	 * 这些私有状态同样参与原子校验和提交，保证整体一致性。
	 */
	struct __drm_private_objs_state *private_objs;

	/**
	 * @acquire_ctx: 此次原子提交的锁获取上下文。
	 *
	 * DRM 使用 drm_modeset_lock 保护硬件对象的状态，
	 * acquire_ctx 记录了本次提交过程中持有的所有锁，
	 * 提交完成（无论成功或失败）后统一释放，防止死锁。
	 *
	 * 锁的获取顺序由 DRM 核心严格管理，驱动不应手动操作此字段。
	 */
	struct drm_modeset_acquire_ctx *acquire_ctx;

	/**
	 * @fake_commit: 为"游离状态"的 Plane/Connector 提供的虚拟提交对象。
	 *
	 * ## 问题背景
	 *
	 * 原子状态链保证了各次提交按顺序执行（线性化），依赖机制是：
	 * 每次提交完成后，drm_crtc_commit 对象的 completion 被触发，
	 * 下一次提交在校验阶段等待上一次提交完成后再继续。
	 *
	 * 但如果一个 Plane 或 Connector 当前**没有绑定任何 CRTC**，
	 * 它就没有对应的 drm_crtc_commit 对象，无法参与提交顺序链，
	 * 可能导致持有该 Plane/Connector 新状态的 drm_atomic_state
	 * 被提前释放，引发 use-after-free 内存问题。
	 *
	 * ## 解决方案
	 *
	 * fake_commit 是一个不绑定任何 CRTC 的虚拟提交对象，专门用来
	 * "占位"——让这些游离的 Plane/Connector 也有一个 commit 可以挂靠，
	 * 从而参与提交顺序链，防止状态被过早释放。
	 *
	 * 当 drm_atomic_helper_commit_hw_done() 被调用时，fake_commit 的
	 * completion 也会被触发，正式完成这些游离状态的生命周期管理。
	 */
	struct drm_crtc_commit *fake_commit;

	/**
	 * @commit_work: 非阻塞提交的工作队列项。
	 *
	 * ## 阻塞 vs 非阻塞提交
	 *
	 * 阻塞提交（同步）：
	 *   合成器调用 ioctl → 内核完成 check → 等待 VBlank → 写入硬件 → 返回
	 *   合成器在整个过程中被阻塞，下一帧的渲染被推迟
	 *
	 * 非阻塞提交（异步，通过 commit_work 实现）：
	 *   合成器调用 ioctl → 内核完成 check → **立即返回** → 合成器继续渲染下一帧
	 *   同时，commit_work 被投递到内核工作队列（kthread_worker）
	 *   工作队列在后台等待 VBlank → 写入硬件 → 通过 out-fence 通知合成器
	 *
	 * commit_work 的执行函数通常是 drm_atomic_helper_commit_work()，
	 * 由原子辅助层封装了等待 VBlank 和写寄存器的完整流程。
	 *
	 * 使用非阻塞提交时，drm_atomic_state 的生命周期由 @ref 引用计数保护，
	 * 工作队列完成后调用 drm_atomic_state_put() 释放。
	 */
	struct work_struct commit_work;
};

void __drm_crtc_commit_free(struct kref *kref);

/**
 * drm_crtc_commit_get - acquire a reference to the CRTC commit
 * @commit: CRTC commit
 *
 * Increases the reference of @commit.
 *
 * Returns:
 * The pointer to @commit, with reference increased.
 */
static inline struct drm_crtc_commit *drm_crtc_commit_get(struct drm_crtc_commit *commit)
{
	kref_get(&commit->ref);
	return commit;
}

/**
 * drm_crtc_commit_put - release a reference to the CRTC commmit
 * @commit: CRTC commit
 *
 * This releases a reference to @commit which is freed after removing the
 * final reference. No locking required and callable from any context.
 */
static inline void drm_crtc_commit_put(struct drm_crtc_commit *commit)
{
	kref_put(&commit->ref, __drm_crtc_commit_free);
}

struct drm_atomic_state * __must_check
drm_atomic_state_alloc(struct drm_device *dev);
void drm_atomic_state_clear(struct drm_atomic_state *state);

/**
 * drm_atomic_state_get - acquire a reference to the atomic state
 * @state: The atomic state
 *
 * Returns a new reference to the @state
 */
static inline struct drm_atomic_state *
drm_atomic_state_get(struct drm_atomic_state *state)
{
	kref_get(&state->ref);
	return state;
}

void __drm_atomic_state_free(struct kref *ref);

/**
 * drm_atomic_state_put - release a reference to the atomic state
 * @state: The atomic state
 *
 * This releases a reference to @state which is freed after removing the
 * final reference. No locking required and callable from any context.
 */
static inline void drm_atomic_state_put(struct drm_atomic_state *state)
{
	kref_put(&state->ref, __drm_atomic_state_free);
}

int  __must_check
drm_atomic_state_init(struct drm_device *dev, struct drm_atomic_state *state);
void drm_atomic_state_default_clear(struct drm_atomic_state *state);
void drm_atomic_state_default_release(struct drm_atomic_state *state);

struct drm_crtc_state * __must_check
drm_atomic_get_crtc_state(struct drm_atomic_state *state,
			  struct drm_crtc *crtc);
int drm_atomic_crtc_set_property(struct drm_crtc *crtc,
		struct drm_crtc_state *state, struct drm_property *property,
		uint64_t val);
struct drm_plane_state * __must_check
drm_atomic_get_plane_state(struct drm_atomic_state *state,
			   struct drm_plane *plane);
struct drm_connector_state * __must_check
drm_atomic_get_connector_state(struct drm_atomic_state *state,
			       struct drm_connector *connector);

void drm_atomic_private_obj_init(struct drm_private_obj *obj,
				 struct drm_private_state *state,
				 const struct drm_private_state_funcs *funcs);
void drm_atomic_private_obj_fini(struct drm_private_obj *obj);

struct drm_private_state * __must_check
drm_atomic_get_private_obj_state(struct drm_atomic_state *state,
				 struct drm_private_obj *obj);

/**
 * drm_atomic_get_existing_crtc_state - get crtc state, if it exists
 * @state: global atomic state object
 * @crtc: crtc to grab
 *
 * This function returns the crtc state for the given crtc, or NULL
 * if the crtc is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_crtc_state or
 * @drm_atomic_get_new_crtc_state should be used instead.
 */
static inline struct drm_crtc_state *
drm_atomic_get_existing_crtc_state(struct drm_atomic_state *state,
				   struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].state;
}

/**
 * drm_atomic_get_old_crtc_state - get old crtc state, if it exists
 * @state: global atomic state object
 * @crtc: crtc to grab
 *
 * This function returns the old crtc state for the given crtc, or
 * NULL if the crtc is not part of the global atomic state.
 */
static inline struct drm_crtc_state *
drm_atomic_get_old_crtc_state(struct drm_atomic_state *state,
			      struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].old_state;
}
/**
 * drm_atomic_get_new_crtc_state - get new crtc state, if it exists
 * @state: global atomic state object
 * @crtc: crtc to grab
 *
 * This function returns the new crtc state for the given crtc, or
 * NULL if the crtc is not part of the global atomic state.
 */
static inline struct drm_crtc_state *
drm_atomic_get_new_crtc_state(struct drm_atomic_state *state,
			      struct drm_crtc *crtc)
{
	return state->crtcs[drm_crtc_index(crtc)].new_state;
}

/**
 * drm_atomic_get_existing_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the plane state for the given plane, or NULL
 * if the plane is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_plane_state or
 * @drm_atomic_get_new_plane_state should be used instead.
 */
static inline struct drm_plane_state *
drm_atomic_get_existing_plane_state(struct drm_atomic_state *state,
				    struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].state;
}

/**
 * drm_atomic_get_old_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the old plane state for the given plane, or
 * NULL if the plane is not part of the global atomic state.
 */
static inline struct drm_plane_state *
drm_atomic_get_old_plane_state(struct drm_atomic_state *state,
			       struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].old_state;
}

/**
 * drm_atomic_get_new_plane_state - get plane state, if it exists
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the new plane state for the given plane, or
 * NULL if the plane is not part of the global atomic state.
 */
static inline struct drm_plane_state *
drm_atomic_get_new_plane_state(struct drm_atomic_state *state,
			       struct drm_plane *plane)
{
	return state->planes[drm_plane_index(plane)].new_state;
}

/**
 * drm_atomic_get_existing_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 *
 * This function is deprecated, @drm_atomic_get_old_connector_state or
 * @drm_atomic_get_new_connector_state should be used instead.
 */
static inline struct drm_connector_state *
drm_atomic_get_existing_connector_state(struct drm_atomic_state *state,
					struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].state;
}

/**
 * drm_atomic_get_old_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the old connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 */
static inline struct drm_connector_state *
drm_atomic_get_old_connector_state(struct drm_atomic_state *state,
				   struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].old_state;
}

/**
 * drm_atomic_get_new_connector_state - get connector state, if it exists
 * @state: global atomic state object
 * @connector: connector to grab
 *
 * This function returns the new connector state for the given connector,
 * or NULL if the connector is not part of the global atomic state.
 */
static inline struct drm_connector_state *
drm_atomic_get_new_connector_state(struct drm_atomic_state *state,
				   struct drm_connector *connector)
{
	int index = drm_connector_index(connector);

	if (index >= state->num_connector)
		return NULL;

	return state->connectors[index].new_state;
}

/**
 * __drm_atomic_get_current_plane_state - get current plane state
 * @state: global atomic state object
 * @plane: plane to grab
 *
 * This function returns the plane state for the given plane, either from
 * @state, or if the plane isn't part of the atomic state update, from @plane.
 * This is useful in atomic check callbacks, when drivers need to peek at, but
 * not change, state of other planes, since it avoids threading an error code
 * back up the call chain.
 *
 * WARNING:
 *
 * Note that this function is in general unsafe since it doesn't check for the
 * required locking for access state structures. Drivers must ensure that it is
 * safe to access the returned state structure through other means. One common
 * example is when planes are fixed to a single CRTC, and the driver knows that
 * the CRTC lock is held already. In that case holding the CRTC lock gives a
 * read-lock on all planes connected to that CRTC. But if planes can be
 * reassigned things get more tricky. In that case it's better to use
 * drm_atomic_get_plane_state and wire up full error handling.
 *
 * Returns:
 *
 * Read-only pointer to the current plane state.
 */
static inline const struct drm_plane_state *
__drm_atomic_get_current_plane_state(struct drm_atomic_state *state,
				     struct drm_plane *plane)
{
	if (state->planes[drm_plane_index(plane)].state)
		return state->planes[drm_plane_index(plane)].state;

	return plane->state;
}

int __must_check
drm_atomic_set_mode_for_crtc(struct drm_crtc_state *state,
			     const struct drm_display_mode *mode);
int __must_check
drm_atomic_set_mode_prop_for_crtc(struct drm_crtc_state *state,
				  struct drm_property_blob *blob);
int __must_check
drm_atomic_set_crtc_for_plane(struct drm_plane_state *plane_state,
			      struct drm_crtc *crtc);
void drm_atomic_set_fb_for_plane(struct drm_plane_state *plane_state,
				 struct drm_framebuffer *fb);
void drm_atomic_set_fence_for_plane(struct drm_plane_state *plane_state,
				    struct dma_fence *fence);
int __must_check
drm_atomic_set_crtc_for_connector(struct drm_connector_state *conn_state,
				  struct drm_crtc *crtc);
int drm_atomic_set_writeback_fb_for_connector(
		struct drm_connector_state *conn_state,
		struct drm_framebuffer *fb);
int __must_check
drm_atomic_add_affected_connectors(struct drm_atomic_state *state,
				   struct drm_crtc *crtc);
int __must_check
drm_atomic_add_affected_planes(struct drm_atomic_state *state,
			       struct drm_crtc *crtc);

int __must_check drm_atomic_check_only(struct drm_atomic_state *state);
int __must_check drm_atomic_commit(struct drm_atomic_state *state);
int __must_check drm_atomic_nonblocking_commit(struct drm_atomic_state *state);

void drm_state_dump(struct drm_device *dev, struct drm_printer *p);

/**
 * for_each_oldnew_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @old_connector_state: &struct drm_connector_state iteration cursor for the
 * 	old state
 * @new_connector_state: &struct drm_connector_state iteration cursor for the
 * 	new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_connector_in_state(__state, connector, old_connector_state, new_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (old_connector_state) = (__state)->connectors[__i].old_state,	\
			     (new_connector_state) = (__state)->connectors[__i].new_state, 1))

/**
 * for_each_old_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @old_connector_state: &struct drm_connector_state iteration cursor for the
 * 	old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_connector_in_state(__state, connector, old_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (old_connector_state) = (__state)->connectors[__i].old_state, 1))

/**
 * for_each_new_connector_in_state - iterate over all connectors in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @connector: &struct drm_connector iteration cursor
 * @new_connector_state: &struct drm_connector_state iteration cursor for the
 * 	new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all connectors in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_connector_in_state(__state, connector, new_connector_state, __i) \
	for ((__i) = 0;								\
	     (__i) < (__state)->num_connector;					\
	     (__i)++)								\
		for_each_if ((__state)->connectors[__i].ptr &&			\
			     ((connector) = (__state)->connectors[__i].ptr,	\
			     (new_connector_state) = (__state)->connectors[__i].new_state, 1))

/**
 * for_each_oldnew_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @old_crtc_state: &struct drm_crtc_state iteration cursor for the old state
 * @new_crtc_state: &struct drm_crtc_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_crtc_in_state(__state, crtc, old_crtc_state, new_crtc_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			     (old_crtc_state) = (__state)->crtcs[__i].old_state, \
			     (new_crtc_state) = (__state)->crtcs[__i].new_state, 1))

/**
 * for_each_old_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @old_crtc_state: &struct drm_crtc_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_crtc_in_state(__state, crtc, old_crtc_state, __i)	\
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			     (old_crtc_state) = (__state)->crtcs[__i].old_state, 1))

/**
 * for_each_new_crtc_in_state - iterate over all CRTCs in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @crtc: &struct drm_crtc iteration cursor
 * @new_crtc_state: &struct drm_crtc_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all CRTCs in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_crtc_in_state(__state, crtc, new_crtc_state, __i)	\
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_crtc;		\
	     (__i)++)							\
		for_each_if ((__state)->crtcs[__i].ptr &&		\
			     ((crtc) = (__state)->crtcs[__i].ptr,	\
			     (new_crtc_state) = (__state)->crtcs[__i].new_state, 1))

/**
 * for_each_oldnew_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking both old and
 * new state. This is useful in places where the state delta needs to be
 * considered, for example in atomic check functions.
 */
#define for_each_oldnew_plane_in_state(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (old_plane_state) = (__state)->planes[__i].old_state,\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_oldnew_plane_in_state_reverse - iterate over all planes in an atomic
 * update in reverse order
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update in reverse order,
 * tracking both old and  new state. This is useful in places where the
 * state delta needs to be considered, for example in atomic check functions.
 */
#define for_each_oldnew_plane_in_state_reverse(__state, plane, old_plane_state, new_plane_state, __i) \
	for ((__i) = ((__state)->dev->mode_config.num_total_plane - 1);	\
	     (__i) >= 0;						\
	     (__i)--)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (old_plane_state) = (__state)->planes[__i].old_state,\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_old_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @old_plane_state: &struct drm_plane_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking only the old
 * state. This is useful in disable functions, where we need the old state the
 * hardware is still in.
 */
#define for_each_old_plane_in_state(__state, plane, old_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (old_plane_state) = (__state)->planes[__i].old_state, 1))
/**
 * for_each_new_plane_in_state - iterate over all planes in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @plane: &struct drm_plane iteration cursor
 * @new_plane_state: &struct drm_plane_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all planes in an atomic update, tracking only the new
 * state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_plane_in_state(__state, plane, new_plane_state, __i) \
	for ((__i) = 0;							\
	     (__i) < (__state)->dev->mode_config.num_total_plane;	\
	     (__i)++)							\
		for_each_if ((__state)->planes[__i].ptr &&		\
			     ((plane) = (__state)->planes[__i].ptr,	\
			      (new_plane_state) = (__state)->planes[__i].new_state, 1))

/**
 * for_each_oldnew_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @old_obj_state: &struct drm_private_state iteration cursor for the old state
 * @new_obj_state: &struct drm_private_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking both
 * old and new state. This is useful in places where the state delta needs
 * to be considered, for example in atomic check functions.
 */
#define for_each_oldnew_private_obj_in_state(__state, obj, old_obj_state, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (old_obj_state) = (__state)->private_objs[__i].old_state,	\
		      (new_obj_state) = (__state)->private_objs[__i].new_state, 1); \
	     (__i)++)

/**
 * for_each_old_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @old_obj_state: &struct drm_private_state iteration cursor for the old state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking only
 * the old state. This is useful in disable functions, where we need the old
 * state the hardware is still in.
 */
#define for_each_old_private_obj_in_state(__state, obj, old_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (old_obj_state) = (__state)->private_objs[__i].old_state, 1); \
	     (__i)++)

/**
 * for_each_new_private_obj_in_state - iterate over all private objects in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @obj: &struct drm_private_obj iteration cursor
 * @new_obj_state: &struct drm_private_state iteration cursor for the new state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all private objects in an atomic update, tracking only
 * the new state. This is useful in enable functions, where we need the new state the
 * hardware should be in when the atomic commit operation has completed.
 */
#define for_each_new_private_obj_in_state(__state, obj, new_obj_state, __i) \
	for ((__i) = 0; \
	     (__i) < (__state)->num_private_objs && \
		     ((obj) = (__state)->private_objs[__i].ptr, \
		      (new_obj_state) = (__state)->private_objs[__i].new_state, 1); \
	     (__i)++)

/**
 * drm_atomic_crtc_needs_modeset - compute combined modeset need
 * @state: &drm_crtc_state for the CRTC
 *
 * To give drivers flexibility &struct drm_crtc_state has 3 booleans to track
 * whether the state CRTC changed enough to need a full modeset cycle:
 * mode_changed, active_changed and connectors_changed. This helper simply
 * combines these three to compute the overall need for a modeset for @state.
 *
 * The atomic helper code sets these booleans, but drivers can and should
 * change them appropriately to accurately represent whether a modeset is
 * really needed. In general, drivers should avoid full modesets whenever
 * possible.
 *
 * For example if the CRTC mode has changed, and the hardware is able to enact
 * the requested mode change without going through a full modeset, the driver
 * should clear mode_changed in its &drm_mode_config_funcs.atomic_check
 * implementation.
 */
static inline bool
drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state)
{
	return state->mode_changed || state->active_changed ||
	       state->connectors_changed;
}

int drm_atomic_set_property(struct drm_atomic_state *state,
			    struct drm_mode_object *obj,
			    struct drm_property *prop,
			    uint64_t prop_value);

#endif /* DRM_ATOMIC_H_ */
