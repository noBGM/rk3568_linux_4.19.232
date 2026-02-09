/*
 * drm_irq.c IRQ and vblank support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drm_vblank.h>
#include <drm/drmP.h>
#include <linux/export.h>

#include "drm_trace.h"
#include "drm_internal.h"

/**
 * DOC: vblank handling（垂直消隐处理）
 *
 * ============================================================================
 * 一、VBlank 的核心作用
 * ============================================================================
 *
 * 垂直消隐（Vertical Blanking，简称 VBlank）在图形渲染中扮演关键角色。
 * 为了实现无撕裂的画面显示，用户必须将页面翻转和/或渲染操作同步到垂直消隐期。
 *
 * 什么是画面撕裂（Tearing）？
 *   显示器正在扫描显示画面A的上半部分时，程序切换到了画面B，
 *   导致显示器下半部分显示的是画面B，形成画面断裂现象。
 *
 * VBlank 如何解决撕裂？
 *   在两帧画面之间的空白期（VBlank）进行页面切换，此时显示器
 *   不在扫描显示，切换缓冲区不会被用户看到。
 *
 *   时间线：
 *   ┌────────┐ VBlank ┌────────┐ VBlank ┌────────┐
 *   │Frame 1 │ ······ │Frame 2 │ ······ │Frame 3 │
 *   └────────┘   ↑    └────────┘   ↑    └────────┘
 *             在这里切换          在这里切换
 *             不会撕裂            不会撕裂
 *
 * DRM API 提供的用户空间接口：
 * - ioctl：执行与 VBlank 同步的页面翻转
 * - ioctl：等待 VBlank 事件
 *
 * ============================================================================
 * 二、DRM 核心层的职责
 * ============================================================================
 *
 * DRM 核心负责处理大部分垂直消隐管理逻辑，包括：
 *
 * 1. 过滤杂散中断：
 *    硬件可能产生错误的 VBlank 中断，核心层会识别并丢弃
 *
 * 2. 维护无竞态的计数器：
 *    多线程访问 VBlank 计数器时保证数据一致性
 *
 * 3. 处理计数器回绕和重置：
 *    硬件计数器溢出时（如 24 位计数器从 0xFFFFFF 变为 0），
 *    核心层会正确处理，保证软件计数器持续递增
 *
 * 4. 维护使用计数（引用计数）：
 *    跟踪有多少用户在使用 VBlank 中断，确保在有人使用时
 *    不会被错误关闭
 *
 * 驱动的职责：
 * - 必须：产生垂直消隐中断
 * - 可选：提供硬件 VBlank 计数器（推荐，可提高精度）
 *
 * ============================================================================
 * 三、驱动实现要求
 * ============================================================================
 *
 * 最小实现（必需的三步）：
 *
 * 1. 初始化：调用 drm_vblank_init()
 *    在驱动加载时初始化 VBlank 子系统
 *
 * 2. 实现回调函数：
 *    - &drm_crtc_funcs.enable_vblank：使能 VBlank 硬件中断
 *    - &drm_crtc_funcs.disable_vblank：禁用 VBlank 硬件中断
 *
 * 3. 中断处理：
 *    在驱动的 VBlank 中断处理函数中调用 drm_crtc_handle_vblank()，
 *    通知 DRM 核心 VBlank 事件已发生
 *
 * 示例代码结构：
 *   static irqreturn_t my_irq_handler(int irq, void *arg) {
 *       if (vblank_occurred)
 *           drm_crtc_handle_vblank(crtc);  // ← 通知核心层
 *       return IRQ_HANDLED;
 *   }
 *
 * ============================================================================
 * 四、VBlank 中断的使能/禁用机制
 * ============================================================================
 *
 * VBlank 中断可以被以下两种方式启用：
 * 1. DRM 核心层自动启用（响应用户空间请求）
 * 2. 驱动主动启用（例如处理页面翻转操作）
 *
 * 引用计数管理：
 * DRM 核心维护一个 VBlank 使用计数，确保在有用户需要时中断不会被禁用。
 *
 * API 使用模式：
 *   drm_crtc_vblank_get(crtc);      // 引用计数 +1，保证中断开启
 *   // ... 使用 VBlank 中断期间，中断保证不会被关闭 ...
 *   drm_crtc_vblank_put(crtc);      // 引用计数 -1，允许关闭中断
 *
 * 实际应用场景：
 *   // 等待下一个 VBlank 来翻转页面
 *   drm_crtc_vblank_get(crtc);
 *   setup_page_flip();
 *   wait_for_vblank_event();
 *   drm_crtc_vblank_put(crtc);
 *
 * ============================================================================
 * 五、延迟禁用机制
 * ============================================================================
 *
 * 为什么需要延迟禁用？主要有两个原因：
 *
 * 原因 1：硬件关闭中断不是瞬时的（存在竞态风险）
 *
 *   时序问题示例：
 *   CPU 时间线：      软件写寄存器禁用中断 ────→ 认为中断已关闭
 *                          |
 *   硬件时间线：      VBlank发生 ─→ 产生中断 ─→ 寄存器生效，中断关闭
 *                         |           |
 *                         |           └─ 这个中断已经在路上了！
 *                         |              CPU 必须处理它
 *                         |
 *                         └─ 硬件检测到 VBlank 在寄存器写入之前
 *
 *   后果：软件以为中断关了，但硬件还会发 1-2 个"意外"中断，
 *        导致计数器出错或触发 WARN。
 *
 * 原因 2：避免频繁开关中断的性能损耗
 *
 *   典型场景（没有延迟禁用）：
 *   用户程序：等待 VBlank → 收到信号 → 立即关闭中断
 *            ↓ 过了 5ms
 *            等待 VBlank → 又要开启中断
 *            ↓ 过了 5ms
 *            等待 VBlank → 又要开启中断
 *            ...（每秒可能上百次）
 *
 *   每次开关中断的成本：
 *   - 寄存器读写操作（慢）
 *   - 中断控制器配置
 *   - 可能触发硬件初始化
 *   - CPU 缓存失效
 *
 *   有了延迟禁用（保持开启 5 秒）：
 *   第一次请求 → 开启中断
 *   后续请求  → 中断已经开着，直接用！
 *             → 5 秒内没新请求才真正关闭
 *   结果：大大减少开关次数，提升性能
 *
 * 解决方案：
 * 当引用计数降为 0 时，VBlank 核心不会立即禁用中断，
 * 而是启动一个定时器，在定时器超时后才真正禁用。
 *
 * 相关配置：
 * - &drm_driver.vblank_disable_immediate：是否立即禁用（true=立即，false=延迟）
 * - &drm_driver.max_vblank_count：硬件计数器最大值（影响回绕处理）
 * - vblankoffdelay 模块参数：延迟禁用的时间（毫秒，默认 5000ms）
 *
 * 延迟禁用时序：
 *   引用计数变为0 → 启动定时器（5秒） → 定时器到期 → 真正禁用中断
 *   |               |                      |
 *   |               ← 如果这期间有新请求，取消定时器
 *   |
 *   ← 避免频繁开关中断的开销
 * 开关中断的实际成本
 * 每次开关中断涉及：
 * 1.写硬件寄存器：PCIe/AXI 总线访问，延迟几微秒到几十微秒
 * 2.中断控制器配置：GIC（ARM）或 APIC（x86）需要同步状态
 * 3.硬件初始化：某些芯片关闭中断会复位内部状态
 * 4.缓存失效：寄存器映射的内存区域可能失效
 * 实测数据（假设）：
 * 1.开启中断：约 20μs
 * 2.关闭中断：约 15μs
 * 3.每次往返：35μs
 * 如果每秒开关 200 次：
 * 200 次/秒 × 35μs = 7000μs = 7ms相当于浪费了 0.7% 的 CPU 时间在无意义的开关操作上！
 * 而延迟禁用后可能只开关 1-2 次，几乎没有损耗。
 */

/* Retry timestamp calculation up to 3 times to satisfy
 * drm_timestamp_precision before giving up.
 */
#define DRM_TIMESTAMP_MAXRETRIES 3

/* Threshold in nanoseconds for detection of redundant
 * vblank irq in drm_handle_vblank(). 1 msec should be ok.
 */
#define DRM_REDUNDANT_VBLIRQ_THRESH_NS 1000000

static bool
drm_get_last_vbltimestamp(struct drm_device *dev, unsigned int pipe,
			  ktime_t *tvblank, bool in_vblank_irq);

static unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */

static int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */

module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs] (0: never disable, <0: disable immediately)");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");

static void store_vblank(struct drm_device *dev, unsigned int pipe,
			 u32 vblank_count_inc,
			 ktime_t t_vblank, u32 last)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	assert_spin_locked(&dev->vblank_time_lock);

	vblank->last = last;

	write_seqlock(&vblank->seqlock);
	vblank->time = t_vblank;
	vblank->count += vblank_count_inc;
	write_sequnlock(&vblank->seqlock);
}

static u32 drm_max_vblank_count(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	return vblank->max_vblank_count ?: dev->max_vblank_count;
}

/*
 * "No hw counter" fallback implementation of .get_vblank_counter() hook,
 * if there is no useable hardware frame counter available.
 */
static u32 drm_vblank_no_hw_counter(struct drm_device *dev, unsigned int pipe)
{
	WARN_ON_ONCE(drm_max_vblank_count(dev, pipe) != 0);
	return 0;
}

static u32 __get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (WARN_ON(!crtc))
			return 0;

		if (crtc->funcs->get_vblank_counter)
			return crtc->funcs->get_vblank_counter(crtc);
	}

	if (dev->driver->get_vblank_counter)
		return dev->driver->get_vblank_counter(dev, pipe);

	return drm_vblank_no_hw_counter(dev, pipe);
}

