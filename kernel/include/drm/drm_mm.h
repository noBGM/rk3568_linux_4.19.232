/**************************************************************************
 *
 * Copyright 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX. USA.
 * Copyright 2016 Intel Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/
/*
 * Authors:
 * Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#ifndef _DRM_MM_H_
#define _DRM_MM_H_

/*
 * Generic range manager structs
 */
#include <linux/bug.h>
#include <linux/rbtree.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#ifdef CONFIG_DRM_DEBUG_MM
#include <linux/stackdepot.h>
#endif
#include <drm/drm_print.h>

#ifdef CONFIG_DRM_DEBUG_MM
#define DRM_MM_BUG_ON(expr) BUG_ON(expr)
#else
#define DRM_MM_BUG_ON(expr) BUILD_BUG_ON_INVALID(expr)
#endif

/**
 * enum drm_mm_insert_mode - control search and allocation behaviour
 *
 * The &struct drm_mm range manager supports finding a suitable modes using
 * a number of search trees. These trees are oranised by size, by address and
 * in most recent eviction order. This allows the user to find either the
 * smallest hole to reuse, the lowest or highest address to reuse, or simply
 * reuse the most recent eviction that fits. When allocating the &drm_mm_node
 * from within the hole, the &drm_mm_insert_mode also dictate whether to
 * allocate the lowest matching address or the highest.
 */
enum drm_mm_insert_mode {
	/**
	 * @DRM_MM_INSERT_BEST:
	 *
	 * Search for the smallest hole (within the search range) that fits
	 * the desired node.
	 *
	 * Allocates the node from the bottom of the found hole.
	 */
	DRM_MM_INSERT_BEST = 0,

	/**
	 * @DRM_MM_INSERT_LOW:
	 *
	 * Search for the lowest hole (address closest to 0, within the search
	 * range) that fits the desired node.
	 *
	 * Allocates the node from the bottom of the found hole.
	 */
	DRM_MM_INSERT_LOW,

	/**
	 * @DRM_MM_INSERT_HIGH:
	 *
	 * Search for the highest hole (address closest to U64_MAX, within the
	 * search range) that fits the desired node.
	 *
	 * Allocates the node from the *top* of the found hole. The specified
	 * alignment for the node is applied to the base of the node
	 * (&drm_mm_node.start).
	 */
	DRM_MM_INSERT_HIGH,

	/**
	 * @DRM_MM_INSERT_EVICT:
	 *
	 * Search for the most recently evicted hole (within the search range)
	 * that fits the desired node. This is appropriate for use immediately
	 * after performing an eviction scan (see drm_mm_scan_init()) and
	 * removing the selected nodes to form a hole.
	 *
	 * Allocates the node from the bottom of the found hole.
	 */
	DRM_MM_INSERT_EVICT,

	/**
	 * @DRM_MM_INSERT_ONCE:
	 *
	 * Only check the first hole for suitablity and report -ENOSPC
	 * immediately otherwise, rather than check every hole until a
	 * suitable one is found. Can only be used in conjunction with another
	 * search method such as DRM_MM_INSERT_HIGH or DRM_MM_INSERT_LOW.
	 */
	DRM_MM_INSERT_ONCE = BIT(31),

	/**
	 * @DRM_MM_INSERT_HIGHEST:
	 *
	 * Only check the highest hole (the hole with the largest address) and
	 * insert the node at the top of the hole or report -ENOSPC if
	 * unsuitable.
	 *
	 * Does not search all holes.
	 */
	DRM_MM_INSERT_HIGHEST = DRM_MM_INSERT_HIGH | DRM_MM_INSERT_ONCE,

	/**
	 * @DRM_MM_INSERT_LOWEST:
	 *
	 * Only check the lowest hole (the hole with the smallest address) and
	 * insert the node at the bottom of the hole or report -ENOSPC if
	 * unsuitable.
	 *
	 * Does not search all holes.
	 */
	DRM_MM_INSERT_LOWEST  = DRM_MM_INSERT_LOW | DRM_MM_INSERT_ONCE,
};

