/*
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DRM_PANEL_H__
#define __DRM_PANEL_H__

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/notifier.h>

/* A hardware display blank change occurred */
#define DRM_PANEL_EVENT_BLANK		0x01
/* A hardware display blank early change occurred */
#define DRM_PANEL_EARLY_EVENT_BLANK	0x02

enum {
	/* panel: power on */
	DRM_PANEL_BLANK_UNBLANK,
	/* panel: power off */
	DRM_PANEL_BLANK_POWERDOWN,
};

struct drm_panel_notifier {
	int refresh_rate;
	void *data;
	uint32_t id;
};

struct device_node;
struct drm_connector;
struct drm_device;
struct drm_panel;
struct display_timing;

/**
 * @loader_protect: protect loader logo panel's power
 * struct drm_panel_funcs - perform operations on a given panel
 * @disable: disable panel (turn off back light, etc.)
 * @unprepare: turn off panel
 * @prepare: turn on panel and perform set up
 * @enable: enable panel (turn on back light, etc.)
 * @get_modes: add modes to the connector that the panel is attached to and
 * return the number of modes added
 * @get_timings: copy display timings into the provided array and return
 * the number of display timings available
 *
 * The .prepare() function is typically called before the display controller
 * starts to transmit video data. Panel drivers can use this to turn the panel
 * on and wait for it to become ready. If additional configuration is required
 * (via a control bus such as I2C, SPI or DSI for example) this is a good time
 * to do that.
 *
 * After the display controller has started transmitting video data, it's safe
 * to call the .enable() function. This will typically enable the backlight to
 * make the image on screen visible. Some panels require a certain amount of
 * time or frames before the image is displayed. This function is responsible
 * for taking this into account before enabling the backlight to avoid visual
 * glitches.
 *
 * Before stopping video transmission from the display controller it can be
 * necessary to turn off the panel to avoid visual glitches. This is done in
 * the .disable() function. Analogously to .enable() this typically involves
 * turning off the backlight and waiting for some time to make sure no image
 * is visible on the panel. It is then safe for the display controller to
 * cease transmission of video data.
 *
 * To save power when no video data is transmitted, a driver can power down
 * the panel. This is the job of the .unprepare() function.
 */
struct drm_panel_funcs {
	int (*loader_protect)(struct drm_panel *panel, bool on);
	int (*disable)(struct drm_panel *panel);
	int (*unprepare)(struct drm_panel *panel);
	int (*prepare)(struct drm_panel *panel);
	int (*enable)(struct drm_panel *panel);
	int (*get_modes)(struct drm_panel *panel);
	int (*get_timings)(struct drm_panel *panel, unsigned int num_timings,
			   struct display_timing *timings);
};

/**
 * struct drm_panel - DRM Panel（嵌入式显示面板）核心控制结构体
 *
 * 【Panel 是什么？与 Connector/Bridge 的区别】
 *
 * drm_panel 代表**内嵌式显示面板**，通常是焊接在 PCB 上、无法热插拔的屏幕，
 * 例如手机屏幕、平板屏幕、笔记本内屏、车载屏幕等。
 *
 * 三者的边界：
 *   drm_connector ← 物理接口抽象（HDMI 座子/eDP 接口），可热插拔，从外部读 EDID
 *   drm_bridge    ← 信号转换芯片（如 DSI→HDMI），在 Encoder 和 Connector 之间
 *   drm_panel     ← 嵌入式面板，无 EDID，由驱动硬编码 display mode，不可热插拔
 *
 * 【Panel 在显示链路中的位置】
 *
 *   CRTC → Encoder(DSI Host) → [Bridge(可选)] → Connector(DSI) ←→ Panel(LCD)
 *                                                      ↑
 *                                             drm_panel_attach()
 *                                             将 panel 绑定到 connector
 *
 *   Panel 挂在 Connector 的下游（物理面板侧），
 *   Connector 调用 panel->funcs->get_modes() 获取 Panel 声明的显示模式，
 *   代替了外部显示器通过 EDID 汇报自身能力的角色。
 *
 * 【Panel 的四阶段上电/下电时序】
 *
 * Panel 的生命周期由四个回调严格定序，与 Bridge 的 pre_enable/enable/
 * disable/post_disable 语义对应：
 *
 *   上电序列（时序不可颠倒）：
 *     .prepare()   ← 给 Panel 上电（AVDD/IOVCC），释放 RESET 信号，
 *                     等待面板控制器 IC 启动，可以开始发 DSI 初始化命令
 *                     （此时 CRTC/Encoder 尚未开始发送视频流）
 *     .enable()    ← CRTC 已在发送视频流，背光开启，图像变为可见
 *                     （部分 Panel 需等待若干帧后再开背光，避免闪烁）
 *
 *   下电序列（反向）：
 *     .disable()   ← 关闭背光，发送显示关闭命令（此时 CRTC 仍在发流）
 *     .unprepare() ← 停止 DSI 流后给 Panel 断电，拉低 RESET，切断电源轨
 *
 * 【Panel 注册与发现机制】
 *
 *   Panel 驱动 probe：
 *     drm_panel_init(panel)   ← 初始化结构体
 *     drm_panel_add(panel)    ← 注册到全局 panel_list
 *
 *   Connector 驱动绑定时：
 *     of_drm_find_panel(np)   ← 按 DTS of_node 在 panel_list 中查找
 *     drm_panel_attach(panel, connector) ← 建立 panel ↔ connector 双向绑定
 *       → panel->connector = connector
 *       → connector->panel  = panel
 */
