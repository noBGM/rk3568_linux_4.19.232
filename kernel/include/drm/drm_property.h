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

#ifndef __DRM_PROPERTY_H__
#define __DRM_PROPERTY_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>

/**
 * struct drm_property_enum - symbolic values for enumerations
 * @value: numeric property value for this enum entry
 * @head: list of enum values, linked to &drm_property.enum_list
 * @name: symbolic name for the enum
 *
 * For enumeration and bitmask properties this structure stores the symbolic
 * decoding for each value. This is used for example for the rotation property.
 */
struct drm_property_enum {
	uint64_t value;
	struct list_head head;
	char name[DRM_PROP_NAME_LEN];
};

/**
 * struct drm_property - KMS 模式设置对象的属性描述符
 *
 * ## 什么是 Property？
 *
 * Property 是 DRM 对象（CRTC、Plane、Connector）向用户空间暴露可配置参数的
 * 统一接口。它把"属性名称"和"合法值域"绑定在一起，是 KMS 子系统的
 * 通用元数据传输机制。
 *
 * 你可以把它理解为一个带名字和值域约束的"旋钮"：
 *   - 旋钮的名字：@name（如 "rotation"、"DPMS"、"EDID"）
 *   - 旋钮的可选值范围：由 @flags 类型决定（范围/枚举/位掩码/对象/blob）
 *   - 旋钮挂在哪个硬件对象上：通过 drm_object_attach_property() 关联
 *
 * ## "属性描述符" vs "属性实例值"的区别
 *
 * drm_property 本身只是**描述符**，定义了属性的名称和合法值域，
 * 是所有同类对象共享的"模板"。
 *
 * 而每个对象实际持有的**当前值**，存储在该对象自己的 properties 结构体里
 * （drm_object_properties），两者分离。
 *
 * ## 同名属性的重用与复制规则
 *
 * 当驱动想在不同对象上使用同名属性时，有两种情况：
 *
 * 情况 1：名称相同，值域也相同 → **可以复用同一个 drm_property 实例**
 *   例如所有 Plane 都支持完整的 0-359° 旋转范围，可以共享一个 "rotation" 属性
 *
 * 情况 2：名称相同，但值域不同 → **必须为每个对象单独创建属性**
 *   例如 Primary Plane 不支持旋转（值域只有 "0"），而 Overlay Plane 支持全范围旋转，
 *   则必须创建两个不同的 "rotation" 属性对象，分别附加到对应 Plane 上
 *
 * **重要**：用户空间**不能**假设相同名称的属性在不同 KMS 对象上具有相同的对象 ID，
 * 必须每次通过名称动态查询对应对象上该属性的 ID。
 *
 * ## 创建与附加流程
 *
 * 1. 根据 @flags 类型选择对应的创建函数创建属性描述符（见 @flags 注释）
 * 2. 调用 drm_object_attach_property(obj, property, default_value) 将属性
 *    附加到具体的 KMS 对象上，并设置初始值
 *    目前只有 &drm_connector、&drm_crtc、&drm_plane 支持附加属性
 * 3. 用户空间通过 DRM_IOCTL_MODE_OBJ_GETPROPERTIES 查询对象的属性列表，
 *    再通过 DRM_IOCTL_MODE_SETPROPERTY 或原子 ioctl 设置值
 *
 * ## Property 在原子提交中的角色
 *
 * Property 是原子 IOCTL 的**通用元数据传输总线**。
 *
 * 旧式（Legacy）modeset ioctl 直接在结构体里设置参数，耦合度高、无法原子化。
 * 原子模式（Atomic KMS）把所有可配置参数都统一表达为 Property：
 *
 *   Legacy 方式：ioctl 结构体字段直接设置 plane 的 src_x/src_y/crtc_x/crtc_y
 *   原子方式：  通过 "SRC_X"、"SRC_Y"、"CRTC_X"、"CRTC_Y" 等 Property 设置
 *
 *   Legacy 方式：ioctl 字段直接指定 plane 绑定哪个 framebuffer
 *   原子方式：  通过 "FB_ID" Property（Object 类型）指定
 *
 *   Legacy 方式：ioctl 字段直接指定 connector 绑定哪个 CRTC
 *   原子方式：  通过 "CRTC_ID" Property（Object 类型）指定
 *
 * 这些由原子模式新增的参数属性，均设置了 DRM_MODE_PROP_ATOMIC 标志，
 * 不会暴露给不理解原子语义的旧式用户空间程序。
 */