/**
 * struct drm_mm_node - allocated block in the DRM allocator
 *
 * This represents an allocated block in a &drm_mm allocator. Except for
 * pre-reserved nodes inserted using drm_mm_reserve_node() the structure is
 * entirely opaque and should only be accessed through the provided funcions.
 * Since allocation of these nodes is entirely handled by the driver they can be
 * embedded.
 */
struct drm_mm_node {
	/** @color: Opaque driver-private tag. */
	unsigned long color;
	/** @start: Start address of the allocated block. */
	u64 start;
	/** @size: Size of the allocated block. */
	u64 size;
	/* private: */
	struct drm_mm *mm;
	struct list_head node_list;
	struct list_head hole_stack;
	struct rb_node rb;
	struct rb_node rb_hole_size;
	struct rb_node rb_hole_addr;
	u64 __subtree_last;
	u64 hole_size;
	bool allocated : 1;
	bool scanned_block : 1;
#ifdef CONFIG_DRM_DEBUG_MM
	depot_stack_handle_t stack;
#endif
};

/**
 * struct drm_mm - DRM内存管理器核心结构体
 *
 * 专用于GPU内存管理的地址范围分配器，提供内存块的分配/释放、区间查找等功能，
 * 针对GPU内存的特殊需求（如物理地址连续、按颜色/大小分类、插入保护页等）做了优化。
 * 除@color_adjust回调外，其余成员均为私有（opaque），仅能通过DRM提供的API访问，
 * 该结构体可嵌入到驱动的更大结构体中使用。
 */

 /*
 总结
struct drm_mm 是 DRM 子系统GPU 内存分配的核心管理器，通过链表 + 红黑树（区间树）实现高效的内存块分配 / 释放；
核心设计亮点：
hole_stack/holes_size/holes_addr 多维度索引空闲空洞，兼顾分配效率；
interval_tree 实现地址快速查找，适配 GPU 内存地址管理需求；
color_adjust 回调允许驱动自定义内存分配规则（如保护页、对齐）；
私有成员（hole_stack/head_node等）禁止驱动直接修改，必须通过 DRM 提供的 API（如 drm_mm_alloc ()、drm_mm_free ()）操作，避免破坏内存结构。
 */
struct drm_mm {
	/**
	 * @color_adjust: 内存空洞（hole）限制调整的驱动回调函数
	 *
	 * 可选回调，驱动可通过此函数对内存空洞施加额外限制。分配内存块时，
	 * node参数指向待分配空洞对应的drm_mm_node节点（可通过drm_mm_hole_follows()等函数获取），
	 * color为内存块的"颜色"标识（用于分类管理内存，如不同缓存属性/权限），
	 * start/end为待分配块的起始/结束地址指针，驱动可修改这两个值（如插入保护页、
	 * 调整对齐方式），实现自定义的内存分配规则。
	 *
	 * 函数参数说明：
	 * @node: 待分配空洞对应的节点
	 * @color: 待分配内存块的颜色标识
	 * @start: 待分配块起始地址（入参为候选值，驱动可修改）
	 * @end: 待分配块结束地址（入参为候选值，驱动可修改）
	 */
	void (*color_adjust)(const struct drm_mm_node *node,
			     unsigned long color,
			     u64 *start, u64 *end);

	/* private: */ // 以下均为私有成员，禁止驱动直接访问
	/**
	 * @hole_stack: 内存空洞前驱节点链表头
	 *
	 * 存储所有"紧邻内存空洞"的内存节点链表，每个节点都指向一个空闲空洞的起始位置，
	 * 用于快速定位可分配的内存空洞，提升分配效率。
	 * 注释原文：List of all memory nodes that immediately precede a free hole.
	 */
	struct list_head hole_stack;

	/**
	 * @head_node: 内存节点链表的根节点
	 *
	 * head_node.node_list 是所有内存节点（包括已分配/空闲）的链表头，
	 * 链表节点按内存起始地址**递增**排序，是遍历所有内存节点的入口。
	 * 注释原文：head_node.node_list is the list of all memory nodes, ordered
	 * according to the (increasing) start address of the memory node.
	 */
	struct drm_mm_node head_node;