struct drm_panel {
	/**
	 * @drm: 本 Panel 所属的 DRM 设备
	 *
	 * 在 drm_panel_attach() 时填入，指向 Connector 所属的 drm_device。
	 * Panel 驱动可通过此指针访问全局资源（如 mode_config）。
	 * Panel probe 阶段此指针为 NULL，attach 之前不可使用。
	 */
	struct drm_device *drm;

	/**
	 * @connector: 本 Panel 绑定的 DRM Connector
	 *
	 * 由 drm_panel_attach() 填入，建立 Panel 与 Connector 的关联。
	 * Connector 的 .get_modes() 回调通过此关联调用 panel->funcs->get_modes()，
	 * 将 Panel 驱动硬编码的 display mode 注入 Connector 的模式列表，
	 * 代替外部显示器通过 EDID 汇报能力的角色。
	 *
	 * 若 Panel 尚未 attach，此字段为 NULL。
	 * drm_panel_detach() 时此字段被清空。
	 */
	struct drm_connector *connector;

	/**
	 * @dev: Panel 的父设备（struct device）
	 *
	 * 通常是 Panel 驱动自身注册的设备（如 i2c_client->dev、
	 * spi_device->dev 或 platform_device->dev），
	 * 在 drm_panel_init() 中由驱动填入。
	 *
	 * 用途：
	 *   - 访问 Panel 的硬件资源（GPIO、regulator、PWM 背光等）
	 *   - 用于 of_drm_find_panel() 按 device_node 查找 Panel 实例
	 *   - devres 资源管理（devm_* 系列 API）
	 */
	struct device *dev;

	/**
	 * @funcs: Panel 操作回调函数表（struct drm_panel_funcs）
	 *
	 * 驱动实现的四个核心生命周期回调（顺序严格）：
	 *
	 *   .prepare()     ← 上电 + 硬件初始化（RESET 释放，DSI 初始化序列）
	 *                     调用时机：Encoder/Bridge pre_enable 之前或之后，
	 *                     由 Connector 驱动在合适的时序点调用
	 *   .enable()      ← 背光开启（视频流已稳定）
	 *   .disable()     ← 背光关闭（视频流仍在运行）
	 *   .unprepare()   ← 断电（视频流已停止）
	 *
	 * 以及两个查询回调：
	 *   .get_modes()   ← 向 Connector 注入 Panel 支持的 display mode 列表
	 *                     （硬编码在驱动中，或从 DTS 读取，不走 EDID）
	 *   .get_timings() ← 返回 display_timing 数组（部分驱动使用）
	 *
	 * Rockchip 扩展：
	 *   .loader_protect(on) ← 保护 uboot 阶段已点亮的 logo，
	 *                          防止内核驱动 probe 时意外关闭背光导致闪屏
	 */
	const struct drm_panel_funcs *funcs;

	/**
	 * @list: 全局 Panel 注册表链表节点
	 *
	 * 链入内核全局的 panel_list（在 drm_panel.c 中定义），
	 * 由 drm_panel_add() 插入，drm_panel_remove() 删除。
	 *
	 * of_drm_find_panel(np) 通过遍历此链表，
	 * 按 panel->dev->of_node 匹配查找目标 Panel 实例，
	 * 供 Connector 驱动在 bind/attach 阶段使用。
	 */
	struct list_head list;

