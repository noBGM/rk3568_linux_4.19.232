// SPDX-License-Identifier: GPL-2.0
/*
 * Componentized device handling.
 *
 * This is work in progress.  We gather up the component devices into a list,
 * and bind them when instructed.  At the moment, we're specific to the DRM
 * subsystem, and only handles one master device, but this doesn't have to be
 * the case.
 */
#include <linux/component.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

struct component;

/*
 * component 框架的四个核心数据结构
 *
 * ## 整体关系图
 *
 *  全局链表 masters ──────────────────────────────────────────┐
 *                                                             │
 *  struct master  (e.g. Rockchip DRM display-subsystem)      │
 *    ├── dev      → platform_device（master 硬件节点）        │
 *    ├── ops      → { .bind, .unbind }（聚合/解聚回调）       │
 *    ├── match ──→ struct component_match                     │
 *    │               ├── alloc  （已分配槽位数）              │
 *    │               ├── num    （有效规则数）                 │
 *    │               └── compare[] ──→ struct component_match_array[]
 *    │                                   ├── [0] compare_dev, data=VOP2设备
 *    │                                   │       └── component ──┐
 *    │                                   ├── [1] compare_dev, data=DSI0设备
 *    │                                   │       └── component ──┤
 *    │                                   └── [2] compare_dev, data=HDMI设备
 *    │                                           └── component ──┤
 *    └── bound    （是否已完成聚合绑定）                        │
 *                                                             │
 *  全局链表 component_list ─────────────────────────────────┐ │
 *                                                           │ │
 *  struct component  (e.g. VOP2 vop2@fe040000)             │ │
 *    ├── dev     → platform_device（子组件硬件节点）        │ │
 *    ├── ops     → { .bind, .unbind }（子组件 bind 回调）   │ │
 *    ├── master ←────────────────────────────────────────────┘ │
 *    │            反向引用归属的 master（匹配成功后赋值）       │
 *    └── bound   （子组件本身是否已执行 bind）                  │
 *         ↑                                                     │
 *         └──── match_array[i].component ←─────────────────────┘
 *               match 规则与具体 component 的双向绑定
 *
 * ## 匹配与绑定流程
 *
 *   ① 子驱动 probe 完成 → component_add(dev, ops)
 *      → 在 component_list 追加一个 struct component 节点
 *      → 遍历 masters 链表，为每个 master 调用 find_components()
 *
 *   ② find_components() 遍历 master->match->compare[] 数组，
 *      对每条 component 为 NULL 的规则，在 component_list 中搜索：
 *      调用规则的 compare(comp->dev, mc->data) 回调
 *      → 匹配成功：mc->component = comp，comp->master = master
 *
 *   ③ 所有规则的 component 字段均非 NULL
 *      → 调用 master->ops->bind()，完成整体聚合初始化
 *      → master->bound = true，各子组件 bound = true
 */

/**
 * struct component_match_array - match 列表中的单条匹配规则
 *
 * 每调用一次 component_match_add*()，就向 master 的需求清单追加一条此类型的规则。
 * 一条规则描述："我需要一个满足 compare() 条件的子组件"。
 */
struct component_match_array {
	/**
	 * @data: 传递给 compare/release 回调的上下文数据。
	 *
	 * 在 Rockchip DRM 中，这是具体子设备的 struct device 指针
	 * （如 VOP2 的 platform_device.dev、DSI0 的 platform_device.dev）。
	 * compare_dev() 直接比较 comp->dev == data 来判断是否匹配。
	 */
	void *data;

	/**
	 * @compare: 匹配回调函数。
	 *
	 * 签名：int compare(struct device *comp_dev, void *data)
	 * 返回非零表示匹配成功，返回零表示不匹配。
	 *
	 * find_component() 遍历全局 component_list，
	 * 对每个 component 调用此函数，找到第一个返回非零的即为目标。
	 *
	 * Rockchip 使用 compare_dev()：直接比较设备指针，效率最高。
	 * 其他驱动可使用更复杂的比较逻辑（如按 of_node、按名称等）。
	 */
	int (*compare)(struct device *, void *);

	/**
	 * @release: 可选的释放回调函数。
	 *
	 * master 设备卸载时（devres 触发 devm_component_match_release），
	 * 对每条规则调用此函数，释放 @data 指向的资源（如减少设备引用计数）。
	 *
	 * Rockchip 使用 component_match_add()（无 release 版），此字段为 NULL，
	 * 设备引用计数由 device_link 机制管理。
	 */
	void (*release)(struct device *, void *);

	/**
	 * @component: 匹配成功后指向对应 struct component 的指针。
	 *
	 * 初始值为 NULL（规则刚创建时，尚未找到对应子组件）。
	 * find_components() 搜索成功后赋值为找到的 component 指针。
	 *
	 * 核心判断依据：当 master->match->compare[] 中**所有规则**的
	 * component 字段均非 NULL 时，master 的需求清单凑齐，触发 bind()。
	 */
	struct component *component;