/*
 * Reset the stored timestamp for the current vblank count to correspond
 * to the last vblank occurred.
 *
 * Only to be called from drm_crtc_vblank_on().
 *
 * Note: caller must hold &drm_device.vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_reset_vblank_timestamp(struct drm_device *dev, unsigned int pipe)
{
	u32 cur_vblank;
	bool rc;
	ktime_t t_vblank;
	int count = DRM_TIMESTAMP_MAXRETRIES;

	spin_lock(&dev->vblank_time_lock);

	/*
	 * sample the current counter to avoid random jumps
	 * when drm_vblank_enable() applies the diff
	 */
	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		rc = drm_get_last_vbltimestamp(dev, pipe, &t_vblank, false);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	/*
	 * Only reinitialize corresponding vblank timestamp if high-precision query
	 * available and didn't fail. Otherwise reinitialize delayed at next vblank
	 * interrupt and assign 0 for now, to mark the vblanktimestamp as invalid.
	 */
	if (!rc)
		t_vblank = 0;

	/*
	 * +1 to make sure user will never see the same
	 * vblank counter value before and after a modeset
	 */
	store_vblank(dev, pipe, 1, t_vblank, cur_vblank);

	spin_unlock(&dev->vblank_time_lock);
}

/*
 * Call back into the driver to update the appropriate vblank counter
 * (specified by @pipe).  Deal with wraparound, if it occurred, and
 * update the last read value so we can deal with wraparound on the next
 * call if necessary.
 *
 * Only necessary when going from off->on, to account for frames we
 * didn't get an interrupt for.
 *
 * Note: caller must hold &drm_device.vbl_lock since this reads & writes
 * device vblank fields.
 */
static void drm_update_vblank_count(struct drm_device *dev, unsigned int pipe,
				    bool in_vblank_irq)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	u32 cur_vblank, diff;
	bool rc;
	ktime_t t_vblank;
	int count = DRM_TIMESTAMP_MAXRETRIES;
	int framedur_ns = vblank->framedur_ns;
	u32 max_vblank_count = drm_max_vblank_count(dev, pipe);

	/*
	 * Interrupts were disabled prior to this call, so deal with counter
	 * wrap if needed.
	 * NOTE!  It's possible we lost a full dev->max_vblank_count + 1 events
	 * here if the register is small or we had vblank interrupts off for
	 * a long time.
	 *
	 * We repeat the hardware vblank counter & timestamp query until
	 * we get consistent results. This to prevent races between gpu
	 * updating its hardware counter while we are retrieving the
	 * corresponding vblank timestamp.
	 */
	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		rc = drm_get_last_vbltimestamp(dev, pipe, &t_vblank, in_vblank_irq);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	if (max_vblank_count) {
		/* trust the hw counter when it's around */
		diff = (cur_vblank - vblank->last) & max_vblank_count;
	} else if (rc && framedur_ns) {
		u64 diff_ns = ktime_to_ns(ktime_sub(t_vblank, vblank->time));

		/*
		 * Figure out how many vblanks we've missed based
		 * on the difference in the timestamps and the
		 * frame/field duration.
		 */
		diff = DIV_ROUND_CLOSEST_ULL(diff_ns, framedur_ns);

		if (diff == 0 && in_vblank_irq)
			DRM_DEBUG_VBL("crtc %u: Redundant vblirq ignored."
				      " diff_ns = %lld, framedur_ns = %d)\n",
				      pipe, (long long) diff_ns, framedur_ns);
	} else {
		/* some kind of default for drivers w/o accurate vbl timestamping */
		diff = in_vblank_irq ? 1 : 0;
	}

	/*
	 * Within a drm_vblank_pre_modeset - drm_vblank_post_modeset
	 * interval? If so then vblank irqs keep running and it will likely
	 * happen that the hardware vblank counter is not trustworthy as it
	 * might reset at some point in that interval and vblank timestamps
	 * are not trustworthy either in that interval. Iow. this can result
	 * in a bogus diff >> 1 which must be avoided as it would cause
	 * random large forward jumps of the software vblank counter.
	 */
	if (diff > 1 && (vblank->inmodeset & 0x2)) {
		DRM_DEBUG_VBL("clamping vblank bump to 1 on crtc %u: diffr=%u"
			      " due to pre-modeset.\n", pipe, diff);
		diff = 1;
	}

	DRM_DEBUG_VBL("updating vblank count on crtc %u:"
		      " current=%llu, diff=%u, hw=%u hw_last=%u\n",
		      pipe, vblank->count, diff, cur_vblank, vblank->last);

	if (diff == 0) {
		WARN_ON_ONCE(cur_vblank != vblank->last);
		return;
	}

	/*
	 * Only reinitialize corresponding vblank timestamp if high-precision query
	 * available and didn't fail, or we were called from the vblank interrupt.
	 * Otherwise reinitialize delayed at next vblank interrupt and assign 0
	 * for now, to mark the vblanktimestamp as invalid.
	 */
	if (!rc && !in_vblank_irq)
		t_vblank = 0;

	store_vblank(dev, pipe, diff, t_vblank, cur_vblank);
}

static u64 drm_vblank_count(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	if (WARN_ON(pipe >= dev->num_crtcs))
		return 0;

	return vblank->count;
}

/**
 * drm_crtc_accurate_vblank_count - retrieve the master vblank counter
 * @crtc: which counter to retrieve
 *
 * This function is similar to drm_crtc_vblank_count() but this function
 * interpolates to handle a race with vblank interrupts using the high precision
 * timestamping support.
 *
 * This is mostly useful for hardware that can obtain the scanout position, but
 * doesn't have a hardware frame counter.
 */
u64 drm_crtc_accurate_vblank_count(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	u64 vblank;
	unsigned long flags;

	WARN_ONCE(drm_debug & DRM_UT_VBL && !dev->driver->get_vblank_timestamp,
		  "This function requires support for accurate vblank timestamps.");

	spin_lock_irqsave(&dev->vblank_time_lock, flags);

	drm_update_vblank_count(dev, pipe, false);
	vblank = drm_vblank_count(dev, pipe);

	spin_unlock_irqrestore(&dev->vblank_time_lock, flags);

	return vblank;
}
EXPORT_SYMBOL(drm_crtc_accurate_vblank_count);

/**
 * __disable_vblank - 禁用指定 CRTC 的 vblank 中断（内部函数）
 * @dev: DRM 设备
 * @pipe: CRTC 索引号（pipe 是历史术语，实际就是 CRTC 编号）
 *
 * 这个函数负责关闭硬件的 vblank 中断。它体现了 DRM 框架中新旧接口的兼容性设计：
 *
 * 调用路径优先级：
 * 1. 优先尝试现代 KMS 接口：crtc->funcs->disable_vblank(crtc)
 * 2. 降级到旧式驱动接口：dev->driver->disable_vblank(dev, pipe)
 *
 * 为什么需要两种方式？
 * - 新驱动（KMS）：使用面向对象的 CRTC 回调，更清晰直观
 * - 旧驱动（非KMS）：使用驱动级全局回调 + pipe 索引，兼容历史代码
 */
static void __disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	/* 检查驱动是否支持 KMS（内核模式设置） */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		/* 现代方式：通过 pipe 索引获取对应的 CRTC 对象 */
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		/* 防御性检查：确保 CRTC 存在 */
		if (WARN_ON(!crtc))
			return;

		/* 如果 CRTC 实现了 disable_vblank 回调，使用现代接口 */
		if (crtc->funcs->disable_vblank) {
			crtc->funcs->disable_vblank(crtc);
			return;
		}
		/* 否则继续尝试旧式接口（下面的代码） */
	}

	/* 降级方案：使用驱动级别的旧式接口（非 KMS 驱动或 CRTC 未实现新接口） */
	dev->driver->disable_vblank(dev, pipe);
}

/*
 * Disable vblank irq's on crtc, make sure that last vblank count
 * of hardware and corresponding consistent software vblank counter
 * are preserved, even if there are any spurious vblank irq's after
 * disable.
 */
void drm_vblank_disable_and_save(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	unsigned long irqflags;

	assert_spin_locked(&dev->vbl_lock);

	/* Prevent vblank irq processing while disabling vblank irqs,
	 * so no updates of timestamps or count can happen after we've
	 * disabled. Needed to prevent races in case of delayed irq's.
	 */
	spin_lock_irqsave(&dev->vblank_time_lock, irqflags);

	/*
	 * Update vblank count and disable vblank interrupts only if the
	 * interrupts were enabled. This avoids calling the ->disable_vblank()
	 * operation in atomic context with the hardware potentially runtime
	 * suspended.
	 */
	if (!vblank->enabled)
		goto out;

	/*
	 * Update the count and timestamp to maintain the
	 * appearance that the counter has been ticking all along until
	 * this time. This makes the count account for the entire time
	 * between drm_crtc_vblank_on() and drm_crtc_vblank_off().
	 */
	drm_update_vblank_count(dev, pipe, false);
	__disable_vblank(dev, pipe);
	vblank->enabled = false;

out:
	spin_unlock_irqrestore(&dev->vblank_time_lock, irqflags);
}

