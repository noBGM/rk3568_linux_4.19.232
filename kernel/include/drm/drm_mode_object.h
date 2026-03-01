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

#ifndef __DRM_MODESET_H__
#define __DRM_MODESET_H__

#include <linux/kref.h>
#include <drm/drm_lease.h>
struct drm_object_properties;
struct drm_property;
struct drm_device;
struct drm_file;

/**
 * struct drm_mode_object - KMS 模式设置对象的公共基类
 *
 * ## 什么是 drm_mode_object？
 *
 * DRM 子系统中所有面向用户空间可见的 KMS 对象（CRTC、Plane、Connector、
 * Encoder、FrameBuffer、PropertyBlob 等）都在结构体的头部内嵌此基类，
 * 就像 C++ 继承关系一样，统一了所有 KMS 对象的身份标识、属性管理和生命周期。
 *
 * 内嵌此基类的 KMS 对象类型（@type 字段的可能值）由 drm_mode.h 文件定义，
 * 如：
 *   DRM_MODE_OBJECT_CRTC       → drm_crtc（视频时序控制器）
 *   DRM_MODE_OBJECT_CONNECTOR  → drm_connector（物理显示接口）
 *   DRM_MODE_OBJECT_ENCODER    → drm_encoder（信号编码器）
 *   DRM_MODE_OBJECT_MODE       → drm_display_mode（显示模式参数）
 *   DRM_MODE_OBJECT_PROPERTY   → drm_property（属性描述符）
 *   DRM_MODE_OBJECT_FB         → drm_framebuffer（帧缓冲）
 *   DRM_MODE_OBJECT_BLOB       → drm_property_blob（二进制大对象）
 *   DRM_MODE_OBJECT_PLANE      → drm_plane（显示层）
 *
 * ## 提供的两项核心服务
 *
 * ### 服务 1：对象身份标识（ID + 类型）
 *
 * 每个 KMS 对象都有全局唯一的整数 ID（@id），用户空间通过这个 ID 在 ioctl
 * 中引用内核对象，内核通过 drm_mode_object_find(dev, file, id, type) 反查：
 *
 *   用户空间原子 ioctl 传入 plane_id=5
 *       → 内核调用 drm_mode_object_find(dev, file, 5, DRM_MODE_OBJECT_PLANE)
 *       → 查找全局 idr 表，返回对应的 drm_plane 对象指针
 *       → 再通过 container_of(obj, struct drm_plane, base) 获取完整结构体
 *
 * ### 服务 2：属性追踪（@properties）
 *
 * CRTC、Plane、Connector 三类对象都通过此机制管理附加在自身上的所有属性：
 *
 *   drm_object_attach_property(obj, property, default_val)
 *       → 在对象对用户空间可见之前，将属性描述符和初始值注册到 @properties 表
 *
 * 属性值的存储方式分两种（重要区别）：
 *
 *   旧式驱动（非原子）：
 *     当前值直接存在 drm_object_properties.values[] 数组里，
 *     通过 drm_object_property_get/set_value() 读写。
 *
 *   原子驱动：
 *     可变属性（不带 DRM_MODE_PROP_IMMUTABLE 标志）的当前值存在各自的
 *     state 结构体里（drm_crtc_state、drm_plane_state、drm_connector_state），
 *     通过 atomic_get/set_property() 回调进行编解码。
 *     drm_object_properties.values[] 只存储不可变属性（IMMUTABLE）的值。
 *     原子驱动**不应**对可变属性调用 drm_object_property_get/set_value()。
 *
 * ### 服务 3：动态生命周期管理（@refcount + @free_cb）
 *
 * 并非所有 KMS 对象的生命周期都与设备绑定：
 *
 *   静态生命周期对象（@free_cb == NULL）：
 *     drm_crtc、drm_plane、drm_encoder 等，随设备创建而创建，
 *     随设备销毁而销毁，不需要引用计数。
 *
 *   动态生命周期对象（@free_cb != NULL）：
 *     drm_framebuffer、drm_connector、drm_property_blob
 *     可以在设备运行期间动态创建和销毁（热插拔、用户主动创建/释放）。
 *     通过 drm_mode_object_get() / drm_mode_object_put() 管理引用计数，
 *     引用计数归零时自动调用 @free_cb 释放对象内存。
 *
 *   各动态对象提供了更高层的封装函数（不要直接调用底层 get/put）：
 *     drm_framebuffer_get() / drm_framebuffer_put()
 *     drm_connector_get()   / drm_connector_put()
 *     drm_property_blob_get() / drm_property_blob_put()
 */