	/**
	 * @duplicate: 重复匹配标志。
	 *
	 * 当同一个 component 被多条规则匹配（即该子组件同时满足多个 master 的条件），
	 * 后匹配的规则将此字段置为 true，表示"这是一个共享子组件"。
	 * 用于 unbind 时的逻辑判断：共享子组件不应被重复解绑。
	 */
	bool duplicate;
};

/**
 * struct component_match - master 的子组件需求清单（动态数组包装）
 *
 * 包装了一个 component_match_array 动态数组，记录 master 声明的
 * 所有子组件需求规则。通过 devres 与 master 设备绑定，自动管理生命周期。
 */
struct component_match {
	/**
	 * @alloc: compare[] 数组当前已分配的槽位总数。
	 *
	 * component_match_realloc() 以 +16 步长扩容，
	 * alloc 始终 >= num，多余的槽位是预留空间。
	 * component_master_add_with_match() 最终会精确收缩到 alloc == num。
	 */
	size_t alloc;

	/**
	 * @num: 已填入的有效规则数量（num <= alloc）。
	 * 每次 component_match_add*() 调用后递增。
	 */
	size_t num;

	/**
	 * @compare: 动态分配的规则数组，每个元素是一条 component_match_array 规则。
	 * 通过 component_match_realloc() 按需扩容，初始为 NULL。
	 */
	struct component_match_array *compare;
};

/**
 * struct master - component framework 中的"聚合主控"
 *
 * 代表一个需要聚合多个子组件才能完整初始化的复合设备驱动。
 * 所有注册的 master 挂在全局 masters 链表上，受 component_mutex 保护。
 *
 * 在 Rockchip DRM 中，display-subsystem 对应一个 master，
 * 其 match 列表包含 VOP2、DSI、HDMI 等所有显示子驱动的需求规则。
 */
struct master {
	/**
	 * @node: 挂入全局 masters 链表的节点。
	 * 每当有新 component 注册，遍历此链表检查是否触发某个 master 的 bind。
	 */
	struct list_head node;

	/**
	 * @bound: master 是否已完成聚合绑定。
	 *
	 * true  → bind() 已成功执行，显示子系统已完整初始化。
	 * false → 正在等待子组件就绪，或 bind() 尚未触发。
	 * 防止重复 bind 的标志。
	 */
	bool bound;

	/**
	 * @ops: master 的操作集，包含两个回调：
	 *   .bind(master_dev)   → 所有子组件就绪时调用，完成整体初始化
	 *                          对应 rockchip_drm_bind()
	 *   .unbind(master_dev) → 驱动卸载或子组件注销时调用，执行反初始化
	 *                          对应 rockchip_drm_unbind()
	 */
	const struct component_master_ops *ops;

	/**
	 * @dev: master 对应的设备节点（display-subsystem platform_device.dev）。
	 * 用于 devres 资源管理、日志输出、debugfs 条目创建等。
	 */
	struct device *dev;

	/**
	 * @match: 指向本 master 的子组件需求清单。
	 * 由 rockchip_drm_match_add() 构建，通过 devres 与 @dev 绑定。
	 */
	struct component_match *match;

	/**
	 * @dentry: debugfs 条目指针（仅 CONFIG_DEBUG_FS 开启时有效）。
	 * 对应 /sys/kernel/debug/component/<master_dev_name>，
	 * 可 cat 查看各子组件的绑定状态，是诊断显示初始化问题的重要工具。
	 */
	struct dentry *dentry;
};

/**
 * struct component - component framework 中的"子组件"
 *
 * 代表显示管道中的一个独立硬件单元（VOP2、DSI0、HDMI 等），
 * 由对应的 platform 驱动在 probe 成功后通过 component_add() 注册。
 * 所有注册的 component 挂在全局 component_list 链表上。
 */
struct component {
	/**
	 * @node: 挂入全局 component_list 链表的节点。
	 * 每次有新 master 注册或新 component 注册时，
	 * find_component() 遍历此链表寻找匹配的子组件。
	 */
	struct list_head node;

	/**
	 * @master: 反向引用归属的 master（匹配成功后由 find_components() 赋值）。
	 *
	 * NULL    → 此子组件尚未被任何 master 匹配（等待中）
	 * 非 NULL → 已归属于某个 master，参与其聚合流程
	 *
	 * 与 component_match_array.component 形成双向引用：
	 *   match_array[i].component → 此 component
	 *   this->master             → 拥有该 match_array 的 master
	 */
	struct master *master;

	/**
	 * @bound: 此子组件是否已执行自身的 bind()。
	 *
	 * master->ops->bind() 内部会依次调用每个子组件的 ops->bind()，
	 * 完成后此标志置为 true。
	 * unbind 时按逆序调用 ops->unbind()，完成后置为 false。
	 */
	bool bound;