static void vblank_disable_fn(struct timer_list *t)
{
	struct drm_vblank_crtc *vblank = from_timer(vblank, t, disable_timer);
	struct drm_device *dev = vblank->dev;
	unsigned int pipe = vblank->pipe;
	unsigned long irqflags;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	if (atomic_read(&vblank->refcount) == 0 && vblank->enabled) {
		DRM_DEBUG("disabling vblank on crtc %u\n", pipe);
		drm_vblank_disable_and_save(dev, pipe);
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
}

void drm_vblank_cleanup(struct drm_device *dev)
{
	unsigned int pipe;

	/* Bail if the driver didn't call drm_vblank_init() */
	if (dev->num_crtcs == 0)
		return;

	for (pipe = 0; pipe < dev->num_crtcs; pipe++) {
		struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

		WARN_ON(READ_ONCE(vblank->enabled) &&
			drm_core_check_feature(dev, DRIVER_MODESET));

		del_timer_sync(&vblank->disable_timer);
	}

	kfree(dev->vblank);

	dev->num_crtcs = 0;
}

/**
 * drm_vblank_init - initialize vblank support
 * @dev: DRM device
 * @num_crtcs: number of CRTCs supported by @dev
 *
 * This function initializes vblank support for @num_crtcs display pipelines.
 * Cleanup is handled by the DRM core, or through calling drm_dev_fini() for
 * drivers with a &drm_driver.release callback.
 *
 * Returns:
 * Zero on success or a negative error code on failure.
 */
/**
 * drm_vblank_init - 初始化 DRM 设备的 VBlank 管理子系统
 * @dev: DRM 设备
 * @num_crtcs: 该设备拥有的 CRTC 数量
 *
 * 为每个 CRTC 分配并初始化 VBlank 跟踪结构，建立 VBlank 事件通知机制。
 *
 * 必须在驱动初始化的早期阶段调用，在任何 VBlank 相关操作之前完成。
 *
 * 返回值：0 表示成功，-ENOMEM 表示内存分配失败
 */
int drm_vblank_init(struct drm_device *dev, unsigned int num_crtcs)
{
	int ret = -ENOMEM;
	unsigned int i;

	/*
	 * 初始化两把关键锁：
	 *
	 * vbl_lock: 保护 VBlank 引用计数和使能/禁用状态
	 *   使用场景：drm_vblank_get/put 操作时需要持有
	 *
	 * vblank_time_lock: 保护时间戳相关操作（历史遗留，现在基本不用）
	 *   大部分时间戳操作已改用 seqlock（每个 CRTC 独立锁）
	 */
	spin_lock_init(&dev->vbl_lock);
	spin_lock_init(&dev->vblank_time_lock);

	dev->num_crtcs = num_crtcs;

	/*
	 * 为每个 CRTC 分配一个 drm_vblank_crtc 结构。
	 *
	 * kcalloc：分配并清零，避免野指针和未初始化字段。
	 *
	 * 为什么是数组而非链表？
	 * - CRTC 数量固定（初始化后不变）
	 * - 通过 pipe 索引直接访问，O(1) 时间复杂度
	 * - 内存连续，缓存友好
	 */
	dev->vblank = kcalloc(num_crtcs, sizeof(*dev->vblank), GFP_KERNEL);
	if (!dev->vblank)
		goto err;

	/* 为每个 CRTC 初始化 VBlank 跟踪结构 */
	for (i = 0; i < num_crtcs; i++) {
		struct drm_vblank_crtc *vblank = &dev->vblank[i];

		vblank->dev = dev;   /* 反向指针，指回 DRM 设备 */
		vblank->pipe = i;    /* CRTC 索引号（0, 1, 2...） */

		/*
		 * 初始化等待队列：用于 drm_wait_vblank() 等阻塞接口。
		 * 用户空间进程调用 DRM_IOCTL_WAIT_VBLANK 时会在此队列休眠，
		 * VBlank 中断到来时唤醒所有等待者。
		 */
		init_waitqueue_head(&vblank->queue);

		/*
		 * 初始化延迟禁用定时器：
		 *
		 * vblank_disable_fn：定时器到期时的回调函数
		 * 0：标志位（此处无特殊标志）
		 *
		 * 工作原理：
		 *   当 VBlank 引用计数降为 0 时，不立即禁用中断，
		 *   而是启动这个定时器（默认 5 秒）。
		 *   如果 5 秒内没有新的 VBlank 请求，定时器到期才真正禁用。
		 *   这样避免了频繁开关中断的开销。
		 */
		timer_setup(&vblank->disable_timer, vblank_disable_fn, 0);

		/*
		 * 初始化序列锁（seqlock）：
		 *
		 * 保护 vblank->count 和 vblank->time 的一致性。
		 *
		 * 为什么用 seqlock 而非普通自旋锁？
		 * - 读多写少：大量读取 VBlank 计数和时间戳，极少写入（仅在中断时）
		 * - 读者无锁：读者不需要加锁，通过序列号检测是否需要重试
		 * - 写者优先：中断处理函数（写者）不会被读者阻塞
		 */
		seqlock_init(&vblank->seqlock);
	}

	DRM_INFO("Supports vblank timestamp caching Rev 2 (21.10.2013).\n");

	/*
	 * 检查驱动是否提供了高精度时间戳查询接口：
	 *
	 * get_vblank_timestamp：驱动提供的回调函数，
	 * 用于获取 VBlank 发生的精确时间戳（通常通过读取硬件计数器推算）。
	 *
	 * 有此接口的好处：
	 * - 时间戳精度更高（微秒级甚至纳秒级）
	 * - 可以支持"立即禁用 VBlank"模式（下面会检查）
	 */
	if (dev->driver->get_vblank_timestamp)
		DRM_INFO("Driver supports precise vblank timestamp query.\n");
	else
		DRM_INFO("No driver support for vblank timestamp query.\n");

	/*
	 * 立即禁用模式的前提条件检查：
	 *
	 * vblank_disable_immediate = true：
	 *   VBlank 引用计数降为 0 时立即禁用中断（不等 5 秒）
	 *
	 * 但这个模式有前提：必须有 get_vblank_timestamp 接口。
	 *
	 * 为什么？用一个具体场景说明：
	 *
	 * 假设 60Hz 显示器，中断在第 100 次 VBlank 时被立即关闭：
	 *
	 *   VBlank #100  → 中断触发，count=100，随后 refcount 降为 0，立即关闭中断
	 *   VBlank #101  → 显示器照常刷新，但没有中断了，count 停留在 100
	 *   VBlank #102  → 同上，count 仍然是 100
	 *   ...
	 *   VBlank #110  → 同上，count 仍然是 100
	 *   VBlank #111  → 用户空间又来查询了，重新开启中断
	 *
	 * 问题来了：重新开启中断时，count 还是 100，但实际已经过了 111 次。
	 * 系统需要把 count 从 100 补到 111，差值 = 11 次。
	 *
	 * 怎么算出这个差值 11？→ 看 drm_update_vblank_count()：
	 *
	 *   方法 1：硬件计数器（如果有的话）
	 *     diff = 当前硬件计数 - 上次记录的硬件计数 = 111 - 100 = 11  ✓
	 *
	 *   方法 2：时间戳推算（需要 get_vblank_timestamp）
	 *     diff = (当前时间戳 - 关闭时的时间戳) / 每帧时长
	 *          = (111 * 16.6ms - 100 * 16.6ms) / 16.6ms = 11  ✓
	 *
	 *   方法 3：两者都没有
	 *     diff = 0（直接放弃，计数器永久丢失 11 次）  ✗
	 *
	 * 所以：如果驱动没有 get_vblank_timestamp，方法 2 用不了，
	 * 万一方法 1 也不可靠（比如硬件计数器会溢出或重置），
	 * 中断一旦关闭就无法准确补回错过的次数。
	 * 这种情况下，立即禁用太危险了，必须回退到延迟禁用模式
	 * （延迟 5 秒不关中断，让计数器有时间保持准确）。
	 */
	if (dev->vblank_disable_immediate && !dev->driver->get_vblank_timestamp) {
		dev->vblank_disable_immediate = false;
		DRM_INFO("Setting vblank_disable_immediate to false because "
			 "get_vblank_timestamp == NULL\n");
	}

	return 0;

err:
	/* 分配失败，清零 CRTC 数量，防止后续代码误用 */
	dev->num_crtcs = 0;
	return ret;
}
EXPORT_SYMBOL(drm_vblank_init);

/**
 * drm_crtc_vblank_waitqueue - get vblank waitqueue for the CRTC
 * @crtc: which CRTC's vblank waitqueue to retrieve
 *
 * This function returns a pointer to the vblank waitqueue for the CRTC.
 * Drivers can use this to implement vblank waits using wait_event() and related
 * functions.
 */
wait_queue_head_t *drm_crtc_vblank_waitqueue(struct drm_crtc *crtc)
{
	return &crtc->dev->vblank[drm_crtc_index(crtc)].queue;
}
EXPORT_SYMBOL(drm_crtc_vblank_waitqueue);


/**
 * drm_calc_timestamping_constants - calculate vblank timestamp constants
 * @crtc: drm_crtc whose timestamp constants should be updated.
 * @mode: display mode containing the scanout timings
 *
 * Calculate and store various constants which are later needed by vblank and
 * swap-completion timestamping, e.g, by
 * drm_calc_vbltimestamp_from_scanoutpos(). They are derived from CRTC's true
 * scanout timing, so they take things like panel scaling or other adjustments
 * into account.
 */
void drm_calc_timestamping_constants(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	int linedur_ns = 0, framedur_ns = 0;
	int dotclock = mode->crtc_clock;

	if (!dev->num_crtcs)
		return;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	/* Valid dotclock? */
	if (dotclock > 0) {
		int frame_size = mode->crtc_htotal * mode->crtc_vtotal;

		/*
		 * Convert scanline length in pixels and video
		 * dot clock to line duration and frame duration
		 * in nanoseconds:
		 */
		linedur_ns  = div_u64((u64) mode->crtc_htotal * 1000000, dotclock);
		framedur_ns = div_u64((u64) frame_size * 1000000, dotclock);

		/*
		 * Fields of interlaced scanout modes are only half a frame duration.
		 */
		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			framedur_ns /= 2;
	} else
		DRM_ERROR("crtc %u: Can't calculate constants, dotclock = 0!\n",
			  crtc->base.id);

	vblank->linedur_ns  = linedur_ns;
	vblank->framedur_ns = framedur_ns;
	vblank->hwmode = *mode;

	DRM_DEBUG("crtc %u: hwmode: htotal %d, vtotal %d, vdisplay %d\n",
		  crtc->base.id, mode->crtc_htotal,
		  mode->crtc_vtotal, mode->crtc_vdisplay);
	DRM_DEBUG("crtc %u: clock %d kHz framedur %d linedur %d\n",
		  crtc->base.id, dotclock, framedur_ns, linedur_ns);
}
EXPORT_SYMBOL(drm_calc_timestamping_constants);

/**
 * drm_calc_vbltimestamp_from_scanoutpos - precise vblank timestamp helper
 * @dev: DRM device
 * @pipe: index of CRTC whose vblank timestamp to retrieve
 * @max_error: Desired maximum allowable error in timestamps (nanosecs)
 *             On return contains true maximum error of timestamp
 * @vblank_time: Pointer to time which should receive the timestamp
 * @in_vblank_irq:
 *     True when called from drm_crtc_handle_vblank().  Some drivers
 *     need to apply some workarounds for gpu-specific vblank irq quirks
 *     if flag is set.
 *
 * Implements calculation of exact vblank timestamps from given drm_display_mode
 * timings and current video scanout position of a CRTC. This can be directly
 * used as the &drm_driver.get_vblank_timestamp implementation of a kms driver
 * if &drm_driver.get_scanout_position is implemented.
 *
 * The current implementation only handles standard video modes. For double scan
 * and interlaced modes the driver is supposed to adjust the hardware mode
 * (taken from &drm_crtc_state.adjusted mode for atomic modeset drivers) to
 * match the scanout position reported.
 *
 * Note that atomic drivers must call drm_calc_timestamping_constants() before
 * enabling a CRTC. The atomic helpers already take care of that in
 * drm_atomic_helper_update_legacy_modeset_state().
 *
 * Returns:
 *
 * Returns true on success, and false on failure, i.e. when no accurate
 * timestamp could be acquired.
 */
bool drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
					   unsigned int pipe,
					   int *max_error,
					   ktime_t *vblank_time,
					   bool in_vblank_irq)
{
	struct timespec64 ts_etime, ts_vblank_time;
	ktime_t stime, etime;
	bool vbl_status;
	struct drm_crtc *crtc;
	const struct drm_display_mode *mode;
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	int vpos, hpos, i;
	int delta_ns, duration_ns;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return false;

	crtc = drm_crtc_from_index(dev, pipe);

	if (pipe >= dev->num_crtcs || !crtc) {
		DRM_ERROR("Invalid crtc %u\n", pipe);
		return false;
	}

	/* Scanout position query not supported? Should not happen. */
	if (!dev->driver->get_scanout_position) {
		DRM_ERROR("Called from driver w/o get_scanout_position()!?\n");
		return false;
	}

	if (drm_drv_uses_atomic_modeset(dev))
		mode = &vblank->hwmode;
	else
		mode = &crtc->hwmode;

	/* If mode timing undefined, just return as no-op:
	 * Happens during initial modesetting of a crtc.
	 */
	if (mode->crtc_clock == 0) {
		DRM_DEBUG("crtc %u: Noop due to uninitialized mode.\n", pipe);
		WARN_ON_ONCE(drm_drv_uses_atomic_modeset(dev));

		return false;
	}

	/* Get current scanout position with system timestamp.
	 * Repeat query up to DRM_TIMESTAMP_MAXRETRIES times
	 * if single query takes longer than max_error nanoseconds.
	 *
	 * This guarantees a tight bound on maximum error if
	 * code gets preempted or delayed for some reason.
	 */
	for (i = 0; i < DRM_TIMESTAMP_MAXRETRIES; i++) {
		/*
		 * Get vertical and horizontal scanout position vpos, hpos,
		 * and bounding timestamps stime, etime, pre/post query.
		 */
		vbl_status = dev->driver->get_scanout_position(dev, pipe,
							       in_vblank_irq,
							       &vpos, &hpos,
							       &stime, &etime,
							       mode);

		/* Return as no-op if scanout query unsupported or failed. */
		if (!vbl_status) {
			DRM_DEBUG("crtc %u : scanoutpos query failed.\n",
				  pipe);
			return false;
		}

		/* Compute uncertainty in timestamp of scanout position query. */
		duration_ns = ktime_to_ns(etime) - ktime_to_ns(stime);

		/* Accept result with <  max_error nsecs timing uncertainty. */
		if (duration_ns <= *max_error)
			break;
	}

	/* Noisy system timing? */
	if (i == DRM_TIMESTAMP_MAXRETRIES) {
		DRM_DEBUG("crtc %u: Noisy timestamp %d us > %d us [%d reps].\n",
			  pipe, duration_ns/1000, *max_error/1000, i);
	}

	/* Return upper bound of timestamp precision error. */
	*max_error = duration_ns;

	/* Convert scanout position into elapsed time at raw_time query
	 * since start of scanout at first display scanline. delta_ns
	 * can be negative if start of scanout hasn't happened yet.
	 */
	delta_ns = div_s64(1000000LL * (vpos * mode->crtc_htotal + hpos),
			   mode->crtc_clock);

	/* Subtract time delta from raw timestamp to get final
	 * vblank_time timestamp for end of vblank.
	 */
	*vblank_time = ktime_sub_ns(etime, delta_ns);

	if ((drm_debug & DRM_UT_VBL) == 0)
		return true;

	ts_etime = ktime_to_timespec64(etime);
	ts_vblank_time = ktime_to_timespec64(*vblank_time);

	DRM_DEBUG_VBL("crtc %u : v p(%d,%d)@ %lld.%06ld -> %lld.%06ld [e %d us, %d rep]\n",
		      pipe, hpos, vpos,
		      (u64)ts_etime.tv_sec, ts_etime.tv_nsec / 1000,
		      (u64)ts_vblank_time.tv_sec, ts_vblank_time.tv_nsec / 1000,
		      duration_ns / 1000, i);

	return true;
}
EXPORT_SYMBOL(drm_calc_vbltimestamp_from_scanoutpos);