struct drm_mode_object {
	/**
	 * @id: 用户空间可见的全局唯一对象标识符。
	 *
	 * 由内核在对象注册时通过 idr（整数 ID 分配器）自动分配，
	 * 在该对象的整个生命周期内不变，销毁后可能被复用。
	 * 用户空间在原子 ioctl 的属性列表中通过此 ID 引用内核对象。
	 */
	uint32_t id;

	/**
	 * @type: 对象类型标识，取值为 DRM_MODE_OBJECT_* 系列常量。
	 *
	 * 用于 drm_mode_object_find() 查找时的类型校验：
	 * 即使 ID 匹配，若类型不符也会返回 NULL，防止类型混淆攻击。
	 * 同时允许从基类指针通过 container_of 安全地转换为具体子类。
	 */
	uint32_t type;

	/**
	 * @properties: 指向此对象附加的属性集合（drm_object_properties）。
	 *
	 * drm_object_properties 内部是两个平行数组：
	 *   properties[i] → 指向第 i 个 drm_property 描述符（属性的"模板"）
	 *   values[i]     → 第 i 个属性的当前值（旧式驱动）或初始默认值（原子驱动）
	 *
	 * 最多支持 DRM_OBJECT_MAX_PROPERTY（64）个属性。
	 * 注意：对于原子驱动，可变属性的运行时值存储在 state 里，不在此数组。
	 */
	struct drm_object_properties *properties;

	/**
	 * @refcount: 引用计数（仅动态生命周期对象使用）。
	 *
	 * 仅当 @free_cb 非 NULL 时此字段有意义。
	 * 使用标准内核 kref 机制，通过 kref_get() / kref_put() 操作，
	 * 但驱动代码应调用各对象专用的封装函数（如 drm_framebuffer_get()），
	 * 不要直接操作此字段。
	 *
	 * 静态对象（@free_cb == NULL）的此字段未初始化，不可使用。
	 */
	struct kref refcount;

	/**
	 * @free_cb: 引用计数归零时的释放回调（仅动态生命周期对象设置）。
	 *
	 * NULL  → 静态生命周期对象（CRTC、Plane、Encoder），随设备销毁
	 * 非NULL → 动态生命周期对象，引用计数归零时由 kref_put() 自动调用
	 *
	 * 回调内部通常通过 container_of 获取外层完整对象指针，
	 * 再执行具体的资源释放逻辑（释放显存、解除映射、kfree 等）。
	 * 例如 drm_framebuffer 的释放回调会调用 drm_framebuffer_free()，
	 * 后者再调用驱动实现的 fb->funcs->destroy()。
	 */
	void (*free_cb)(struct kref *kref);
};

#define DRM_OBJECT_MAX_PROPERTY 64

/**
 * struct drm_object_properties - KMS 对象的属性实例表
 *
 * 这是 drm_mode_object.properties 指向的具体数据结构，
 * 存储某个 KMS 对象（CRTC/Plane/Connector）上已附加的所有属性描述符
 * 及其对应的属性值。
 *
 * 可以把它理解为一张"旋钮配置表"：
 *   properties[i] → 第 i 个旋钮的规格说明书（drm_property 描述符）
 *   values[i]     → 第 i 个旋钮当前拨到的位置（属性值）
 */