	/**
	 * @ops: 子组件的操作集，包含两个回调：
	 *   .bind(comp_dev, master_dev)   → 在 master bind 阶段被调用
	 *                                    执行子组件自身的初始化
	 *                                    对应 dw_mipi_dsi_bind()、vop2_bind() 等
	 *   .unbind(comp_dev, master_dev) → 执行子组件自身的反初始化
	 */
	const struct component_ops *ops;

	/**
	 * @dev: 此子组件对应的设备节点（如 DSI0 的 platform_device.dev）。
	 * find_component() 将此指针传给 compare() 回调进行匹配判断。
	 */
	struct device *dev;
};

static DEFINE_MUTEX(component_mutex);
static LIST_HEAD(component_list);
static LIST_HEAD(masters);

#ifdef CONFIG_DEBUG_FS

static struct dentry *component_debugfs_dir;

static int component_devices_show(struct seq_file *s, void *data)
{
	struct master *m = s->private;
	struct component_match *match = m->match;
	size_t i;

	mutex_lock(&component_mutex);
	seq_printf(s, "%-40s %20s\n", "master name", "status");
	seq_puts(s, "-------------------------------------------------------------\n");
	seq_printf(s, "%-40s %20s\n\n",
		   dev_name(m->dev), m->bound ? "bound" : "not bound");

	seq_printf(s, "%-40s %20s\n", "device name", "status");
	seq_puts(s, "-------------------------------------------------------------\n");
	for (i = 0; i < match->num; i++) {
		struct component *component = match->compare[i].component;

		seq_printf(s, "%-40s %20s\n",
			   component ? dev_name(component->dev) : "(unknown)",
			   component ? (component->bound ? "bound" : "not bound") : "not registered");
	}
	mutex_unlock(&component_mutex);

	return 0;
}

static int component_devices_open(struct inode *inode, struct file *file)
{
	return single_open(file, component_devices_show, inode->i_private);
}