/**
 * drm_get_last_vbltimestamp - retrieve raw timestamp for the most recent
 *                             vblank interval
 * @dev: DRM device
 * @pipe: index of CRTC whose vblank timestamp to retrieve
 * @tvblank: Pointer to target time which should receive the timestamp
 * @in_vblank_irq:
 *     True when called from drm_crtc_handle_vblank().  Some drivers
 *     need to apply some workarounds for gpu-specific vblank irq quirks
 *     if flag is set.
 *
 * Fetches the system timestamp corresponding to the time of the most recent
 * vblank interval on specified CRTC. May call into kms-driver to
 * compute the timestamp with a high-precision GPU specific method.
 *
 * Returns zero if timestamp originates from uncorrected do_gettimeofday()
 * call, i.e., it isn't very precisely locked to the true vblank.
 *
 * Returns:
 * True if timestamp is considered to be very precise, false otherwise.
 */
static bool
drm_get_last_vbltimestamp(struct drm_device *dev, unsigned int pipe,
			  ktime_t *tvblank, bool in_vblank_irq)
{
	bool ret = false;

	/* Define requested maximum error on timestamps (nanoseconds). */
	int max_error = (int) drm_timestamp_precision * 1000;

	/* Query driver if possible and precision timestamping enabled. */
	if (dev->driver->get_vblank_timestamp && (max_error > 0))
		ret = dev->driver->get_vblank_timestamp(dev, pipe, &max_error,
							tvblank, in_vblank_irq);

	/* GPU high precision timestamp query unsupported or failed.
	 * Return current monotonic/gettimeofday timestamp as best estimate.
	 */
	if (!ret)
		*tvblank = ktime_get();

	return ret;
}

/**
 * drm_crtc_vblank_count - retrieve "cooked" vblank counter value
 * @crtc: which counter to retrieve
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Note that this timer isn't correct against a racing
 * vblank interrupt (since it only reports the software vblank counter), see
 * drm_crtc_accurate_vblank_count() for such use-cases.
 *
 * Returns:
 * The software vblank counter.
 */
u64 drm_crtc_vblank_count(struct drm_crtc *crtc)
{
	return drm_vblank_count(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_count);

/**
 * drm_vblank_count_and_time - retrieve "cooked" vblank counter value and the
 *     system timestamp corresponding to that vblank counter value.
 * @dev: DRM device
 * @pipe: index of CRTC whose counter to retrieve
 * @vblanktime: Pointer to ktime_t to receive the vblank timestamp.
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Returns corresponding system timestamp of the time
 * of the vblank interval that corresponds to the current vblank counter value.
 *
 * This is the legacy version of drm_crtc_vblank_count_and_time().
 */
static u64 drm_vblank_count_and_time(struct drm_device *dev, unsigned int pipe,
				     ktime_t *vblanktime)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	u64 vblank_count;
	unsigned int seq;

	if (WARN_ON(pipe >= dev->num_crtcs)) {
		*vblanktime = 0;
		return 0;
	}

	do {
		seq = read_seqbegin(&vblank->seqlock);
		vblank_count = vblank->count;
		*vblanktime = vblank->time;
	} while (read_seqretry(&vblank->seqlock, seq));

	return vblank_count;
}