struct drm_property {
	/**
	 * @head: per-device list of properties, for cleanup.
	 */
	struct list_head head;

	/**
	 * @base: base KMS object
	 */
	struct drm_mode_object base;

	/**
	 * @flags: 属性类型标志位（类型 + 修饰符的组合）。
	 *
	 * 每个属性必须是以下**五种类型之一**（互斥，只能选一种）：
	 *
	 * DRM_MODE_PROP_RANGE（无符号范围型）
	 *     属性值是一个无符号整数，有明确的最小值和最大值。
	 *     @values[0] = 最小值，@values[1] = 最大值。
	 *     KMS 核心在设置时自动校验值是否在 [min, max] 范围内，超出则拒绝。
	 *     适用场景：亮度调节（0-255）、透明度（0-255）、音量（0-100）。
	 *     创建函数：drm_property_create_range()
	 *
	 * DRM_MODE_PROP_SIGNED_RANGE（有符号范围型）
	 *     与 RANGE 相同，但范围是有符号整数（支持负数）。
	 *     适用场景：色调偏移（-180 到 +180 度）、对比度补偿（-100 到 +100）。
	 *     创建函数：drm_property_create_signed_range()
	 *
	 * DRM_MODE_PROP_ENUM（枚举型）
	 *     属性值是一个整数索引（从 0 开始），每个索引对应一个有意义的字符串名称，
	 *     名称-值映射存储在 @enum_list 链表中。
	 *     用户空间通过 ioctl 读取枚举列表，再用数值索引来 get/set 属性。
	 *     适用场景：
	 *       "DPMS"   → {0: "On", 1: "Standby", 2: "Suspend", 3: "Off"}
	 *       "scaling mode" → {0: "None", 1: "Full", 2: "Center", 3: "Full aspect"}
	 *       "Content Protection" → {0: "Undesired", 1: "Desired", 2: "Enabled"}
	 *     创建函数：drm_property_create_enum()
	 *
	 * DRM_MODE_PROP_BITMASK（位掩码型）
	 *     枚举型的变体：所有枚举值都限制在 0-63 范围内（即 64 个比特位）。
	 *     属性的实际值是多个枚举位的 OR 组合，可以同时选中多个选项。
	 *     枚举项同样存储在 @enum_list 中，但每项代表一个独立的比特位。
	 *     适用场景：
	 *       "rotation" → {bit0: "rotate-0", bit1: "rotate-90",
	 *                     bit2: "rotate-180", bit3: "rotate-270",
	 *                     bit4: "reflect-x", bit5: "reflect-y"}
	 *     创建函数：drm_property_create_bitmask()
	 *
	 * DRM_MODE_PROP_OBJECT（对象引用型）
	 *     属性值是另一个 KMS 对象的 ID，用于在 KMS 对象之间建立显式的关联关系。
	 *     是原子提交构建显示管道的核心机制：
	 *       "FB_ID" 属性（Plane 上）    → 值为 drm_framebuffer 的 ID
	 *       "CRTC_ID" 属性（Plane 上）  → 值为 drm_crtc 的 ID（指定 Plane 属于哪个 CRTC）
	 *       "CRTC_ID" 属性（Connector 上）→ 值为 drm_crtc 的 ID（指定 Connector 输出到哪个 CRTC）
	 *     每个 Object 属性只能关联特定类型的对象，KMS 核心负责类型检查。
	 *     仅限原子驱动使用，必须同时设置 DRM_MODE_PROP_ATOMIC 标志。
	 *     创建函数：drm_property_create_object()
	 *
	 * DRM_MODE_PROP_BLOB（二进制大对象型）
	 *     属性值是一个"blob 对象"的 ID，blob 对象可以存储任意格式的二进制数据。
	 *     blob 对象独立于属性存在，通过 drm_property_create_blob() 或对应 IOCTL 创建。
	 *     属性只存储 blob 对象的 ID，读取属性值时需要再用该 ID 查询 blob 内容。
	 *     适用场景：
	 *       "EDID" 属性  → blob 内容是 128/256 字节的原始 EDID 数据
	 *       "PATH" 属性  → blob 内容是 DP MST 路径字符串
	 *       "DEGAMMA_LUT" → blob 内容是 Gamma 查找表数组
	 *     本质上与 Object 属性类似，但限制只能关联 blob 对象，
	 *     存在的唯一原因是与已有用户空间代码的向后兼容性。
	 *     创建函数：drm_property_create()，类型传 DRM_MODE_PROP_BLOB
	 *
	 * ---
	 * 除类型外，以下**修饰符标志**可以与任意类型组合使用：
	 *
	 * DRM_MODE_PROP_ATOMIC（原子标志）
	 *     标记此属性承载原子模式设置的状态，仅对理解原子语义的用户空间可见。
	 *     旧式用户空间（X11 legacy、非原子 Wayland）看不到此类属性。
	 *     所有通过原子 IOCTL 管理的参数（帧缓冲 ID、Plane 位置、CRTC 链接等）
	 *     都带有此标志。
	 *
	 * DRM_MODE_PROP_IMMUTABLE（只读标志）
	 *     标记此属性的值用户空间只能读取，不能写入，只有内核才能更新。
	 *     通常用于向用户空间暴露探测到的硬件信息，例如：
	 *       - "EDID"：显示器上报的能力数据，驱动读取后写入，用户空间只读
	 *       - "PATH"：DP MST 拓扑路径，由驱动在热插拔时更新
	 *       - "TILE"：拼接显示器的分块信息，驱动自动填充
	 */
	uint32_t flags;