static const struct file_operations component_devices_fops = {
	.open = component_devices_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init component_debug_init(void)
{
	component_debugfs_dir = debugfs_create_dir("device_component", NULL);

	return 0;
}

core_initcall(component_debug_init);

static void component_master_debugfs_add(struct master *m)
{
	m->dentry = debugfs_create_file(dev_name(m->dev), 0444,
					component_debugfs_dir,
					m, &component_devices_fops);
}

static void component_master_debugfs_del(struct master *m)
{
	debugfs_remove(m->dentry);
	m->dentry = NULL;
}

#else

static void component_master_debugfs_add(struct master *m)
{ }

static void component_master_debugfs_del(struct master *m)
{ }

#endif

static struct master *__master_find(struct device *dev,
	const struct component_master_ops *ops)
{
	struct master *m;

	list_for_each_entry(m, &masters, node)
		if (m->dev == dev && (!ops || m->ops == ops))
			return m;

	return NULL;
}

/**
 * find_component - 在全局 component_list 中查找满足条件的子组件
 * @master:       发起查找的 master
 * @compare:      匹配回调，返回非零表示匹配成功
 * @compare_data: 传给 compare 的上下文数据（如目标子设备的 struct device 指针）
 *
 * 返回第一个匹配的 struct component 指针，未找到返回 NULL。
 *
 * ## 过滤规则：跳过已被其他 master 独占的 component
 *
 *   c->master == NULL        → 此 component 尚未归属任何 master，可以候选
 *   c->master == master      → 已归属于本 master（之前某条规则已匹配过），
 *                              仍可候选（允许同一个 component 满足同一 master 的多条规则，
 *                              即 duplicate 场景）
 *   c->master != master（他人）→ 已被其他 master 独占，跳过，不参与本次匹配
 *
 * 这一过滤保证了子组件的归属唯一性：一旦某个 component 被某个 master 的
 * find_components() 先找到并设置 c->master，其他 master 就无法再抢占它。
 */
static struct component *find_component(struct master *master,
	int (*compare)(struct device *, void *), void *compare_data)
{
	struct component *c;

	list_for_each_entry(c, &component_list, node) {
		/* 跳过已归属于其他 master 的 component，保证归属唯一性 */
		if (c->master && c->master != master)
			continue;

		/* 调用调用者提供的比较函数，匹配成功则立即返回 */
		if (compare(c->dev, compare_data))
			return c;
	}

	return NULL; /* 此条规则对应的子组件尚未注册（probe 未完成） */
}

/**
 * find_components - 遍历 master 的需求清单，尝试为每条未匹配规则找到对应 component
 * @master: 发起查找的 master
 *
 * 返回值：
 *   0      → 所有规则均已找到对应 component，need list 凑齐
 *   -ENXIO → 至少一条规则找不到对应 component，need list 未凑齐
 *
 * 本函数是 try_to_bring_up_master() 的第一道关卡，
 * 同时承担"搜索"和"绑定"两个职责：
 *   搜索：在全局 component_list 中为每条未匹配的规则找到候选 component
 *   绑定：建立 match_array[i].component ↔ c->master 的双向关联
 *
 * ## 遍历逻辑
 *
 * 对 match->compare[0..num-1] 中的每条规则：
 *
 *   已匹配（.component != NULL）→ 跳过（之前的调用已处理，无需重复搜索）
 *
 *   未匹配（.component == NULL）→ 调用 find_component() 在 component_list 搜索：
 *     找到 c：
 *       duplicate = !!c->master
 *         若 c->master 已非 NULL（= 本 master，因 find_component 过滤了他人），
 *         表示此 component 同时满足本 master 的多条规则（共享/重复匹配），
 *         标记 duplicate = true，unbind 时据此避免重复解绑。
 *       mc->component = c   建立规则→component 的正向引用
 *       c->master = master  建立 component→master 的反向归属
 *
 *     未找到 c（component 尚未通过 component_add 注册）：
 *       ret = -ENXIO，立即 break（无需继续，清单无法凑齐）
 *
 * ## 幂等性
 *
 * 已匹配的规则（.component != NULL）直接跳过，保证本函数可被多次调用
 * （每次新 component 注册后重新调用），不会重复绑定已完成的规则。
 */
static int find_components(struct master *master)
{
	struct component_match *match = master->match;
	size_t i;
	int ret = 0;

	for (i = 0; i < match->num; i++) {
		struct component_match_array *mc = &match->compare[i];
		struct component *c;

		dev_dbg(master->dev, "Looking for component %zu\n", i);

		/* 此条规则已在上次调用中匹配成功，跳过，保证幂等 */
		if (match->compare[i].component)
			continue;

		/* 在全局 component_list 中搜索满足本条规则的 component */
		c = find_component(master, mc->compare, mc->data);
		if (!c) {
			/* 对应子驱动尚未 probe，清单无法凑齐，提前退出 */
			ret = -ENXIO;
			break;
		}

		dev_dbg(master->dev, "found component %s, duplicate %u\n", dev_name(c->dev), !!c->master);

		/*
		 * 建立双向绑定关系：
		 *   duplicate：c->master 非 NULL 说明此 component 已被本 master
		 *              的另一条规则匹配过（共享场景），标记供 unbind 使用
		 *   mc->component = c：正向引用，规则指向具体 component
		 *   c->master = master：反向归属，阻止其他 master 抢占此 component
		 */
		match->compare[i].duplicate = !!c->master;
		match->compare[i].component = c;
		c->master = master;
	}
	return ret;
}

/* Detach component from associated master */
static void remove_component(struct master *master, struct component *c)
{
	size_t i;

	/* Detach the component from this master. */
	for (i = 0; i < master->match->num; i++)
		if (master->match->compare[i].component == c)
			master->match->compare[i].component = NULL;
}

/*
 * try_to_bring_up_master - 尝试触发 master 的聚合绑定
 * @master:    待检查的 master 对象
 * @component: 触发本次检查的子组件（新注册时传入，master 自检时传 NULL）
 *
 * 本函数是 component 框架的**核心调度引擎**，在以下两个时机被调用：
 *   时机 1：新子组件注册时（component_add），遍历所有 master，
 *           检查这个新子组件是否让某个 master 凑齐了全部依赖
 *   时机 2：master 自身注册时（component_master_add_with_match），
 *           检查此刻是否所有子组件都已就绪（component 参数传 NULL）
 *
 * 返回值：
 *   1  → 成功触发 bind，master 已完成聚合初始化
 *   0  → 条件不满足，暂不触发（等待更多子组件注册）
 *   -errno → bind 执行失败
 *
 * ## 核心判断流程（三道关卡）
 *
 * 关卡 1：find_components(master) —— 全量检查
 *   遍历 master->match->compare[] 数组（即 master 的"需求清单"），
 *   对每条 compare 为 NULL 的规则，在全局 component_list 中搜索满足条件的子组件：
 *     find_component() 调用规则的 compare(comp_dev, data) 回调，
 *     找到后将 match->compare[i].component = c，并将 c->master = master。
 *   若任意一条规则找不到对应子组件 → 返回 -ENXIO，find_components() 非零，
 *   本函数返回 0，继续等待。
 *   全部找到 → find_components() 返回 0，通过此关卡。
 *
 * 关卡 2：component->master != master —— 归属校验
 *   仅当 @component 非 NULL（由 component_add 触发）时执行此检查。
 *   经过关卡 1 后，component_list 中的子组件可能被多个 master 竞争，
 *   find_components() 会将 c->master 赋为最先找到它的 master。
 *   如果触发检查的 @component 最终被分配给了另一个 master，
 *   则当前 master 的检查由那个 master 负责，本次直接返回 0。
 *
 * 关卡 3：devres_open_group —— 为 bind 创建资源组
 *   在调用 bind() 前打开一个 devres 资源组（group token = NULL）。
 *   group 的作用：将 bind() 期间所有子组件通过 devm_* 分配的资源
 *   收集到同一个 group 中，bind 失败时通过 devres_release_group() 一键回滚，
 *   无需手动逐项释放，保证资源清理的完整性。
 */
static int try_to_bring_up_master(struct master *master,
	struct component *component)
{
	int ret;

	dev_dbg(master->dev, "trying to bring up master\n");

	/*
	 * 关卡 1：检查所有 match 规则是否都已找到对应子组件。
	 * find_components() 返回非零（-ENXIO）表示清单未凑齐，继续等待。
	 * 内部副作用：为已就绪的规则填充 .component 指针，并设置 c->master = master。
	 */
	if (find_components(master)) {
		dev_dbg(master->dev, "master has incomplete components\n");
		return 0;
	}

	/*
	 * 关卡 2：归属校验（仅 component_add 触发路径有效）。
	 * 若触发本次检查的子组件 @component 经过关卡 1 后归属于其他 master，
	 * 说明本 master 的聚合由另一个触发点负责，此处跳过。
	 */
	if (component && component->master != master) {
		dev_dbg(master->dev, "master is not for this component (%s)\n",
			dev_name(component->dev));
		return 0;
	}

	/*
	 * 关卡 3：为 bind 过程开启 devres 资源组。
	 * 组内的所有 devm_* 资源在 bind 失败时通过 devres_release_group() 统一回滚。
	 * 分配失败（极低概率）直接返回 -ENOMEM。
	 */
	if (!devres_open_group(master->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * 三道关卡全部通过，触发 master->ops->bind()。
	 * 对于 Rockchip DRM：bind = rockchip_drm_bind()，
	 * 内部完成 drm_device 创建、所有子组件的 bind（注册 CRTC/Encoder/Connector）、
	 * drm_dev_register() 向用户空间开放 /dev/dri/card0 等全部初始化工作。
	 */
	ret = master->ops->bind(master->dev);
	if (ret < 0) {
		/*
		 * bind 失败：通过 devres_release_group 回滚 bind 期间分配的所有资源，
		 * 恢复到 bind 之前的干净状态。
		 * -EPROBE_DEFER 是特殊错误码，表示"依赖未就绪，稍后重试"，
		 * 此情况不打印错误日志（避免日志噪音），由内核延迟重试机制处理。
		 */
		devres_release_group(master->dev, NULL);
		if (ret != -EPROBE_DEFER)
			dev_info(master->dev, "master bind failed: %d\n", ret);
		return ret;
	}

	master->bound = true; /* 标记 master 已成功聚合，防止重复 bind */
	return 1;             /* 返回 1 通知调用者：聚合已成功完成 */
}

static int try_to_bring_up_masters(struct component *component)
{
	struct master *m;
	int ret = 0;

	list_for_each_entry(m, &masters, node) {
		if (!m->bound) {
			ret = try_to_bring_up_master(m, component);
			if (ret != 0)
				break;
		}
	}

	return ret;
}

static void take_down_master(struct master *master)
{
	if (master->bound) {
		master->ops->unbind(master->dev);
		devres_release_group(master->dev, NULL);
		master->bound = false;
	}
}

static void component_match_release(struct device *master,
	struct component_match *match)
{
	unsigned int i;

	for (i = 0; i < match->num; i++) {
		struct component_match_array *mc = &match->compare[i];

		if (mc->release)
			mc->release(master, mc->data);
	}

	kfree(match->compare);
}

static void devm_component_match_release(struct device *dev, void *res)
{
	component_match_release(dev, res);
}

static int component_match_realloc(struct device *dev,
	struct component_match *match, size_t num)
{
	struct component_match_array *new;

	if (match->alloc == num)
		return 0;

	new = kmalloc_array(num, sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (match->compare) {
		memcpy(new, match->compare, sizeof(*new) *
					    min(match->num, num));
		kfree(match->compare);
	}
	match->compare = new;
	match->alloc = num;

	return 0;
}

/**
 * component_match_add_release - 向 match 列表追加一条子组件匹配规则（带释放回调）
 * @master:       component master 设备（如 Rockchip DRM 的 display-subsystem）
 * @matchptr:     指向 match 列表指针的指针（二级指针，允许函数内部修改调用者持有的指针）
 * @release:      可选的释放回调，master 卸载时对每条规则的 compare_data 执行清理
 * @compare:      匹配回调，用于判断某个 component 设备是否满足本条规则
 *                签名：int compare(struct device *comp_dev, void *data)
 *                返回非零表示匹配成功
 * @compare_data: 传递给 compare/release 的上下文数据（如具体的 struct device 指针）
 *
 * ## component 框架中 match 列表的作用
 *
 * match 列表是 master 声明"我需要哪些子组件"的清单。
 * 每调用一次本函数，就向清单追加一条"我需要满足 compare() 条件的那个子组件"。
 *
 * 当所有列在清单上的子组件都已通过 component_add() 注册并就绪时，
 * component 框架自动调用 master->ops->bind()，完成整体聚合初始化。
 *
 * 在 Rockchip DRM 中，rockchip_drm_match_add() 对每个找到的子设备（VOP2、DSI、HDMI 等）
 * 调用 component_match_add()（本函数的无 release 版包装），形成如下 match 列表：
 *   compare[0]: compare_dev, data=VOP2设备指针
 *   compare[1]: compare_dev, data=DSI0设备指针
 *   compare[2]: compare_dev, data=DSI1设备指针
 *   compare[3]: compare_dev, data=HDMI设备指针
 *   ...
 *
 * ## match 列表的内存布局
 *
 * struct component_match {
 *     size_t alloc;                        ← compare[] 数组当前已分配的槽位数
 *     size_t num;                          ← 已填入的有效规则数（num <= alloc）
 *     struct component_match_array *compare; ← 动态扩展的规则数组
 * }
 *
 * struct component_match_array {
 *     void *data;                          ← compare_data（如子设备指针）
 *     int  (*compare)(dev, data);          ← 匹配回调
 *     void (*release)(dev, data);          ← 释放回调（可为 NULL）
 *     struct component *component;         ← 匹配成功后反向指向找到的 component
 *     bool duplicate;                      ← 重复匹配检测标志
 * }
 */
void component_match_add_release(struct device *master,
	struct component_match **matchptr,
	void (*release)(struct device *, void *),
	int (*compare)(struct device *, void *), void *compare_data)
{
	struct component_match *match = *matchptr;

	/*
	 * 快速失败：若 match 指针已是错误码（之前某次调用失败留下的），
	 * 直接返回，不做任何操作。
	 * 调用者最终通过 IS_ERR(*matchptr) 统一判断是否有错误发生。
	 * 这种"粘性错误"设计避免了每次调用都需要检查返回值的繁琐。
	 */
	if (IS_ERR(match))
		return;

	if (!match) {
		/*
		 * 首次调用，match 列表尚未创建，使用 devres 分配。
		 *
		 * devres_alloc(devm_component_match_release, sizeof(*match), GFP_KERNEL)：
		 *   分配一个 struct component_match 大小的内存，并注册释放回调
		 *   devm_component_match_release。
		 *   当 master 设备被 detach（卸载）时，devres 框架自动调用此释放函数，
		 *   遍历 match->compare[] 数组，依次调用每条规则的 release() 回调，
		 *   再 kfree(match->compare)，无需驱动手动清理。
		 *
		 * 注意：此处只分配了 struct component_match 本体，
		 * compare[] 数组（存放各条规则）在后续 component_match_realloc() 中分配。
		 */
		match = devres_alloc(devm_component_match_release,
				     sizeof(*match), GFP_KERNEL);
		if (!match) {
			*matchptr = ERR_PTR(-ENOMEM); /* 粘性错误，后续调用直接跳过 */
			return;
		}

		/*
		 * devres_add：将已分配的 devres 资源绑定到 master 设备的 devres 链表上。
		 * 从此刻起，match 的生命周期由 master 设备的 devres 机制托管，
		 * master 设备释放时自动触发 devm_component_match_release()。
		 */
		devres_add(master, match);

		*matchptr = match; /* 将新分配的 match 写回调用者的指针变量 */
	}

	if (match->num == match->alloc) {
		/*
		 * compare[] 数组已满（或尚未分配），需要扩容。
		 *
		 * 每次以 +16 为步长扩容（新容量 = 旧容量 + 16），
		 * 采用固定步长而非倍增，是因为 match 列表通常只有个位数到十几条规则，
		 * 不需要倍增策略，固定步长避免过度分配。
		 *
		 * component_match_realloc 内部：
		 *   kmalloc_array(new_size, sizeof(*new))  → 分配新数组
		 *   memcpy(new, old, min(num, new_size))   → 拷贝已有规则
		 *   kfree(old compare[])                   → 释放旧数组
		 *   match->compare = new; match->alloc = new_size
		 *
		 * 失败时同样置为粘性错误，后续调用直接跳过。
		 */
		size_t new_size = match->alloc + 16;
		int ret;

		ret = component_match_realloc(master, match, new_size);
		if (ret) {
			*matchptr = ERR_PTR(ret);
			return;
		}
	}

	/*
	 * 在 compare[] 数组的末尾追加新规则，填写四个字段：
	 *
	 *   compare  → 匹配回调（如 compare_dev：直接比较设备指针是否相等）
	 *   release  → 释放回调（可为 NULL，Rockchip 使用无 release 版本）
	 *   data     → 匹配上下文（如具体子设备的 struct device 指针）
	 *   component→ 初始化为 NULL，后续 try_to_bring_up_master() 扫描
	 *              component_list 时，若某个 component 满足本规则，
	 *              则将其指针写入此字段，表示"本规则已找到对应的子组件"
	 *
	 * 只有 match->compare[] 中所有规则的 component 字段都非 NULL 时，
	 * master 才会被 bind。
	 */
	match->compare[match->num].compare = compare;
	match->compare[match->num].release = release;
	match->compare[match->num].data = compare_data;
	match->compare[match->num].component = NULL;
	match->num++;
}
EXPORT_SYMBOL(component_match_add_release);

static void free_master(struct master *master)
{
	struct component_match *match = master->match;
	int i;

	component_master_debugfs_del(master);
	list_del(&master->node);

	if (match) {
		for (i = 0; i < match->num; i++) {
			struct component *c = match->compare[i].component;
			if (c)
				c->master = NULL;
		}
	}

	kfree(master);
}

/**
 * component_master_add_with_match - 将设备注册为 component master 并尝试触发聚合
 * @dev:   master 设备（如 Rockchip DRM 的 display-subsystem platform_device）
 * @ops:   master 操作集，包含 .bind 和 .unbind 两个回调
 * @match: 由 component_match_add*() 系列函数构建的子组件需求列表
 *
 * 返回值：0 表示成功（无论是否已触发 bind）；负值表示错误
 *
 * ## 函数职责
 *
 * 本函数完成 master 注册的最后一步，将 master 加入全局 masters 链表，
 * 并立即尝试检查此刻子组件是否已全部就绪（同步触发路径）。
 *
 * ## 为什么返回 0 并不代表 bind 已完成？
 *
 * 有两种成功路径：
 *
 *   路径 A（同步完成）：调用本函数时所有子组件已通过 component_add() 注册，
 *     try_to_bring_up_master() 返回 1，bind() 在本函数调用栈内同步执行完毕。
 *     对于 Rockchip DRM，这意味着 /dev/dri/card0 在 probe 返回前就已创建。
 *
 *   路径 B（异步等待）：部分子组件尚未 probe（component_add 尚未调用），
 *     try_to_bring_up_master() 返回 0，本函数同样返回 0，
 *     master 挂在 masters 链表上等待。
 *     后续每当有新子组件注册（component_add → try_to_bring_up_masters），
 *     会再次遍历 masters 链表调用 try_to_bring_up_master()，
 *     直到最后一个子组件就绪时触发 bind。
 *
 * ## 执行流程
 */
int component_master_add_with_match(struct device *dev,
	const struct component_master_ops *ops,
	struct component_match *match)
{
	struct master *master;
	int ret;

	/*
	 * 将 match->compare[] 数组缩减到实际使用的大小（match->num 条规则）。
	 * component_match_add*() 每次以 +16 步长扩容，可能存在尾部空槽，
	 * 在此精确收缩，避免浪费内存，同时使 alloc == num，结构更整洁。
	 */
	ret = component_match_realloc(dev, match, match->num);
	if (ret)
		return ret;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	/* 初始化 master 对象的三个核心字段 */
	master->dev  = dev;   /* 反向引用 master 设备，用于 devres/日志 */
	master->ops  = ops;   /* 绑定 bind/unbind 操作集 */
	master->match = match; /* 关联子组件需求清单 */

	/*
	 * 在 debugfs 下为本 master 创建条目（如 /sys/kernel/debug/component/display-subsystem），
	 * 可通过 cat 查看各子组件的绑定状态（bound / not bound / not registered），
	 * 是调试"为什么 DRM 没有初始化"的重要工具。【笔记钩子】
	 * 若未开启 CONFIG_DEBUG_FS，此函数为空操作。
	 */
	component_master_debugfs_add(master);

	mutex_lock(&component_mutex); /* 保护全局 masters 链表和 component_list */

	/*
	 * 将 master 加入全局 masters 链表头部。
	 * 此后每当有新子组件注册（component_add），
	 * 都会遍历此链表调用 try_to_bring_up_master()，
	 * 检查新子组件是否让某个 master 的需求清单凑齐。
	 */
	list_add(&master->node, &masters);

	/*
	 * 立即尝试触发聚合绑定（component 参数传 NULL，表示 master 自检）。
	 *
	 * 返回值语义：
	 *   ret == 1  → bind() 已成功执行，master 聚合完成
	 *   ret == 0  → 子组件未凑齐，挂起等待（正常的异步路径）
	 *   ret < 0   → bind() 执行失败或资源不足，需要清理
	 *
	 * 失败时调用 free_master()：
	 *   → 从 masters 链表移除本 master
	 *   → 解除所有已匹配子组件的 c->master 指针（避免悬空引用）
	 *   → kfree(master)
	 */
	ret = try_to_bring_up_master(master, NULL);

	if (ret < 0)
		free_master(master);

	mutex_unlock(&component_mutex);

	/*
	 * 将 ret=1（bind 成功）和 ret=0（等待中）统一归为成功返回 0。
	 * 调用者（如 rockchip_drm_platform_probe）无需区分这两种情况，
	 * bind 的完成会通过其他机制（devres、drm_dev_register）体现。
	 */
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(component_master_add_with_match);
EXPORT_SYMBOL_GPL(component_master_add_with_match);

void component_master_del(struct device *dev,
	const struct component_master_ops *ops)
{
	struct master *master;

	mutex_lock(&component_mutex);
	master = __master_find(dev, ops);
	if (master) {
		take_down_master(master);
		free_master(master);
	}
	mutex_unlock(&component_mutex);
}
EXPORT_SYMBOL_GPL(component_master_del);

static void component_unbind(struct component *component,
	struct master *master, void *data)
{
	WARN_ON(!component->bound);

	component->ops->unbind(component->dev, master->dev, data);
	component->bound = false;

	/* Release all resources claimed in the binding of this component */
	devres_release_group(component->dev, component);
}

void component_unbind_all(struct device *master_dev, void *data)
{
	struct master *master;
	struct component *c;
	size_t i;

	WARN_ON(!mutex_is_locked(&component_mutex));

	master = __master_find(master_dev, NULL);
	if (!master)
		return;

	/* Unbind components in reverse order */
	for (i = master->match->num; i--; )
		if (!master->match->compare[i].duplicate) {
			c = master->match->compare[i].component;
			component_unbind(c, master, data);
		}
}
EXPORT_SYMBOL_GPL(component_unbind_all);

static int component_bind(struct component *component, struct master *master,
	void *data)
{
	int ret;

	/*
	 * Each component initialises inside its own devres group.
	 * This allows us to roll-back a failed component without
	 * affecting anything else.
	 */
	if (!devres_open_group(master->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * Also open a group for the device itself: this allows us
	 * to release the resources claimed against the sub-device
	 * at the appropriate moment.
	 */
	if (!devres_open_group(component->dev, component, GFP_KERNEL)) {
		devres_release_group(master->dev, NULL);
		return -ENOMEM;
	}

	dev_dbg(master->dev, "binding %s (ops %ps)\n",
		dev_name(component->dev), component->ops);

	//bind即初始化组件自身功能，为契合group框架，bind里使用devres系列函数申请资源
	ret = component->ops->bind(component->dev, master->dev, data);
	if (!ret) {
		component->bound = true;

		/*
		 * Close the component device's group so that resources
		 * allocated in the binding are encapsulated for removal
		 * at unbind.  Remove the group on the DRM device as we
		 * can clean those resources up independently.
		 */
		devres_close_group(component->dev, NULL);
		devres_remove_group(master->dev, NULL);

		dev_info(master->dev, "bound %s (ops %ps)\n",
			 dev_name(component->dev), component->ops);
	} else {
		devres_release_group(component->dev, NULL);
		devres_release_group(master->dev, NULL);

		if (ret != -EPROBE_DEFER)
			dev_err(master->dev, "failed to bind %s (ops %ps): %d\n",
				dev_name(component->dev), component->ops, ret);
	}

	return ret;
}

int component_bind_all(struct device *master_dev, void *data)
{
	struct master *master;
	struct component *c;
	size_t i;
	int ret = 0;

	WARN_ON(!mutex_is_locked(&component_mutex));

	master = __master_find(master_dev, NULL);
	if (!master)
		return -EINVAL;

	/* Bind components in match order */
	for (i = 0; i < master->match->num; i++)
		if (!master->match->compare[i].duplicate) {
			c = master->match->compare[i].component;
			ret = component_bind(c, master, data);
			if (ret)
				break;
		}

	if (ret != 0) {
		for (; i > 0; i--)
			if (!master->match->compare[i - 1].duplicate) {
				c = master->match->compare[i - 1].component;
				component_unbind(c, master, data);
			}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(component_bind_all);

int component_add(struct device *dev, const struct component_ops *ops)
{
	struct component *component;
	int ret;

	component = kzalloc(sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;

	component->ops = ops;
	component->dev = dev;

	dev_dbg(dev, "adding component (ops %ps)\n", ops);

	mutex_lock(&component_mutex);
	list_add_tail(&component->node, &component_list);

	ret = try_to_bring_up_masters(component);
	if (ret < 0) {
		if (component->master)
			remove_component(component->master, component);
		list_del(&component->node);

		kfree(component);
	}
	mutex_unlock(&component_mutex);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(component_add);

void component_del(struct device *dev, const struct component_ops *ops)
{
	struct component *c, *component = NULL;

	mutex_lock(&component_mutex);
	list_for_each_entry(c, &component_list, node)
		if (c->dev == dev && c->ops == ops) {
			list_del(&c->node);
			component = c;
			break;
		}

	if (component && component->master) {
		take_down_master(component->master);
		remove_component(component->master, component);
	}

	mutex_unlock(&component_mutex);

	WARN_ON(!component);
	kfree(component);
}
EXPORT_SYMBOL_GPL(component_del);

MODULE_LICENSE("GPL v2");
