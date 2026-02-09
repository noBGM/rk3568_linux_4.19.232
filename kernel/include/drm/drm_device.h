#ifndef _DRM_DEVICE_H_
#define _DRM_DEVICE_H_

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include <drm/drm_hashtab.h>
#include <drm/drm_mode_config.h>

struct drm_driver;
struct drm_minor;
struct drm_master;
struct drm_device_dma;
struct drm_vblank_crtc;
struct drm_sg_mem;
struct drm_local_map;
struct drm_vma_offset_manager;
struct drm_fb_helper;

struct inode;

struct pci_dev;
struct pci_controller;

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	struct list_head legacy_dev_list;/**< list of devices per driver for stealth attach cleanup */
	int if_version;			/**< Highest interface version set */

	/** \name Lifetime Management */
	/*@{ */
	struct kref ref;		/**< Object ref-count */
	struct device *dev;		/**< Device structure of bus-device */
	struct drm_driver *driver;	/**< DRM driver managing the device */
	void *dev_private;		/**< DRM driver private data */
	struct drm_minor *primary;		/**< Primary node */
	struct drm_minor *render;		/**< Render node */
	bool registered;

	/* currently active master for this device. Protected by master_mutex */
	struct drm_master *master;

	/**
	 * @unplugged:
	 *
	 * Flag to tell if the device has been unplugged.
	 * See drm_dev_enter() and drm_dev_is_unplugged().
	 */
	bool unplugged;

	struct inode *anon_inode;		/**< inode for private address-space */
	char *unique;				/**< unique name of the device */
	/*@} */

	/** \name Locks */
	/*@{ */
	struct mutex struct_mutex;	/**< For others */
	struct mutex master_mutex;      /**< For drm_minor::master and drm_file::is_master */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open, protected by drm_global_mutex. */
	spinlock_t buf_lock;		/**< For drm_device::buf_use and a few other things. */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	struct mutex filelist_mutex;
	struct list_head filelist;

	/**
	 * @filelist_internal:
	 *
	 * List of open DRM files for in-kernel clients. Protected by @filelist_mutex.
	 */
	struct list_head filelist_internal;

	/**
	 * @clientlist_mutex:
	 *
	 * Protects @clientlist access.
	 */
	struct mutex clientlist_mutex;

	/**
	 * @clientlist:
	 *
	 * List of in-kernel clients. Protected by @clientlist_mutex.
	 */
	struct list_head clientlist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	struct list_head vmalist;	/**< List of vmas (for debugging) */

	/*@} */

	/** \name DMA support */
	/*@{ */
	struct drm_device_dma *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */

	__volatile__ long context_flag;	/**< Context swapping flag */
	int last_context;		/**< Last current context */
	/*@} */

	/**
	 * @irq_enabled:
	 *
	 * Indicates that interrupt handling is enabled, specifically vblank
	 * handling. Drivers which don't use drm_irq_install() need to set this
	 * to true manually.
	 */
	bool irq_enabled;
	int irq;

	/**
	 * @vblank_disable_immediate: VBlank 中断立即禁用标志
	 *
	 * 当引用计数降为 0 时，是否立即禁用 vblank 中断。
	 * - true: 引用计数为 0 时立即禁用中断
	 * - false: 通过禁用定时器延迟禁用（节省开关中断的开销）
	 *
	 * 满足以下条件时可设为 true：
	 * 1. 硬件有可用的 vblank 计数器且支持高精度时间戳
	 * 2. 驱动正确使用 drm_crtc_vblank_on() 和 drm_crtc_vblank_off()
	 *
	 * 参考: @max_vblank_count 和 &drm_crtc_funcs.get_vblank_counter
	 */
	bool vblank_disable_immediate;

	/**
	 * @vblank: VBlank 跟踪结构体数组
	 *
	 * 每个 CRTC 对应一个 vblank 跟踪结构体。由于历史原因（vblank 支持
	 * 早于内核模式设置），这是独立的数组而不是 &struct drm_crtc 的一部分。
	 * 必须通过 drm_vblank_init() 显式初始化。
	 *
	 * VBlank（垂直消隐）：显示器刷新帧之间的间隔时间，用于同步显示更新
	 */
	struct drm_vblank_crtc *vblank;

	/**
	 * vblank_time_lock: VBlank 时间保护锁
	 * 保护 vblank 计数和时间更新操作，防止使能/禁用期间的并发访问
	 */
	spinlock_t vblank_time_lock;
	spinlock_t vbl_lock;  /* VBlank 通用保护锁 */

	/**
	 * @max_vblank_count: VBlank 计数器寄存器最大值
	 *
	 * 硬件 vblank 寄存器的最大值。该值 +1 会导致寄存器回绕（溢出归零），
	 * vblank 核心使用此值处理回绕情况。
	 *
	 * 取值说明：
	 * - 0: vblank 核心通过高精度时间戳推算经过的 vblank 数（存在竞态和误差，
	 *      长时间运行精度会下降，不推荐）
	 * - 非 0: 使用硬件 vblank 计数器（推荐），此时必须设置
	 *         &drm_crtc_funcs.get_vblank_counter
	 *
	 * This is the statically configured device wide maximum. The driver
	 * can instead choose to use a runtime configurable per-crtc value
	 * &drm_vblank_crtc.max_vblank_count, in which case @max_vblank_count
	 * must be left at zero. See drm_crtc_set_max_vblank_count() on how
	 * to use the per-crtc value.
	 *
	 * If non-zero, &drm_crtc_funcs.get_vblank_counter must be set.
	 */
	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * vblank_event_list: VBlank 事件链表
	 * 存储所有待处理的 vblank 事件（如页面翻转完成通知等）
	 */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/*@} */

	struct drm_agp_head *agp;	/**< AGP data */

	struct pci_dev *pdev;		/**< PCI device structure */
#ifdef __alpha__
	struct pci_controller *hose;
#endif

	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	unsigned int num_crtcs;                  /**< Number of CRTCs on this device */

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	struct mutex object_name_lock;
	struct idr object_name_idr;
	struct drm_vma_offset_manager *vma_offset_manager;
	/*@} */
	int switch_power_state;

	/**
	 * @fb_helper:
	 *
	 * Pointer to the fbdev emulation structure.
	 * Set by drm_fb_helper_init() and cleared by drm_fb_helper_fini().
	 */
	struct drm_fb_helper *fb_helper;
};

#endif