	/**
	 * @name: symbolic name of the properties
	 */
	char name[DRM_PROP_NAME_LEN];

	/**
	 * @num_values: size of the @values array.
	 */
	uint32_t num_values;

	/**
	 * @values:
	 *
	 * Array with limits and values for the property. The
	 * interpretation of these limits is dependent upon the type per @flags.
	 */
	uint64_t *values;

	/**
	 * @dev: DRM device
	 */
	struct drm_device *dev;

	/**
	 * @enum_list:
	 *
	 * List of &drm_prop_enum_list structures with the symbolic names for
	 * enum and bitmask values.
	 */
	struct list_head enum_list;
};

/**
 * struct drm_property_blob - Blob data for &drm_property
 * @base: base KMS object
 * @dev: DRM device
 * @head_global: entry on the global blob list in
 * 	&drm_mode_config.property_blob_list.
 * @head_file: entry on the per-file blob list in &drm_file.blobs list.
 * @length: size of the blob in bytes, invariant over the lifetime of the object
 * @data: actual data, embedded at the end of this structure
 *
 * Blobs are used to store bigger values than what fits directly into the 64
 * bits available for a &drm_property.
 *
 * Blobs are reference counted using drm_property_blob_get() and
 * drm_property_blob_put(). They are created using drm_property_create_blob().
 */