/**
 * drm_crtc_vblank_count_and_time - retrieve "cooked" vblank counter value
 *     and the system timestamp corresponding to that vblank counter value
 * @crtc: which counter to retrieve
 * @vblanktime: Pointer to time to receive the vblank timestamp.
 *
 * Fetches the "cooked" vblank count value that represents the number of
 * vblank events since the system was booted, including lost events due to
 * modesetting activity. Returns corresponding system timestamp of the time
 * of the vblank interval that corresponds to the current vblank counter value.
 */
u64 drm_crtc_vblank_count_and_time(struct drm_crtc *crtc,
				   ktime_t *vblanktime)
{
	return drm_vblank_count_and_time(crtc->dev, drm_crtc_index(crtc),
					 vblanktime);
}
EXPORT_SYMBOL(drm_crtc_vblank_count_and_time);


/**
 * send_vblank_event - 填充事件时间戳并发送给用户空间
 *
 * 根据事件类型，将 VBlank 序列号和时间戳填入用户空间的事件结构体，
 * 然后通过 drm_send_event_locked() 投递到用户空间的 read() 队列。
 *
 * @seq: VBlank 序列号（从系统启动以来的 VBlank 总数）
 * @now: VBlank 发生的精确时间戳（内核高精度单调时钟）
 */
static void send_vblank_event(struct drm_device *dev,
		struct drm_pending_vblank_event *e,
		u64 seq, ktime_t now)
{
	struct timespec64 tv;

	/* 根据事件类型选择不同的数据格式 */
	switch (e->event.base.type) {
	case DRM_EVENT_VBLANK:        /* 用户等待 VBlank 的事件 */
	case DRM_EVENT_FLIP_COMPLETE: /* Page flip 完成事件 */
		/* 旧格式：秒 + 微秒（兼容旧版用户空间） */
		tv = ktime_to_timespec64(now);
		e->event.vbl.sequence = seq;
		/*
		 * 用户空间结构体用 32 位存储秒和微秒。
		 * 安全性：Linux 4.15+ 已改用单调时钟，不会受系统时间调整影响。
		 */
		e->event.vbl.tv_sec = tv.tv_sec;
		e->event.vbl.tv_usec = tv.tv_nsec / 1000; /* 纳秒转微秒 */
		break;
	case DRM_EVENT_CRTC_SEQUENCE: /* 新式精确序列事件 */
		/* 新格式：64 位纳秒时间戳（更高精度） */
		if (seq)
			e->event.seq.sequence = seq;
		e->event.seq.time_ns = ktime_to_ns(now); /* 直接用纳秒 */
		break;
	}
	trace_drm_vblank_event_delivered(e->base.file_priv, e->pipe, seq);
	drm_send_event_locked(dev, &e->base); /* 投递到用户空间 */
}

/**
 * drm_crtc_arm_vblank_event - arm vblank event after pageflip
 * @crtc: the source CRTC of the vblank event
 * @e: the event to send
 *
 * A lot of drivers need to generate vblank events for the very next vblank
 * interrupt. For example when the page flip interrupt happens when the page
 * flip gets armed, but not when it actually executes within the next vblank
 * period. This helper function implements exactly the required vblank arming
 * behaviour.
 *
 * NOTE: Drivers using this to send out the &drm_crtc_state.event as part of an
 * atomic commit must ensure that the next vblank happens at exactly the same
 * time as the atomic commit is committed to the hardware. This function itself
 * does **not** protect against the next vblank interrupt racing with either this
 * function call or the atomic commit operation. A possible sequence could be:
 *
 * 1. Driver commits new hardware state into vblank-synchronized registers.
 * 2. A vblank happens, committing the hardware state. Also the corresponding
 *    vblank interrupt is fired off and fully processed by the interrupt
 *    handler.
 * 3. The atomic commit operation proceeds to call drm_crtc_arm_vblank_event().
 * 4. The event is only send out for the next vblank, which is wrong.
 *
 * An equivalent race can happen when the driver calls
 * drm_crtc_arm_vblank_event() before writing out the new hardware state.
 *
 * The only way to make this work safely is to prevent the vblank from firing
 * (and the hardware from committing anything else) until the entire atomic
 * commit sequence has run to completion. If the hardware does not have such a
 * feature (e.g. using a "go" bit), then it is unsafe to use this functions.
 * Instead drivers need to manually send out the event from their interrupt
 * handler by calling drm_crtc_send_vblank_event() and make sure that there's no
 * possible race with the hardware committing the atomic update.
 *
 * Caller must hold a vblank reference for the event @e, which will be dropped
 * when the next vblank arrives.
 */
void drm_crtc_arm_vblank_event(struct drm_crtc *crtc,
			       struct drm_pending_vblank_event *e)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);

	assert_spin_locked(&dev->event_lock);

	e->pipe = pipe;
	e->sequence = drm_crtc_accurate_vblank_count(crtc) + 1;
	list_add_tail(&e->base.link, &dev->vblank_event_list);
}
EXPORT_SYMBOL(drm_crtc_arm_vblank_event);

/**
 * drm_crtc_send_vblank_event - 立即发送 VBlank 事件给用户空间
 *
 * 页面翻转（page flip）完成后的通知助手函数。
 *
 * 与 drm_crtc_arm_vblank_event() 的区别：
 *
 *   arm_vblank_event：
 *     "预装弹药"——把事件挂到等待列表，等下一次 VBlank 中断到来时自动发送。
 *     适用于有 "GO 位" 机制的硬件（写入配置后，硬件保证在 VBlank 时刻原子生效）。
 *
 *   send_vblank_event（本函数）：
 *     "立即开火"——获取当前最新的 VBlank 计数和时间戳，马上发送事件。
 *     适用于驱动在中断处理函数中，已经确认硬件状态已切换，需要立即通知用户空间。
 *
 * 典型调用场景：
 *   驱动的 VBlank/页面翻转中断处理函数中：
 *     spin_lock(&dev->event_lock);
 *     if (crtc_state->event) {
 *         drm_crtc_send_vblank_event(crtc, crtc_state->event);
 *         crtc_state->event = NULL;
 *     }
 *     spin_unlock(&dev->event_lock);
 *
 * 注意：调用者必须持有 dev->event_lock 自旋锁。
 *
 * @crtc: 产生 VBlank 事件的 CRTC
 * @e: 待发送的事件
 */
void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
				struct drm_pending_vblank_event *e)
{
	struct drm_device *dev = crtc->dev;
	u64 seq;
	unsigned int pipe = drm_crtc_index(crtc);
	ktime_t now;

	if (dev->num_crtcs > 0) {
		/*
		 * 正常路径：设备有 CRTC（绝大多数情况）
		 * 从 VBlank 计数器获取最新的序列号和对应的精确时间戳。
		 * drm_vblank_count_and_time() 使用 seqlock 保护，
		 * 确保 seq 和 now 是同一个 VBlank 时刻的一致快照。
		 */
		seq = drm_vblank_count_and_time(dev, pipe, &now);
	} else {
		/*
		 * 降级路径：设备没有 CRTC（极少见，如纯渲染设备）
		 * 没有硬件 VBlank 计数器，序列号设为 0，
		 * 时间戳用当前单调时钟替代。
		 */
		seq = 0;

		now = ktime_get();
	}
	e->pipe = pipe;
	/*
	 * send_vblank_event() 会根据事件类型填充用户空间结构体：
	 * - DRM_EVENT_VBLANK / DRM_EVENT_FLIP_COMPLETE：填充 tv_sec + tv_usec + sequence
	 * - DRM_EVENT_CRTC_SEQUENCE：填充 time_ns + sequence
	 * 然后通过 drm_send_event_locked() 将事件投递到用户空间的 read() 队列。
	 */
	send_vblank_event(dev, e, seq, now);
}
EXPORT_SYMBOL(drm_crtc_send_vblank_event);

static int __enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);

		if (WARN_ON(!crtc))
			return 0;

		if (crtc->funcs->enable_vblank)
			return crtc->funcs->enable_vblank(crtc);
	}

	return dev->driver->enable_vblank(dev, pipe);
}

static int drm_vblank_enable(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	int ret = 0;

	assert_spin_locked(&dev->vbl_lock);

	spin_lock(&dev->vblank_time_lock);

	if (!vblank->enabled) {
		/*
		 * Enable vblank irqs under vblank_time_lock protection.
		 * All vblank count & timestamp updates are held off
		 * until we are done reinitializing master counter and
		 * timestamps. Filtercode in drm_handle_vblank() will
		 * prevent double-accounting of same vblank interval.
		 */
		ret = __enable_vblank(dev, pipe);
		DRM_DEBUG("enabling vblank on crtc %u, ret: %d\n", pipe, ret);
		if (ret) {
			atomic_dec(&vblank->refcount);
		} else {
			drm_update_vblank_count(dev, pipe, 0);
			/* drm_update_vblank_count() includes a wmb so we just
			 * need to ensure that the compiler emits the write
			 * to mark the vblank as enabled after the call
			 * to drm_update_vblank_count().
			 */
			WRITE_ONCE(vblank->enabled, true);
		}
	}

	spin_unlock(&dev->vblank_time_lock);

	return ret;
}