struct drm_object_properties {
	/**
	 * @count: 当前已附加的有效属性数量，不超过 DRM_OBJECT_MAX_PROPERTY(64)。
	 *
	 * drm_object_attach_property() 每次调用后此值加 1。
	 * 遍历属性时以此为上界：for (i = 0; i < obj->properties->count; i++)
	 */
	int count;

	/**
	 * @properties: 属性描述符指针数组，与 @values 一一对应。
	 *
	 * 每个元素指向一个 drm_property 对象（属性的"模板"），
	 * 定义了属性的名称、类型（范围/枚举/blob 等）和合法值域。
	 *
	 * 此数组在设备运行期间不会动态销毁属性（属性在 drm_mode_config_cleanup()
	 * 时统一销毁），因此无需担心悬空指针问题。
	 * 若未来支持动态销毁属性，则需要增加从对象反向摘除属性的逻辑。
	 */
	struct drm_property *properties[DRM_OBJECT_MAX_PROPERTY];

	/**
	 * @values: 属性值存储数组，与 @properties 一一对应。
	 *
	 * ## 旧式驱动（非原子驱动）的使用方式
	 *
	 * 属性的运行时当前值直接存储在此数组中。
	 * 读写必须通过以下 API，不能直接访问数组：
	 *   drm_object_property_set_value(obj, property, val)  → 写入
	 *   drm_object_property_get_value(obj, property, &val) → 读取
	 *
	 * ## 原子驱动的使用方式（重要区别）
	 *
	 * 原子驱动中，**可变属性**（不带 DRM_MODE_PROP_IMMUTABLE 标志）的
	 * 运行时当前值**不存在此数组**，而是存储在各自的 state 结构体中：
	 *
	 *   CRTC 的可变属性值    → drm_crtc_state 内部字段
	 *     编解码钩子：drm_crtc_funcs.atomic_get/set_property()
	 *
	 *   Plane 的可变属性值   → drm_plane_state 内部字段
	 *     编解码钩子：drm_plane_funcs.atomic_get/set_property()
	 *
	 *   Connector 的可变属性值 → drm_connector_state 内部字段
	 *     编解码钩子：drm_connector_funcs.atomic_get/set_property()
	 *
	 * 原因：属性值存在 state 里才能参与原子提交的"新旧状态对比与回滚"机制。
	 * 若存在此数组，则在原子提交校验失败时无法干净回滚到旧值。
	 *
	 * 此数组在原子驱动中只用于存储**不可变属性**（IMMUTABLE）的固定值，
	 * 例如 Connector 的 "EDID"、"PATH"（这些由内核写入，用户空间只读）。
	 *
	 * 因此，原子驱动**不应**对可变属性调用 drm_object_property_set/get_value()，
	 * 这类调用只对不可变属性有意义。
	 */
	uint64_t values[DRM_OBJECT_MAX_PROPERTY];
};

/* Avoid boilerplate.  I'm tired of typing. */
#define DRM_ENUM_NAME_FN(fnname, list)				\
	const char *fnname(int val)				\
	{							\
		int i;						\
		for (i = 0; i < ARRAY_SIZE(list); i++) {	\
			if (list[i].type == val)		\
				return list[i].name;		\
		}						\
		return "(unknown)";				\
	}

struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
					     struct drm_file *file_priv,
					     uint32_t id, uint32_t type);
void drm_mode_object_get(struct drm_mode_object *obj);
void drm_mode_object_put(struct drm_mode_object *obj);

int drm_object_property_set_value(struct drm_mode_object *obj,
				  struct drm_property *property,
				  uint64_t val);
int drm_object_property_get_value(struct drm_mode_object *obj,
				  struct drm_property *property,
				  uint64_t *value);

void drm_object_attach_property(struct drm_mode_object *obj,
				struct drm_property *property,
				uint64_t init_val);

bool drm_mode_object_lease_required(uint32_t type);
#endif