	/**
	 * @nh: Panel 事件通知链表头（blocking notifier）
	 *
	 * Rockchip 扩展的事件通知机制，允许其他模块（如触摸屏驱动、
	 * 电源管理模块）订阅 Panel 的上下电事件：
	 *
	 *   注册监听：drm_panel_notifier_register(panel, nb)
	 *   注销监听：drm_panel_notifier_unregister(panel, nb)
	 *   触发事件：drm_panel_notifier_call_chain(panel, val, data)
	 *
	 * 事件类型（定义在本文件头部）：
	 *   DRM_PANEL_EVENT_BLANK       ← 显示进入 blank（下电完成）
	 *   DRM_PANEL_EARLY_EVENT_BLANK ← 显示即将 blank（下电开始前通知）
	 *
	 * 典型使用场景：触摸屏驱动监听此事件，在屏幕关闭时同步进入低功耗模式，
	 * 屏幕开启时恢复工作，实现触摸与显示的联动。
	 *
	 * 使用 blocking_notifier（可睡眠），适合需要等待 I2C/SPI 操作的订阅者。
	 */
	struct blocking_notifier_head nh;
};

static inline int drm_panel_loader_protect(struct drm_panel *panel, bool on)
{
	if (panel && panel->funcs && panel->funcs->loader_protect)
		return panel->funcs->loader_protect(panel, on);

	return -EINVAL;
}

/**
 * drm_disable_unprepare - power off a panel
 * @panel: DRM panel
 *
 * Calling this function will completely power off a panel (assert the panel's
 * reset, turn off power supplies, ...). After this function has completed, it
 * is usually no longer possible to communicate with the panel until another
 * call to drm_panel_prepare().
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int drm_panel_unprepare(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->unprepare)
		return panel->funcs->unprepare(panel);

	return panel ? -ENOSYS : -EINVAL;
}

/**
 * drm_panel_disable - disable a panel
 * @panel: DRM panel
 *
 * This will typically turn off the panel's backlight or disable the display
 * drivers. For smart panels it should still be possible to communicate with
 * the integrated circuitry via any command bus after this call.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int drm_panel_disable(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->disable)
		return panel->funcs->disable(panel);

	return panel ? -ENOSYS : -EINVAL;
}

/**
 * drm_panel_prepare - power on a panel
 * @panel: DRM panel
 *
 * Calling this function will enable power and deassert any reset signals to
 * the panel. After this has completed it is possible to communicate with any
 * integrated circuitry via a command bus.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int drm_panel_prepare(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->prepare)
		return panel->funcs->prepare(panel);

	return panel ? -ENOSYS : -EINVAL;
}

/**
 * drm_panel_enable - enable a panel
 * @panel: DRM panel
 *
 * Calling this function will cause the panel display drivers to be turned on
 * and the backlight to be enabled. Content will be visible on screen after
 * this call completes.
 *
 * Return: 0 on success or a negative error code on failure.
 */
static inline int drm_panel_enable(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->enable)
		return panel->funcs->enable(panel);

	return panel ? -ENOSYS : -EINVAL;
}

/**
 * drm_panel_get_modes - probe the available display modes of a panel
 * @panel: DRM panel
 *
 * The modes probed from the panel are automatically added to the connector
 * that the panel is attached to.
 *
 * Return: The number of modes available from the panel on success or a
 * negative error code on failure.
 */
static inline int drm_panel_get_modes(struct drm_panel *panel)
{
	if (panel && panel->funcs && panel->funcs->get_modes)
		return panel->funcs->get_modes(panel);

	return panel ? -ENOSYS : -EINVAL;
}

void drm_panel_init(struct drm_panel *panel);

int drm_panel_add(struct drm_panel *panel);
void drm_panel_remove(struct drm_panel *panel);

int drm_panel_attach(struct drm_panel *panel, struct drm_connector *connector);
int drm_panel_detach(struct drm_panel *panel);

int drm_panel_notifier_register(struct drm_panel *panel,
	struct notifier_block *nb);
int drm_panel_notifier_unregister(struct drm_panel *panel,
	struct notifier_block *nb);
int drm_panel_notifier_call_chain(struct drm_panel *panel,
	unsigned long val, void *v);

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
struct drm_panel *of_drm_find_panel(const struct device_node *np);
#else
static inline struct drm_panel *of_drm_find_panel(const struct device_node *np)
{
	return ERR_PTR(-ENODEV);
}
#endif

#endif