static int drm_vblank_get(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	unsigned long irqflags;
	int ret = 0;

	if (!dev->num_crtcs)
		return -EINVAL;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return -EINVAL;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	/* Going from 0->1 means we have to enable interrupts again */
	if (atomic_add_return(1, &vblank->refcount) == 1) {
		ret = drm_vblank_enable(dev, pipe);
	} else {
		if (!vblank->enabled) {
			atomic_dec(&vblank->refcount);
			ret = -EINVAL;
		}
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	return ret;
}

/**
 * drm_crtc_vblank_get - get a reference count on vblank events
 * @crtc: which CRTC to own
 *
 * Acquire a reference count on vblank events to avoid having them disabled
 * while in use.
 *
 * Returns:
 * Zero on success or a negative error code on failure.
 */
int drm_crtc_vblank_get(struct drm_crtc *crtc)
{
	return drm_vblank_get(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_get);

static void drm_vblank_put(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	if (WARN_ON(atomic_read(&vblank->refcount) == 0))
		return;

	/* Last user schedules interrupt disable */
	if (atomic_dec_and_test(&vblank->refcount)) {
		if (drm_vblank_offdelay == 0)
			return;
		else if (drm_vblank_offdelay < 0)
			vblank_disable_fn(&vblank->disable_timer);
		else if (!dev->vblank_disable_immediate)
			mod_timer(&vblank->disable_timer,
				  jiffies + ((drm_vblank_offdelay * HZ)/1000));
	}
}

/**
 * drm_crtc_vblank_put - give up ownership of vblank events
 * @crtc: which counter to give up
 *
 * Release ownership of a given vblank counter, turning off interrupts
 * if possible. Disable interrupts after drm_vblank_offdelay milliseconds.
 */
void drm_crtc_vblank_put(struct drm_crtc *crtc)
{
	drm_vblank_put(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_put);

/**
 * drm_wait_one_vblank - wait for one vblank
 * @dev: DRM device
 * @pipe: CRTC index
 *
 * This waits for one vblank to pass on @pipe, using the irq driver interfaces.
 * It is a failure to call this when the vblank irq for @pipe is disabled, e.g.
 * due to lack of driver support or because the crtc is off.
 *
 * This is the legacy version of drm_crtc_wait_one_vblank().
 */
void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	int ret;
	u64 last;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	ret = drm_vblank_get(dev, pipe);
	if (WARN(ret, "vblank not available on crtc %i, ret=%i\n", pipe, ret))
		return;

	last = drm_vblank_count(dev, pipe);

	ret = wait_event_timeout(vblank->queue,
				 last != drm_vblank_count(dev, pipe),
				 msecs_to_jiffies(100));

	WARN(ret == 0, "vblank wait timed out on crtc %i\n", pipe);

	drm_vblank_put(dev, pipe);
}
EXPORT_SYMBOL(drm_wait_one_vblank);

/**
 * drm_crtc_wait_one_vblank - wait for one vblank
 * @crtc: DRM crtc
 *
 * This waits for one vblank to pass on @crtc, using the irq driver interfaces.
 * It is a failure to call this when the vblank irq for @crtc is disabled, e.g.
 * due to lack of driver support or because the crtc is off.
 */
void drm_crtc_wait_one_vblank(struct drm_crtc *crtc)
{
	drm_wait_one_vblank(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_wait_one_vblank);

/**
 * drm_crtc_vblank_off - disable vblank events on a CRTC
 * @crtc: CRTC in question
 *
 * Drivers can use this function to shut down the vblank interrupt handling when
 * disabling a crtc. This function ensures that the latest vblank frame count is
 * stored so that drm_vblank_on can restore it again.
 *
 * Drivers must use this function when the hardware vblank counter can get
 * reset, e.g. when suspending or disabling the @crtc in general.
 */
void drm_crtc_vblank_off(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct drm_pending_vblank_event *e, *t;

	ktime_t now;
	unsigned long irqflags;
	u64 seq;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	spin_lock_irqsave(&dev->event_lock, irqflags);

	spin_lock(&dev->vbl_lock);
	DRM_DEBUG_VBL("crtc %d, vblank enabled %d, inmodeset %d\n",
		      pipe, vblank->enabled, vblank->inmodeset);

	/* Avoid redundant vblank disables without previous
	 * drm_crtc_vblank_on(). */
	if (drm_core_check_feature(dev, DRIVER_ATOMIC) || !vblank->inmodeset)
		drm_vblank_disable_and_save(dev, pipe);

	wake_up(&vblank->queue);

	/*
	 * Prevent subsequent drm_vblank_get() from re-enabling
	 * the vblank interrupt by bumping the refcount.
	 */
	if (!vblank->inmodeset) {
		atomic_inc(&vblank->refcount);
		vblank->inmodeset = 1;
	}
	spin_unlock(&dev->vbl_lock);

	/* Send any queued vblank events, lest the natives grow disquiet */
	seq = drm_vblank_count_and_time(dev, pipe, &now);

	list_for_each_entry_safe(e, t, &dev->vblank_event_list, base.link) {
		if (e->pipe != pipe)
			continue;
		DRM_DEBUG("Sending premature vblank event on disable: "
			  "wanted %llu, current %llu\n",
			  e->sequence, seq);
		list_del(&e->base.link);
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
	}
	spin_unlock_irqrestore(&dev->event_lock, irqflags);

	/* Will be reset by the modeset helpers when re-enabling the crtc by
	 * calling drm_calc_timestamping_constants(). */
	vblank->hwmode.crtc_clock = 0;
}
EXPORT_SYMBOL(drm_crtc_vblank_off);

/**
 * drm_crtc_vblank_reset - reset vblank state to off on a CRTC
 * @crtc: CRTC in question
 *
 * Drivers can use this function to reset the vblank state to off at load time.
 * Drivers should use this together with the drm_crtc_vblank_off() and
 * drm_crtc_vblank_on() functions. The difference compared to
 * drm_crtc_vblank_off() is that this function doesn't save the vblank counter
 * and hence doesn't need to call any driver hooks.
 *
 * This is useful for recovering driver state e.g. on driver load, or on resume.
 */
void drm_crtc_vblank_reset(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned long irqflags;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	/*
	 * Prevent subsequent drm_vblank_get() from enabling the vblank
	 * interrupt by bumping the refcount.
	 */
	if (!vblank->inmodeset) {
		atomic_inc(&vblank->refcount);
		vblank->inmodeset = 1;
	}
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

	WARN_ON(!list_empty(&dev->vblank_event_list));
}
EXPORT_SYMBOL(drm_crtc_vblank_reset);

/**
 * drm_crtc_set_max_vblank_count - configure the hw max vblank counter value
 * @crtc: CRTC in question
 * @max_vblank_count: max hardware vblank counter value
 *
 * Update the maximum hardware vblank counter value for @crtc
 * at runtime. Useful for hardware where the operation of the
 * hardware vblank counter depends on the currently active
 * display configuration.
 *
 * For example, if the hardware vblank counter does not work
 * when a specific connector is active the maximum can be set
 * to zero. And when that specific connector isn't active the
 * maximum can again be set to the appropriate non-zero value.
 *
 * If used, must be called before drm_vblank_on().
 */
void drm_crtc_set_max_vblank_count(struct drm_crtc *crtc,
				   u32 max_vblank_count)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	WARN_ON(dev->max_vblank_count);
	WARN_ON(!READ_ONCE(vblank->inmodeset));

	vblank->max_vblank_count = max_vblank_count;
}
EXPORT_SYMBOL(drm_crtc_set_max_vblank_count);

/**
 * drm_crtc_vblank_on - enable vblank events on a CRTC
 * @crtc: CRTC in question
 *
 * This functions restores the vblank interrupt state captured with
 * drm_crtc_vblank_off() again and is generally called when enabling @crtc. Note
 * that calls to drm_crtc_vblank_on() and drm_crtc_vblank_off() can be
 * unbalanced and so can also be unconditionally called in driver load code to
 * reflect the current hardware state of the crtc.
 */
void drm_crtc_vblank_on(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	unsigned long irqflags;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	spin_lock_irqsave(&dev->vbl_lock, irqflags);
	DRM_DEBUG_VBL("crtc %d, vblank enabled %d, inmodeset %d\n",
		      pipe, vblank->enabled, vblank->inmodeset);

	/* Drop our private "prevent drm_vblank_get" refcount */
	if (vblank->inmodeset) {
		atomic_dec(&vblank->refcount);
		vblank->inmodeset = 0;
	}

	drm_reset_vblank_timestamp(dev, pipe);

	/*
	 * re-enable interrupts if there are users left, or the
	 * user wishes vblank interrupts to be enabled all the time.
	 */
	if (atomic_read(&vblank->refcount) != 0 || drm_vblank_offdelay == 0)
		WARN_ON(drm_vblank_enable(dev, pipe));
	spin_unlock_irqrestore(&dev->vbl_lock, irqflags);
}
EXPORT_SYMBOL(drm_crtc_vblank_on);

/**
 * drm_vblank_restore - estimate missed vblanks and update vblank count.
 * @dev: DRM device
 * @pipe: CRTC index
 *
 * Power manamement features can cause frame counter resets between vblank
 * disable and enable. Drivers can use this function in their
 * &drm_crtc_funcs.enable_vblank implementation to estimate missed vblanks since
 * the last &drm_crtc_funcs.disable_vblank using timestamps and update the
 * vblank counter.
 *
 * This function is the legacy version of drm_crtc_vblank_restore().
 */
void drm_vblank_restore(struct drm_device *dev, unsigned int pipe)
{
	ktime_t t_vblank;
	struct drm_vblank_crtc *vblank;
	int framedur_ns;
	u64 diff_ns;
	u32 cur_vblank, diff = 1;
	int count = DRM_TIMESTAMP_MAXRETRIES;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	assert_spin_locked(&dev->vbl_lock);
	assert_spin_locked(&dev->vblank_time_lock);

	vblank = &dev->vblank[pipe];
	WARN_ONCE((drm_debug & DRM_UT_VBL) && !vblank->framedur_ns,
		  "Cannot compute missed vblanks without frame duration\n");
	framedur_ns = vblank->framedur_ns;

	do {
		cur_vblank = __get_vblank_counter(dev, pipe);
		drm_get_last_vbltimestamp(dev, pipe, &t_vblank, false);
	} while (cur_vblank != __get_vblank_counter(dev, pipe) && --count > 0);

	diff_ns = ktime_to_ns(ktime_sub(t_vblank, vblank->time));
	if (framedur_ns)
		diff = DIV_ROUND_CLOSEST_ULL(diff_ns, framedur_ns);


	DRM_DEBUG_VBL("missed %d vblanks in %lld ns, frame duration=%d ns, hw_diff=%d\n",
		      diff, diff_ns, framedur_ns, cur_vblank - vblank->last);
	store_vblank(dev, pipe, diff, t_vblank, cur_vblank);
}
EXPORT_SYMBOL(drm_vblank_restore);

/**
 * drm_crtc_vblank_restore - estimate missed vblanks and update vblank count.
 * @crtc: CRTC in question
 *
 * Power manamement features can cause frame counter resets between vblank
 * disable and enable. Drivers can use this function in their
 * &drm_crtc_funcs.enable_vblank implementation to estimate missed vblanks since
 * the last &drm_crtc_funcs.disable_vblank using timestamps and update the
 * vblank counter.
 */
void drm_crtc_vblank_restore(struct drm_crtc *crtc)
{
	drm_vblank_restore(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_vblank_restore);

static void drm_legacy_vblank_pre_modeset(struct drm_device *dev,
					  unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	/* vblank is not initialized (IRQ not installed ?), or has been freed */
	if (!dev->num_crtcs)
		return;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	/*
	 * To avoid all the problems that might happen if interrupts
	 * were enabled/disabled around or between these calls, we just
	 * have the kernel take a reference on the CRTC (just once though
	 * to avoid corrupting the count if multiple, mismatch calls occur),
	 * so that interrupts remain enabled in the interim.
	 */
	if (!vblank->inmodeset) {
		vblank->inmodeset = 0x1;
		if (drm_vblank_get(dev, pipe) == 0)
			vblank->inmodeset |= 0x2;
	}
}

static void drm_legacy_vblank_post_modeset(struct drm_device *dev,
					   unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	unsigned long irqflags;

	/* vblank is not initialized (IRQ not installed ?), or has been freed */
	if (!dev->num_crtcs)
		return;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return;

	if (vblank->inmodeset) {
		spin_lock_irqsave(&dev->vbl_lock, irqflags);
		drm_reset_vblank_timestamp(dev, pipe);
		spin_unlock_irqrestore(&dev->vbl_lock, irqflags);

		if (vblank->inmodeset & 0x2)
			drm_vblank_put(dev, pipe);

		vblank->inmodeset = 0;
	}
}

int drm_legacy_modeset_ctl_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_modeset_ctl *modeset = data;
	unsigned int pipe;

	/* If drm_vblank_init() hasn't been called yet, just no-op */
	if (!dev->num_crtcs)
		return 0;

	/* KMS drivers handle this internally */
	if (!drm_core_check_feature(dev, DRIVER_LEGACY))
		return 0;

	pipe = modeset->crtc;
	if (pipe >= dev->num_crtcs)
		return -EINVAL;

	switch (modeset->cmd) {
	case _DRM_PRE_MODESET:
		drm_legacy_vblank_pre_modeset(dev, pipe);
		break;
	case _DRM_POST_MODESET:
		drm_legacy_vblank_post_modeset(dev, pipe);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline bool vblank_passed(u64 seq, u64 ref)
{
	return (seq - ref) <= (1 << 23);
}

static int drm_queue_vblank_event(struct drm_device *dev, unsigned int pipe,
				  u64 req_seq,
				  union drm_wait_vblank *vblwait,
				  struct drm_file *file_priv)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct drm_pending_vblank_event *e;
	ktime_t now;
	unsigned long flags;
	u64 seq;
	int ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL) {
		ret = -ENOMEM;
		goto err_put;
	}

	e->pipe = pipe;
	e->event.base.type = DRM_EVENT_VBLANK;
	e->event.base.length = sizeof(e->event.vbl);
	e->event.vbl.user_data = vblwait->request.signal;
	e->event.vbl.crtc_id = 0;
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		struct drm_crtc *crtc = drm_crtc_from_index(dev, pipe);
		if (crtc)
			e->event.vbl.crtc_id = crtc->base.id;
	}

	spin_lock_irqsave(&dev->event_lock, flags);

	/*
	 * drm_crtc_vblank_off() might have been called after we called
	 * drm_vblank_get(). drm_crtc_vblank_off() holds event_lock around the
	 * vblank disable, so no need for further locking.  The reference from
	 * drm_vblank_get() protects against vblank disable from another source.
	 */
	if (!READ_ONCE(vblank->enabled)) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = drm_event_reserve_init_locked(dev, file_priv, &e->base,
					    &e->event.base);

	if (ret)
		goto err_unlock;

	seq = drm_vblank_count_and_time(dev, pipe, &now);

	DRM_DEBUG("event on vblank count %llu, current %llu, crtc %u\n",
		  req_seq, seq, pipe);

	trace_drm_vblank_event_queued(file_priv, pipe, req_seq);

	e->sequence = req_seq;
	if (vblank_passed(seq, req_seq)) {
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
		vblwait->reply.sequence = seq;
	} else {
		/* drm_handle_vblank_events will call drm_vblank_put */
		list_add_tail(&e->base.link, &dev->vblank_event_list);
		vblwait->reply.sequence = req_seq;
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);

	return 0;

err_unlock:
	spin_unlock_irqrestore(&dev->event_lock, flags);
	kfree(e);
err_put:
	drm_vblank_put(dev, pipe);
	return ret;
}

static bool drm_wait_vblank_is_query(union drm_wait_vblank *vblwait)
{
	if (vblwait->request.sequence)
		return false;

	return _DRM_VBLANK_RELATIVE ==
		(vblwait->request.type & (_DRM_VBLANK_TYPES_MASK |
					  _DRM_VBLANK_EVENT |
					  _DRM_VBLANK_NEXTONMISS));
}

/*
 * Widen a 32-bit param to 64-bits.
 *
 * \param narrow 32-bit value (missing upper 32 bits)
 * \param near 64-bit value that should be 'close' to near
 *
 * This function returns a 64-bit value using the lower 32-bits from
 * 'narrow' and constructing the upper 32-bits so that the result is
 * as close as possible to 'near'.
 */

static u64 widen_32_to_64(u32 narrow, u64 near)
{
	return near + (s32) (narrow - near);
}

static void drm_wait_vblank_reply(struct drm_device *dev, unsigned int pipe,
				  struct drm_wait_vblank_reply *reply)
{
	ktime_t now;
	struct timespec64 ts;

	/*
	 * drm_wait_vblank_reply is a UAPI structure that uses 'long'
	 * to store the seconds. This is safe as we always use monotonic
	 * timestamps since linux-4.15.
	 */
	reply->sequence = drm_vblank_count_and_time(dev, pipe, &now);
	ts = ktime_to_timespec64(now);
	reply->tval_sec = (u32)ts.tv_sec;
	reply->tval_usec = ts.tv_nsec / 1000;
}

int drm_wait_vblank_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	union drm_wait_vblank *vblwait = data;
	int ret;
	u64 req_seq, seq;
	unsigned int pipe_index;
	unsigned int flags, pipe, high_pipe;

	if (!dev->irq_enabled)
		return -EOPNOTSUPP;

	if (vblwait->request.type & _DRM_VBLANK_SIGNAL)
		return -EINVAL;

	if (vblwait->request.type &
	    ~(_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK |
	      _DRM_VBLANK_HIGH_CRTC_MASK)) {
		DRM_ERROR("Unsupported type value 0x%x, supported mask 0x%x\n",
			  vblwait->request.type,
			  (_DRM_VBLANK_TYPES_MASK | _DRM_VBLANK_FLAGS_MASK |
			   _DRM_VBLANK_HIGH_CRTC_MASK));
		return -EINVAL;
	}

	flags = vblwait->request.type & _DRM_VBLANK_FLAGS_MASK;
	high_pipe = (vblwait->request.type & _DRM_VBLANK_HIGH_CRTC_MASK);
	if (high_pipe)
		pipe_index = high_pipe >> _DRM_VBLANK_HIGH_CRTC_SHIFT;
	else
		pipe_index = flags & _DRM_VBLANK_SECONDARY ? 1 : 0;

	/* Convert lease-relative crtc index into global crtc index */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		pipe = 0;
		drm_for_each_crtc(crtc, dev) {
			if (drm_lease_held(file_priv, crtc->base.id)) {
				if (pipe_index == 0)
					break;
				pipe_index--;
			}
			pipe++;
		}
	} else {
		pipe = pipe_index;
	}

	if (pipe >= dev->num_crtcs)
		return -EINVAL;

	vblank = &dev->vblank[pipe];

	/* If the counter is currently enabled and accurate, short-circuit
	 * queries to return the cached timestamp of the last vblank.
	 */
	if (dev->vblank_disable_immediate &&
	    drm_wait_vblank_is_query(vblwait) &&
	    READ_ONCE(vblank->enabled)) {
		drm_wait_vblank_reply(dev, pipe, &vblwait->reply);
		return 0;
	}

	ret = drm_vblank_get(dev, pipe);
	if (ret) {
		DRM_DEBUG("crtc %d failed to acquire vblank counter, %d\n", pipe, ret);
		return ret;
	}
	seq = drm_vblank_count(dev, pipe);

	switch (vblwait->request.type & _DRM_VBLANK_TYPES_MASK) {
	case _DRM_VBLANK_RELATIVE:
		req_seq = seq + vblwait->request.sequence;
		vblwait->request.sequence = req_seq;
		vblwait->request.type &= ~_DRM_VBLANK_RELATIVE;
		break;
	case _DRM_VBLANK_ABSOLUTE:
		req_seq = widen_32_to_64(vblwait->request.sequence, seq);
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if ((flags & _DRM_VBLANK_NEXTONMISS) &&
	    vblank_passed(seq, req_seq)) {
		req_seq = seq + 1;
		vblwait->request.type &= ~_DRM_VBLANK_NEXTONMISS;
		vblwait->request.sequence = req_seq;
	}

	if (flags & _DRM_VBLANK_EVENT) {
		/* must hold on to the vblank ref until the event fires
		 * drm_vblank_put will be called asynchronously
		 */
		return drm_queue_vblank_event(dev, pipe, req_seq, vblwait, file_priv);
	}

	if (req_seq != seq) {
		DRM_DEBUG("waiting on vblank count %llu, crtc %u\n",
			  req_seq, pipe);
		DRM_WAIT_ON(ret, vblank->queue, 3 * HZ,
			    vblank_passed(drm_vblank_count(dev, pipe),
					  req_seq) ||
			    !READ_ONCE(vblank->enabled));
	}

	if (ret != -EINTR) {
		drm_wait_vblank_reply(dev, pipe, &vblwait->reply);

		DRM_DEBUG("crtc %d returning %u to client\n",
			  pipe, vblwait->reply.sequence);
	} else {
		DRM_DEBUG("crtc %d vblank wait interrupted by signal\n", pipe);
	}

done:
	drm_vblank_put(dev, pipe);
	return ret;
}