/**
 * struct drm_property_blob - DRM 属性的二进制大对象载体
 *
 * ## 为什么需要 blob？
 *
 * drm_property 的实例值只有 64 位宽，足以存放整数、枚举索引或对象 ID，
 * 但对于 EDID 数据（128~256 字节）、Gamma/CTM 查找表（数百至数千字节）
 * 这类大块二进制数据，64 位完全放不下。
 *
 * 解决方案：引入 blob 对象作为"附件包"——
 *   - blob 对象独立存在于 KMS 对象系统中，拥有自己的对象 ID
 *   - DRM_MODE_PROP_BLOB 类型的属性只存储该 blob 的 ID（64 位足够）
 *   - 用户空间用这个 ID 再单独查询 blob 的二进制内容
 *
 * 典型使用场景：
 *   "EDID"       属性 → blob 内容：显示器原始 EDID 字节流（128/256 字节）
 *   "DEGAMMA_LUT"属性 → blob 内容：去伽马查找表（drm_color_lut 数组）
 *   "CTM"        属性 → blob 内容：3x3 色彩转换矩阵（drm_color_ctm）
 *   "GAMMA_LUT"  属性 → blob 内容：伽马查找表（drm_color_lut 数组）
 *   "PATH"       属性 → blob 内容：DP MST 拓扑路径字符串
 *
 * ## 内存布局（柔性数组尾部嵌入）
 *
 * blob 对象与其数据内容**一次性分配**，数据紧跟在结构体末尾：
 *
 *   [ drm_property_blob 结构体 | 实际 data 内容（length 字节）]
 *
 * @data 指针指向结构体紧后方的内存，无需二次分配和指针追踪，
 * 释放时只需 kfree(blob) 一次即可。
 *
 * ## 生命周期管理
 *
 * blob 使用引用计数管理，有两个来源持有引用：
 *
 * 1. 用户空间通过 DRM_IOCTL_MODE_CREATEPROPBLOB 创建，关闭 drm_file 时
 *    从 @head_file 链表摘除并减引用（文件级生命周期）
 * 2. 内核通过 drm_property_create_blob() 创建，显式调用
 *    drm_property_blob_put() 释放（内核级生命周期）
 *
 * 引用计数 API：
 *   drm_property_blob_get()    增加引用（不释放）
 *   drm_property_blob_put()    减少引用，归零时释放整块内存
 *   drm_property_create_blob() 创建并返回持有引用的 blob
 */
struct drm_property_blob {
	/**
	 * @base: 继承自 drm_mode_object 的基类。
	 *
	 * 使 blob 成为标准 KMS 对象，拥有全局唯一的对象 ID。
	 * 用户空间可以用该 ID 通过 DRM_IOCTL_MODE_GETPROPBLOB 查询 blob 内容。
	 * drm_mode_object.type = DRM_MODE_OBJECT_BLOB。
	 */
	struct drm_mode_object base;

	/**
	 * @dev: 此 blob 归属的 DRM 设备。
	 * 用于在释放时将自身从设备全局 blob 链表（@head_global）中摘除。
	 */
	struct drm_device *dev;

	/**
	 * @head_global: 挂入设备全局 blob 链表的节点。
	 *
	 * 链表头：drm_mode_config.property_blob_list
	 *
	 * 所有存活的 blob 对象都挂在此链表上，内核可以遍历它来：
	 *   - 设备关闭时批量清理所有 blob
	 *   - 调试时列出当前所有 blob 对象
	 */
	struct list_head head_global;

	/**
	 * @head_file: 挂入创建者 drm_file 的 per-file blob 链表的节点。
	 *
	 * 链表头：drm_file.blobs
	 *
	 * 当用户空间进程通过 DRM_IOCTL_MODE_CREATEPROPBLOB 创建 blob 时，
	 * 该 blob 同时挂入进程对应 drm_file 的 blobs 链表。
	 * 进程关闭（drm_file 释放）时，内核遍历此链表，
	 * 对每个 blob 调用 drm_property_blob_put()，防止进程退出后内存泄漏。
	 *
	 * 注意：内核内部创建的 blob（drm_property_create_blob()）
	 * 不属于任何 drm_file，此字段为空链表节点。
	 */
	struct list_head head_file;