	/**
	 * @interval_tree: 按地址索引的区间树（缓存版）
	 *
	 * 基于红黑树实现的区间树，用于通过内存地址**快速查找**对应的drm_mm_node节点，
	 * 相比遍历链表，区间树可将查找复杂度从O(n)降至O(log n)，核心用于地址冲突检测、
	 * 节点定位等场景。
	 * 注释原文：Keep an interval_tree for fast lookup of drm_mm_nodes by address.
	 */
	struct rb_root_cached interval_tree;

	/**
	 * @holes_size: 按大小索引的空闲空洞红黑树（缓存版）
	 *
	 * 所有空闲内存空洞按"大小"排序的红黑树（缓存版），用于快速查找符合大小要求的空洞，
	 * 是内存分配时"按大小匹配"的核心数据结构。
	 */
	struct rb_root_cached holes_size;

	/**
	 * @holes_addr: 按地址索引的空闲空洞红黑树
	 *
	 * 所有空闲内存空洞按"起始地址"排序的红黑树，用于快速查找指定地址范围内的空洞，
	 * 补充holes_size的功能，满足"按地址分配"的场景需求。
	 */
	struct rb_root holes_addr;

	/**
	 * @scan_active: 内存扫描激活标记
	 *
	 * 用于标记当前是否正在进行内存节点/空洞的扫描操作（如遍历所有空洞查找匹配项），
	 * 防止扫描过程中内存结构被并发修改，保证操作原子性。
	 */
	unsigned long scan_active;
};

/**
 * struct drm_mm_scan - DRM allocator eviction roaster data
 *
 * This structure tracks data needed for the eviction roaster set up using
 * drm_mm_scan_init(), and used with drm_mm_scan_add_block() and
 * drm_mm_scan_remove_block(). The structure is entirely opaque and should only
 * be accessed through the provided functions and macros. It is meant to be
 * allocated temporarily by the driver on the stack.
 */
struct drm_mm_scan {
	/* private: */
	struct drm_mm *mm;

	u64 size;
	u64 alignment;
	u64 remainder_mask;

	u64 range_start;
	u64 range_end;

	u64 hit_start;
	u64 hit_end;

	unsigned long color;
	enum drm_mm_insert_mode mode;
};

/**
 * drm_mm_node_allocated - checks whether a node is allocated
 * @node: drm_mm_node to check
 *
 * Drivers are required to clear a node prior to using it with the
 * drm_mm range manager.
 *
 * Drivers should use this helper for proper encapsulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @node is allocated.
 */
static inline bool drm_mm_node_allocated(const struct drm_mm_node *node)
{
	return node->allocated;
}

/**
 * drm_mm_initialized - checks whether an allocator is initialized
 * @mm: drm_mm to check
 *
 * Drivers should clear the struct drm_mm prior to initialisation if they
 * want to use this function.
 *
 * Drivers should use this helper for proper encapsulation of drm_mm
 * internals.
 *
 * Returns:
 * True if the @mm is initialized.
 */
static inline bool drm_mm_initialized(const struct drm_mm *mm)
{
	return mm->hole_stack.next;
}

/**
 * drm_mm_hole_follows - checks whether a hole follows this node
 * @node: drm_mm_node to check
 *
 * Holes are embedded into the drm_mm using the tail of a drm_mm_node.
 * If you wish to know whether a hole follows this particular node,
 * query this function. See also drm_mm_hole_node_start() and
 * drm_mm_hole_node_end().
 *
 * Returns:
 * True if a hole follows the @node.
 */
static inline bool drm_mm_hole_follows(const struct drm_mm_node *node)
{
	return node->hole_size;
}

static inline u64 __drm_mm_hole_node_start(const struct drm_mm_node *hole_node)
{
	return hole_node->start + hole_node->size;
}

/**
 * drm_mm_hole_node_start - computes the start of the hole following @node
 * @hole_node: drm_mm_node which implicitly tracks the following hole
 *
 * This is useful for driver-specific debug dumpers. Otherwise drivers should
 * not inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at drm_mm_hole_follows()
 *
 * Returns:
 * Start of the subsequent hole.
 */
static inline u64 drm_mm_hole_node_start(const struct drm_mm_node *hole_node)
{
	DRM_MM_BUG_ON(!drm_mm_hole_follows(hole_node));
	return __drm_mm_hole_node_start(hole_node);
}