static void drm_handle_vblank_events(struct drm_device *dev, unsigned int pipe)
{
	struct drm_pending_vblank_event *e, *t;
	ktime_t now;
	u64 seq;

	assert_spin_locked(&dev->event_lock);

	seq = drm_vblank_count_and_time(dev, pipe, &now);

	list_for_each_entry_safe(e, t, &dev->vblank_event_list, base.link) {
		if (e->pipe != pipe)
			continue;
		if (!vblank_passed(seq, e->sequence))
			continue;

		DRM_DEBUG("vblank event on %llu, current %llu\n",
			  e->sequence, seq);

		list_del(&e->base.link);
		drm_vblank_put(dev, pipe);
		send_vblank_event(dev, e, seq, now);
	}

	trace_drm_vblank_event(pipe, seq);
}

/**
 * drm_handle_vblank - handle a vblank event
 * @dev: DRM device
 * @pipe: index of CRTC where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 *
 * This is the legacy version of drm_crtc_handle_vblank().
 */
bool drm_handle_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	unsigned long irqflags;
	bool disable_irq;

	if (WARN_ON_ONCE(!dev->num_crtcs))
		return false;

	if (WARN_ON(pipe >= dev->num_crtcs))
		return false;

	spin_lock_irqsave(&dev->event_lock, irqflags);

	/* Need timestamp lock to prevent concurrent execution with
	 * vblank enable/disable, as this would cause inconsistent
	 * or corrupted timestamps and vblank counts.
	 */
	spin_lock(&dev->vblank_time_lock);

	/* Vblank irq handling disabled. Nothing to do. */
	if (!vblank->enabled) {
		spin_unlock(&dev->vblank_time_lock);
		spin_unlock_irqrestore(&dev->event_lock, irqflags);
		return false;
	}

	drm_update_vblank_count(dev, pipe, true);

	spin_unlock(&dev->vblank_time_lock);

	wake_up(&vblank->queue);

	/* With instant-off, we defer disabling the interrupt until after
	 * we finish processing the following vblank after all events have
	 * been signaled. The disable has to be last (after
	 * drm_handle_vblank_events) so that the timestamp is always accurate.
	 */
	disable_irq = (dev->vblank_disable_immediate &&
		       drm_vblank_offdelay > 0 &&
		       !atomic_read(&vblank->refcount));

	drm_handle_vblank_events(dev, pipe);

	spin_unlock_irqrestore(&dev->event_lock, irqflags);

	if (disable_irq)
		vblank_disable_fn(&vblank->disable_timer);

	return true;
}
EXPORT_SYMBOL(drm_handle_vblank);