	/**
	 * @length: blob 数据内容的字节数。
	 *
	 * 在 blob 对象整个生命周期内不可变（创建时确定，此后只读）。
	 * 对应分配内存大小：sizeof(struct drm_property_blob) + length
	 */
	size_t length;

	/**
	 * @data: 指向 blob 二进制数据的指针。
	 *
	 * 指向结构体末尾紧接的内存区域（柔性数组模式），
	 * 与结构体本身一次性分配，无需单独释放。
	 * 数据格式完全由上层约定（如 struct drm_color_lut[]、原始 EDID 字节流等），
	 * drm 核心对内容格式不做任何约束或解析。
	 */
	void *data;
};

struct drm_prop_enum_list {
	int type;
	const char *name;
};

#define obj_to_property(x) container_of(x, struct drm_property, base)
#define obj_to_blob(x) container_of(x, struct drm_property_blob, base)

/**
 * drm_property_type_is - check the type of a property
 * @property: property to check
 * @type: property type to compare with
 *
 * This is a helper function becauase the uapi encoding of property types is
 * a bit special for historical reasons.
 */
static inline bool drm_property_type_is(struct drm_property *property,
					uint32_t type)
{
	/* instanceof for props.. handles extended type vs original types: */
	if (property->flags & DRM_MODE_PROP_EXTENDED_TYPE)
		return (property->flags & DRM_MODE_PROP_EXTENDED_TYPE) == type;
	return property->flags & type;
}

struct drm_property *drm_property_create(struct drm_device *dev,
					 u32 flags, const char *name,
					 int num_values);
struct drm_property *drm_property_create_enum(struct drm_device *dev,
					      u32 flags, const char *name,
					      const struct drm_prop_enum_list *props,
					      int num_values);
struct drm_property *drm_property_create_bitmask(struct drm_device *dev,
						 u32 flags, const char *name,
						 const struct drm_prop_enum_list *props,
						 int num_props,
						 uint64_t supported_bits);
struct drm_property *drm_property_create_range(struct drm_device *dev,
					       u32 flags, const char *name,
					       uint64_t min, uint64_t max);
struct drm_property *drm_property_create_signed_range(struct drm_device *dev,
						      u32 flags, const char *name,
						      int64_t min, int64_t max);
struct drm_property *drm_property_create_object(struct drm_device *dev,
						u32 flags, const char *name,
						uint32_t type);
struct drm_property *drm_property_create_bool(struct drm_device *dev,
					      u32 flags, const char *name);
int drm_property_add_enum(struct drm_property *property,
			  uint64_t value, const char *name);
void drm_property_destroy(struct drm_device *dev, struct drm_property *property);

struct drm_property_blob *drm_property_create_blob(struct drm_device *dev,
						   size_t length,
						   const void *data);
struct drm_property_blob *drm_property_lookup_blob(struct drm_device *dev,
						   uint32_t id);
int drm_property_replace_global_blob(struct drm_device *dev,
				     struct drm_property_blob **replace,
				     size_t length,
				     const void *data,
				     struct drm_mode_object *obj_holds_id,
				     struct drm_property *prop_holds_id);
bool drm_property_replace_blob(struct drm_property_blob **blob,
			       struct drm_property_blob *new_blob);
struct drm_property_blob *drm_property_blob_get(struct drm_property_blob *blob);
void drm_property_blob_put(struct drm_property_blob *blob);

/**
 * drm_property_find - find property object
 * @dev: DRM device
 * @file_priv: drm file to check for lease against.
 * @id: property object id
 *
 * This function looks up the property object specified by id and returns it.
 */
static inline struct drm_property *drm_property_find(struct drm_device *dev,
						     struct drm_file *file_priv,
						     uint32_t id)
{
	struct drm_mode_object *mo;
	mo = drm_mode_object_find(dev, file_priv, id, DRM_MODE_OBJECT_PROPERTY);
	return mo ? obj_to_property(mo) : NULL;
}

#endif