static inline u64 __drm_mm_hole_node_end(const struct drm_mm_node *hole_node)
{
	return list_next_entry(hole_node, node_list)->start;
}

/**
 * drm_mm_hole_node_end - computes the end of the hole following @node
 * @hole_node: drm_mm_node which implicitly tracks the following hole
 *
 * This is useful for driver-specific debug dumpers. Otherwise drivers should
 * not inspect holes themselves. Drivers must check first whether a hole indeed
 * follows by looking at drm_mm_hole_follows().
 *
 * Returns:
 * End of the subsequent hole.
 */
static inline u64 drm_mm_hole_node_end(const struct drm_mm_node *hole_node)
{
	return __drm_mm_hole_node_end(hole_node);
}

/**
 * drm_mm_nodes - list of nodes under the drm_mm range manager
 * @mm: the struct drm_mm range manger
 *
 * As the drm_mm range manager hides its node_list deep with its
 * structure, extracting it looks painful and repetitive. This is
 * not expected to be used outside of the drm_mm_for_each_node()
 * macros and similar internal functions.
 *
 * Returns:
 * The node list, may be empty.
 */
#define drm_mm_nodes(mm) (&(mm)->head_node.node_list)

/**
 * drm_mm_for_each_node - iterator to walk over all allocated nodes
 * @entry: &struct drm_mm_node to assign to in each iteration step
 * @mm: &drm_mm allocator to walk
 *
 * This iterator walks over all nodes in the range allocator. It is implemented
 * with list_for_each(), so not save against removal of elements.
 */
#define drm_mm_for_each_node(entry, mm) \
	list_for_each_entry(entry, drm_mm_nodes(mm), node_list)

/**
 * drm_mm_for_each_node_safe - iterator to walk over all allocated nodes
 * @entry: &struct drm_mm_node to assign to in each iteration step
 * @next: &struct drm_mm_node to store the next step
 * @mm: &drm_mm allocator to walk
 *
 * This iterator walks over all nodes in the range allocator. It is implemented
 * with list_for_each_safe(), so save against removal of elements.
 */
#define drm_mm_for_each_node_safe(entry, next, mm) \
	list_for_each_entry_safe(entry, next, drm_mm_nodes(mm), node_list)

/**
 * drm_mm_for_each_hole - iterator to walk over all holes
 * @pos: &drm_mm_node used internally to track progress
 * @mm: &drm_mm allocator to walk
 * @hole_start: ulong variable to assign the hole start to on each iteration
 * @hole_end: ulong variable to assign the hole end to on each iteration
 *
 * This iterator walks over all holes in the range allocator. It is implemented
 * with list_for_each(), so not save against removal of elements. @entry is used
 * internally and will not reflect a real drm_mm_node for the very first hole.
 * Hence users of this iterator may not access it.
 *
 * Implementation Note:
 * We need to inline list_for_each_entry in order to be able to set hole_start
 * and hole_end on each iteration while keeping the macro sane.
 */
#define drm_mm_for_each_hole(pos, mm, hole_start, hole_end) \
	for (pos = list_first_entry(&(mm)->hole_stack, \
				    typeof(*pos), hole_stack); \
	     &pos->hole_stack != &(mm)->hole_stack ? \
	     hole_start = drm_mm_hole_node_start(pos), \
	     hole_end = hole_start + pos->hole_size, \
	     1 : 0; \
	     pos = list_next_entry(pos, hole_stack))

/*
 * Basic range manager support (drm_mm.c)
 */
int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node);
int drm_mm_insert_node_in_range(struct drm_mm *mm,
				struct drm_mm_node *node,
				u64 size,
				u64 alignment,
				unsigned long color,
				u64 start,
				u64 end,
				enum drm_mm_insert_mode mode);

/**
 * drm_mm_insert_node_generic - search for space and insert @node
 * @mm: drm_mm to allocate from
 * @node: preallocate node to insert
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for this node
 * @mode: fine-tune the allocation search and placement
 *
 * This is a simplified version of drm_mm_insert_node_in_range() with no
 * range restrictions applied.
 *
 * The preallocated node must be cleared to 0.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's no suitable hole.
 */