/**
 * drm_crtc_handle_vblank - handle a vblank event
 * @crtc: where this event occurred
 *
 * Drivers should call this routine in their vblank interrupt handlers to
 * update the vblank counter and send any signals that may be pending.
 *
 * This is the native KMS version of drm_handle_vblank().
 *
 * Returns:
 * True if the event was successfully handled, false on failure.
 */
bool drm_crtc_handle_vblank(struct drm_crtc *crtc)
{
	return drm_handle_vblank(crtc->dev, drm_crtc_index(crtc));
}
EXPORT_SYMBOL(drm_crtc_handle_vblank);

/*
 * Get crtc VBLANK count.
 *
 * \param dev DRM device
 * \param data user arguement, pointing to a drm_crtc_get_sequence structure.
 * \param file_priv drm file private for the user's open file descriptor
 */

int drm_crtc_get_sequence_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	int pipe;
	struct drm_crtc_get_sequence *get_seq = data;
	ktime_t now;
	bool vblank_enabled;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	if (!dev->irq_enabled)
		return -EOPNOTSUPP;

	crtc = drm_crtc_find(dev, file_priv, get_seq->crtc_id);
	if (!crtc)
		return -ENOENT;

	pipe = drm_crtc_index(crtc);

	vblank = &dev->vblank[pipe];
	vblank_enabled = dev->vblank_disable_immediate && READ_ONCE(vblank->enabled);

	if (!vblank_enabled) {
		ret = drm_crtc_vblank_get(crtc);
		if (ret) {
			DRM_DEBUG("crtc %d failed to acquire vblank counter, %d\n", pipe, ret);
			return ret;
		}
	}
	drm_modeset_lock(&crtc->mutex, NULL);
	if (crtc->state)
		get_seq->active = crtc->state->enable;
	else
		get_seq->active = crtc->enabled;
	drm_modeset_unlock(&crtc->mutex);
	get_seq->sequence = drm_vblank_count_and_time(dev, pipe, &now);
	get_seq->sequence_ns = ktime_to_ns(now);
	if (!vblank_enabled)
		drm_crtc_vblank_put(crtc);
	return 0;
}

/*
 * Queue a event for VBLANK sequence
 *
 * \param dev DRM device
 * \param data user arguement, pointing to a drm_crtc_queue_sequence structure.
 * \param file_priv drm file private for the user's open file descriptor
 */

int drm_crtc_queue_sequence_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct drm_crtc *crtc;
	struct drm_vblank_crtc *vblank;
	int pipe;
	struct drm_crtc_queue_sequence *queue_seq = data;
	ktime_t now;
	struct drm_pending_vblank_event *e;
	u32 flags;
	u64 seq;
	u64 req_seq;
	int ret;
	unsigned long spin_flags;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	if (!dev->irq_enabled)
		return -EOPNOTSUPP;

	crtc = drm_crtc_find(dev, file_priv, queue_seq->crtc_id);
	if (!crtc)
		return -ENOENT;

	flags = queue_seq->flags;
	/* Check valid flag bits */
	if (flags & ~(DRM_CRTC_SEQUENCE_RELATIVE|
		      DRM_CRTC_SEQUENCE_NEXT_ON_MISS))
		return -EINVAL;

	pipe = drm_crtc_index(crtc);

	vblank = &dev->vblank[pipe];

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (e == NULL)
		return -ENOMEM;

	ret = drm_crtc_vblank_get(crtc);
	if (ret) {
		DRM_DEBUG("crtc %d failed to acquire vblank counter, %d\n", pipe, ret);
		goto err_free;
	}

	seq = drm_vblank_count_and_time(dev, pipe, &now);
	req_seq = queue_seq->sequence;

	if (flags & DRM_CRTC_SEQUENCE_RELATIVE)
		req_seq += seq;

	if ((flags & DRM_CRTC_SEQUENCE_NEXT_ON_MISS) && vblank_passed(seq, req_seq))
		req_seq = seq + 1;

	e->pipe = pipe;
	e->event.base.type = DRM_EVENT_CRTC_SEQUENCE;
	e->event.base.length = sizeof(e->event.seq);
	e->event.seq.user_data = queue_seq->user_data;

	spin_lock_irqsave(&dev->event_lock, spin_flags);

	/*
	 * drm_crtc_vblank_off() might have been called after we called
	 * drm_crtc_vblank_get(). drm_crtc_vblank_off() holds event_lock around the
	 * vblank disable, so no need for further locking.  The reference from
	 * drm_crtc_vblank_get() protects against vblank disable from another source.
	 */
	if (!READ_ONCE(vblank->enabled)) {
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = drm_event_reserve_init_locked(dev, file_priv, &e->base,
					    &e->event.base);

	if (ret)
		goto err_unlock;

	e->sequence = req_seq;

	if (vblank_passed(seq, req_seq)) {
		drm_crtc_vblank_put(crtc);
		send_vblank_event(dev, e, seq, now);
		queue_seq->sequence = seq;
	} else {
		/* drm_handle_vblank_events will call drm_vblank_put */
		list_add_tail(&e->base.link, &dev->vblank_event_list);
		queue_seq->sequence = req_seq;
	}

	spin_unlock_irqrestore(&dev->event_lock, spin_flags);
	return 0;

err_unlock:
	spin_unlock_irqrestore(&dev->event_lock, spin_flags);
	drm_crtc_vblank_put(crtc);
err_free:
	kfree(e);
	return ret;
}