static inline int
drm_mm_insert_node_generic(struct drm_mm *mm, struct drm_mm_node *node,
			   u64 size, u64 alignment,
			   unsigned long color,
			   enum drm_mm_insert_mode mode)
{
	return drm_mm_insert_node_in_range(mm, node,
					   size, alignment, color,
					   0, U64_MAX, mode);
}

/**
 * drm_mm_insert_node - search for space and insert @node
 * @mm: drm_mm to allocate from
 * @node: preallocate node to insert
 * @size: size of the allocation
 *
 * This is a simplified version of drm_mm_insert_node_generic() with @color set
 * to 0.
 *
 * The preallocated node must be cleared to 0.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's no suitable hole.
 */
static inline int drm_mm_insert_node(struct drm_mm *mm,
				     struct drm_mm_node *node,
				     u64 size)
{
	return drm_mm_insert_node_generic(mm, node, size, 0, 0, 0);
}

void drm_mm_remove_node(struct drm_mm_node *node);
void drm_mm_replace_node(struct drm_mm_node *old, struct drm_mm_node *new);
void drm_mm_init(struct drm_mm *mm, u64 start, u64 size);
void drm_mm_takedown(struct drm_mm *mm);

/**
 * drm_mm_clean - checks whether an allocator is clean
 * @mm: drm_mm allocator to check
 *
 * Returns:
 * True if the allocator is completely free, false if there's still a node
 * allocated in it.
 */
static inline bool drm_mm_clean(const struct drm_mm *mm)
{
	return list_empty(drm_mm_nodes(mm));
}

struct drm_mm_node *
__drm_mm_interval_first(const struct drm_mm *mm, u64 start, u64 last);

/**
 * drm_mm_for_each_node_in_range - iterator to walk over a range of
 * allocated nodes
 * @node__: drm_mm_node structure to assign to in each iteration step
 * @mm__: drm_mm allocator to walk
 * @start__: starting offset, the first node will overlap this
 * @end__: ending offset, the last node will start before this (but may overlap)
 *
 * This iterator walks over all nodes in the range allocator that lie
 * between @start and @end. It is implemented similarly to list_for_each(),
 * but using the internal interval tree to accelerate the search for the
 * starting node, and so not safe against removal of elements. It assumes
 * that @end is within (or is the upper limit of) the drm_mm allocator.
 * If [@start, @end] are beyond the range of the drm_mm, the iterator may walk
 * over the special _unallocated_ &drm_mm.head_node, and may even continue
 * indefinitely.
 */
#define drm_mm_for_each_node_in_range(node__, mm__, start__, end__)	\
	for (node__ = __drm_mm_interval_first((mm__), (start__), (end__)-1); \
	     node__->start < (end__);					\
	     node__ = list_next_entry(node__, node_list))

void drm_mm_scan_init_with_range(struct drm_mm_scan *scan,
				 struct drm_mm *mm,
				 u64 size, u64 alignment, unsigned long color,
				 u64 start, u64 end,
				 enum drm_mm_insert_mode mode);

/**
 * drm_mm_scan_init - initialize lru scanning
 * @scan: scan state
 * @mm: drm_mm to scan
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for the allocation
 * @mode: fine-tune the allocation search and placement
 *
 * This is a simplified version of drm_mm_scan_init_with_range() with no range
 * restrictions applied.
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole.
 *
 * Warning:
 * As long as the scan list is non-empty, no other operations than
 * adding/removing nodes to/from the scan list are allowed.
 */
static inline void drm_mm_scan_init(struct drm_mm_scan *scan,
				    struct drm_mm *mm,
				    u64 size,
				    u64 alignment,
				    unsigned long color,
				    enum drm_mm_insert_mode mode)
{
	drm_mm_scan_init_with_range(scan, mm,
				    size, alignment, color,
				    0, U64_MAX, mode);
}

bool drm_mm_scan_add_block(struct drm_mm_scan *scan,
			   struct drm_mm_node *node);
bool drm_mm_scan_remove_block(struct drm_mm_scan *scan,
			      struct drm_mm_node *node);
struct drm_mm_node *drm_mm_scan_color_evict(struct drm_mm_scan *scan);

void drm_mm_print(const struct drm_mm *mm, struct drm_printer *p);

#endif
