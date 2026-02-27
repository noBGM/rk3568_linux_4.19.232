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

#ifndef __DRM_BRIDGE_H__
#define __DRM_BRIDGE_H__

#include <linux/list.h>
#include <linux/ctype.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_modes.h>

struct drm_bridge;
struct drm_bridge_timings;
struct drm_panel;

/**
 * struct drm_bridge_funcs - drm_bridge control functions
 */
struct drm_bridge_funcs {
	/**
	 * @attach:
	 *
	 * This callback is invoked whenever our bridge is being attached to a
	 * &drm_encoder.
	 *
	 * The attach callback is optional.
	 *
	 * RETURNS:
	 *
	 * Zero on success, error code on failure.
	 */
	int (*attach)(struct drm_bridge *bridge);

	/**
	 * @detach:
	 *
	 * This callback is invoked whenever our bridge is being detached from a
	 * &drm_encoder.
	 *
	 * The detach callback is optional.
	 */
	void (*detach)(struct drm_bridge *bridge);

	/**
	 * @mode_valid:
	 *
	 * This callback is used to check if a specific mode is valid in this
	 * bridge. This should be implemented if the bridge has some sort of
	 * restriction in the modes it can display. For example, a given bridge
	 * may be responsible to set a clock value. If the clock can not
	 * produce all the values for the available modes then this callback
	 * can be used to restrict the number of modes to only the ones that
	 * can be displayed.
	 *
	 * This hook is used by the probe helpers to filter the mode list in
	 * drm_helper_probe_single_connector_modes(), and it is used by the
	 * atomic helpers to validate modes supplied by userspace in
	 * drm_atomic_helper_check_modeset().
	 *
	 * This function is optional.
	 *
	 * NOTE:
	 *
	 * Since this function is both called from the check phase of an atomic
	 * commit, and the mode validation in the probe paths it is not allowed
	 * to look at anything else but the passed-in mode, and validate it
	 * against configuration-invariant hardward constraints. Any further
	 * limits which depend upon the configuration can only be checked in
	 * @mode_fixup.
	 *
	 * RETURNS:
	 *
	 * drm_mode_status Enum
	 */
	enum drm_mode_status (*mode_valid)(struct drm_bridge *bridge,
					   const struct drm_display_mode *mode);

	/**
	 * @mode_fixup:
	 *
	 * This callback is used to validate and adjust a mode. The parameter
	 * mode is the display mode that should be fed to the next element in
	 * the display chain, either the final &drm_connector or the next
	 * &drm_bridge. The parameter adjusted_mode is the input mode the bridge
	 * requires. It can be modified by this callback and does not need to
	 * match mode. See also &drm_crtc_state.adjusted_mode for more details.
	 *
	 * This is the only hook that allows a bridge to reject a modeset. If
	 * this function passes all other callbacks must succeed for this
	 * configuration.
	 *
	 * The mode_fixup callback is optional.
	 *
	 * NOTE:
	 *
	 * This function is called in the check phase of atomic modesets, which
	 * can be aborted for any reason (including on userspace's request to
	 * just check whether a configuration would be possible). Drivers MUST
	 * NOT touch any persistent state (hardware or software) or data
	 * structures except the passed in @state parameter.
	 *
	 * Also beware that userspace can request its own custom modes, neither
	 * core nor helpers filter modes to the list of probe modes reported by
	 * the GETCONNECTOR IOCTL and stored in &drm_connector.modes. To ensure
	 * that modes are filtered consistently put any bridge constraints and
	 * limits checks into @mode_valid.
	 *
	 * RETURNS:
	 *
	 * True if an acceptable configuration is possible, false if the modeset
	 * operation should be rejected.
	 */
	bool (*mode_fixup)(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
	/**
	 * @disable:
	 *
	 * This callback should disable the bridge. It is called right before
	 * the preceding element in the display pipe is disabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @disable vfunc. If the preceding element is a &drm_encoder
	 * it's called right before the &drm_encoder_helper_funcs.disable,
	 * &drm_encoder_helper_funcs.prepare or &drm_encoder_helper_funcs.dpms
	 * hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is still running when this callback is called.
	 *
	 * The disable callback is optional.
	 */
	void (*disable)(struct drm_bridge *bridge);

	/**
	 * @post_disable:
	 *
	 * This callback should disable the bridge. It is called right after the
	 * preceding element in the display pipe is disabled. If the preceding
	 * element is a bridge this means it's called after that bridge's
	 * @post_disable function. If the preceding element is a &drm_encoder
	 * it's called right after the encoder's
	 * &drm_encoder_helper_funcs.disable, &drm_encoder_helper_funcs.prepare
	 * or &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The bridge must assume that the display pipe (i.e. clocks and timing
	 * singals) feeding it is no longer running when this callback is
	 * called.
	 *
	 * The post_disable callback is optional.
	 */
	void (*post_disable)(struct drm_bridge *bridge);

	/**
	 * @mode_set:
	 *
	 * This callback should set the given mode on the bridge. It is called
	 * after the @mode_set callback for the preceding element in the display
	 * pipeline has been called already. If the bridge is the first element
	 * then this would be &drm_encoder_helper_funcs.mode_set. The display
	 * pipe (i.e.  clocks and timing signals) is off when this function is
	 * called.
	 *
	 * The adjusted_mode parameter is the mode output by the CRTC for the
	 * first bridge in the chain. It can be different from the mode
	 * parameter that contains the desired mode for the connector at the end
	 * of the bridges chain, for instance when the first bridge in the chain
	 * performs scaling. The adjusted mode is mostly useful for the first
	 * bridge in the chain and is likely irrelevant for the other bridges.
	 *
	 * For atomic drivers the adjusted_mode is the mode stored in
	 * &drm_crtc_state.adjusted_mode.
	 *
	 * NOTE:
	 *
	 * If a need arises to store and access modes adjusted for other
	 * locations than the connection between the CRTC and the first bridge,
	 * the DRM framework will have to be extended with DRM bridge states.
	 */
	void (*mode_set)(struct drm_bridge *bridge,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
	/**
	 * @pre_enable:
	 *
	 * This callback should enable the bridge. It is called right before
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called before that
	 * bridge's @pre_enable function. If the preceding element is a
	 * &drm_encoder it's called right before the encoder's
	 * &drm_encoder_helper_funcs.enable, &drm_encoder_helper_funcs.commit or
	 * &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The display pipe (i.e. clocks and timing signals) feeding this bridge
	 * will not yet be running when this callback is called. The bridge must
	 * not enable the display link feeding the next bridge in the chain (if
	 * there is one) when this callback is called.
	 *
	 * The pre_enable callback is optional.
	 */
	void (*pre_enable)(struct drm_bridge *bridge);

	/**
	 * @enable:
	 *
	 * This callback should enable the bridge. It is called right after
	 * the preceding element in the display pipe is enabled. If the
	 * preceding element is a bridge this means it's called after that
	 * bridge's @enable function. If the preceding element is a
	 * &drm_encoder it's called right after the encoder's
	 * &drm_encoder_helper_funcs.enable, &drm_encoder_helper_funcs.commit or
	 * &drm_encoder_helper_funcs.dpms hook.
	 *
	 * The bridge can assume that the display pipe (i.e. clocks and timing
	 * signals) feeding it is running when this callback is called. This
	 * callback must enable the display link feeding the next bridge in the
	 * chain if there is one.
	 *
	 * The enable callback is optional.
	 */
	void (*enable)(struct drm_bridge *bridge);
};

/**
 * struct drm_bridge_timings - timing information for the bridge
 */
struct drm_bridge_timings {
	/**
	 * @sampling_edge:
	 *
	 * Tells whether the bridge samples the digital input signal
	 * from the display engine on the positive or negative edge of the
	 * clock, this should reuse the DRM_BUS_FLAG_PIXDATA_[POS|NEG]EDGE
	 * bitwise flags from the DRM connector (bit 2 and 3 valid).
	 */
	u32 sampling_edge;
	/**
	 * @setup_time_ps:
	 *
	 * Defines the time in picoseconds the input data lines must be
	 * stable before the clock edge.
	 */
	u32 setup_time_ps;
	/**
	 * @hold_time_ps:
	 *
	 * Defines the time in picoseconds taken for the bridge to sample the
	 * input signal after the clock edge.
	 */
	u32 hold_time_ps;
};

/**
 * struct drm_bridge - DRM Bridge（显示信号桥接器）核心控制结构体
 *
 * 【Bridge 是什么？为什么需要它？】
 *
 * 在现代 SoC 显示链路中，Encoder 输出的信号往往不能直接驱动外部显示器，
 * 中间需要经过一个或多个"协议转换芯片"。Bridge 正是对这类芯片的软件抽象。
 *
 * 典型场景：
 *
 *   场景1：MIPI DSI → HDMI 转换器
 *     CRTC → Encoder(DSI) → Bridge(DSI-to-HDMI 芯片) → Connector(HDMI)
 *
 *   场景2：eDP → LVDS 转换器（两个 Bridge 串联）
 *     CRTC → Encoder(eDP) → Bridge(eDP-to-LVDS) → Bridge(LVDS-Repeater) → Connector
 *
 *   场景3：无外部转换芯片（Bridge 直接包装 Panel）
 *     CRTC → Encoder(DSI) → Bridge(drm_panel_bridge) → Panel
 *     （drm_panel_bridge 将 drm_panel 包装成 Bridge 接口，统一调用链）
 *
 * 【Bridge 链的工作原理】
 *
 * Bridge 通过 @next 指针形成单向链表，DRM helper 按顺序遍历调用：
 *
 *   启用序列（从 Encoder 侧到 Connector 侧）：
 *     encoder.pre_enable → bridge[0].pre_enable → bridge[1].pre_enable → ...
 *     encoder.enable     → bridge[0].enable     → bridge[1].enable     → ...
 *
 *   关闭序列（从 Connector 侧到 Encoder 侧，反向）：
 *     ... → bridge[1].disable     → bridge[0].disable     → encoder.disable
 *     ... → bridge[1].post_disable→ bridge[0].post_disable→ encoder.post_disable
 *
 * 这种设计保证了"上游时钟稳定后，下游才上电"的硬件时序约束。
 *
 * 【Bridge 与 Encoder/Connector 的边界】
 *
 *   Encoder：SoC 内部的信号发生器（如 DSI Host 控制器、HDMI PHY 等）
 *   Bridge： SoC 外部的信号中继/转换芯片（独立 i2c/spi 设备）
 *   Connector：最终的物理接口（HDMI 座子、FPC 连接器等）
 *
 * 在 Rockchip 平台，MIPI DSI 控制器本身就是 Encoder，
 * 若外接了 DSI-to-HDMI 桥接芯片（如 lt9611），则该芯片驱动注册为 Bridge。
 *
 * 【Bridge 的生命周期】
 *   drm_bridge_add()    ← 驱动 probe 时注册到全局链表（bridge_list）
 *   drm_bridge_attach() ← Encoder 驱动绑定时将 Bridge 挂入 encoder.bridge 链
 *   drm_bridge_remove() ← 驱动 remove 时从全局链表注销
 */
struct drm_bridge {
	/**
	 * @dev: 本 Bridge 所属的 DRM 设备
	 *
	 * 在 drm_bridge_attach() 时由 DRM 核心填入，
	 * Bridge 驱动可通过此指针访问全局 mode_config、event_lock 等资源。
	 * 注意：Bridge 驱动 probe 阶段（drm_bridge_add 之前）此指针为 NULL。
	 */
	struct drm_device *dev;

	/**
	 * @encoder: 本 Bridge 直接连接的上游 Encoder
	 *
	 * 在 drm_bridge_attach() 时填入。对于 Bridge 链中的非首个 Bridge，
	 * 此字段仍指向链头的 Encoder（而非前一个 Bridge），
	 * 因为一条 Bridge 链只服务于一个 Encoder。
	 *
	 * Bridge 驱动可通过此指针反向访问 Encoder 及其所属 CRTC，例如：
	 *   bridge->encoder->crtc  ← 找到关联的 CRTC
	 */
	struct drm_encoder *encoder;

	/**
	 * @next: Bridge 链中的下一个 Bridge（指向 Connector 侧）
	 *
	 * 【Bridge 链结构示意】
	 *
	 *   encoder.bridge ──► bridge[0] ──► bridge[1] ──► NULL
	 *                       @next          @next
	 *                    (首个Bridge)   (末尾Bridge，直接面向Connector)
	 *
	 * DRM helper（如 drm_bridge_enable()）沿 @next 链递归调用每个
	 * Bridge 的 .enable() 回调，实现链式上电。
	 *
	 * 此字段由 drm_bridge_attach() 在 previous 参数指定时自动填写：
	 *   drm_bridge_attach(encoder, new_bridge, previous_bridge)
	 *   → previous_bridge->next = new_bridge
	 */
	struct drm_bridge *next;

#ifdef CONFIG_OF
	/**
	 * @of_node: 设备树中对应的 OF 节点
	 *
	 * 用于 of_drm_find_bridge(np) 查找已注册的 Bridge：
	 * Encoder 驱动通过 DTS 中的 endpoint 找到 Bridge 的 of_node，
	 * 再通过 of_drm_find_bridge() 在全局 bridge_list 中定位对应的
	 * struct drm_bridge 实例，最终调用 drm_bridge_attach() 挂接。
	 *
	 * 典型 DTS 片段：
	 *   &dsi {
	 *       ports {
	 *           port@1 {
	 *               dsi_out: endpoint {
	 *                   remote-endpoint = <&lt9611_in>; ← 指向 Bridge 的 of_node
	 *               };
	 *           };
	 *       };
	 *   };
	 */
	struct device_node *of_node;
#endif

	/**
	 * @list: 全局 Bridge 链表节点
	 *
	 * 链入内核全局的 bridge_list（在 drm_bridge.c 中定义），
	 * 由 drm_bridge_add() 插入，drm_bridge_remove() 删除。
	 * of_drm_find_bridge() 通过遍历此链表按 of_node 查找 Bridge。
	 *
	 * 注意：此 @list 是"全局注册表"，与 Bridge 链的 @next 指针无关：
	 *   @list  → 全局注册表（所有已 probe 的 Bridge 都在这里）
	 *   @next  → 运行时链路拓扑（某条 Encoder 下挂的 Bridge 串联关系）
	 */
	struct list_head list;

	/**
	 * @timings: Bridge 的输入端时序约束（可为 NULL）
	 *
	 * 描述 Bridge 芯片对输入数字信号的物理时序要求，包含三个参数：
	 *
	 *   .sampling_edge：Bridge 在像素时钟的上升沿还是下降沿采样数据
	 *                   （DRM_BUS_FLAG_PIXDATA_POSEDGE / NEGEDGE）
	 *
	 *   .setup_time_ps：数据线在时钟沿前需要稳定的最短时间（皮秒）
	 *                   违反此约束会导致采样到错误数据（建立时间违例）
	 *
	 *   .hold_time_ps： 时钟沿后数据线需要继续保持稳定的最短时间（皮秒）
	 *                   违反此约束同样会导致采样错误（保持时间违例）
	 *
	 * Encoder 驱动在配置像素时钟和数据总线时，需参考 Bridge 的 @timings
	 * 来调整时序参数，确保满足 Bridge 芯片的硬件规格（datasheet）要求。
	 * 若 Bridge 无特殊时序要求，此字段为 NULL。
	 */
	const struct drm_bridge_timings *timings;

	/**
	 * @funcs: Bridge 控制回调函数表（struct drm_bridge_funcs）
	 *
	 * Bridge 驱动实现的核心操作，按调用时序分为三组：
	 *
	 * 【模式验证/调整阶段（atomic check 期间）】
	 *   .mode_valid(bridge, mode)
	 *      检查 Bridge 是否支持该 display mode（如时钟范围限制）。
	 *      只能检查与配置无关的硬件约束，不可访问或修改任何持久状态。
	 *
	 *   .mode_fixup(bridge, mode, adjusted_mode)
	 *      在 mode_valid 之后调用，可修改 adjusted_mode（如调整像素时钟、
	 *      行列参数）。这是 Bridge 拒绝 modeset 的唯一机会（返回 false）。
	 *      同样不可修改持久状态（check 阶段可能被取消）。
	 *
	 * 【模式设置阶段（clocks/signals 关闭时）】
	 *   .mode_set(bridge, mode, adjusted_mode)
	 *      将模式参数写入 Bridge 芯片寄存器（此时像素时钟尚未启动）。
	 *      adjusted_mode 是 CRTC 输出给链头 Bridge 的实际时序，
	 *      mode 是最终 Connector 端期望的显示模式（两者可能不同，
	 *      如链头 Bridge 做了缩放）。
	 *
	 * 【启用序列（signals 从上游往下游依次建立）】
	 *   .pre_enable(bridge)
	 *      在上游 Encoder/Bridge 使能之前调用，此时像素时钟尚未到来。
	 *      用途：给 Bridge 芯片上电、初始化 I2C 配置、使能电源轨等。
	 *      不可启用下游的数据链路（时钟还没准备好）。
	 *
	 *   .enable(bridge)
	 *      在上游 Encoder/Bridge 使能之后调用，此时像素时钟已到来、
	 *      数据信号已稳定。此时必须启用到下游 Bridge/Connector 的数据链路。
	 *      用途：使能视频输出、关闭 MUTE、发送显示开启命令等。
	 *
	 * 【关闭序列（反向，从下游往上游依次关闭）】
	 *   .disable(bridge)
	 *      在上游 Encoder/Bridge 关闭之前调用，此时像素时钟仍在运行。
	 *      用途：关闭视频输出、使能 MUTE、保存状态等。
	 *
	 *   .post_disable(bridge)
	 *      在上游 Encoder/Bridge 关闭之后调用，此时像素时钟已停止。
	 *      用途：给 Bridge 芯片下电、切断电源轨等（此时时钟已无）。
	 *
	 * 【attach/detach】
	 *   .attach(bridge)   ← drm_bridge_attach() 时触发，做初始化绑定
	 *   .detach(bridge)   ← Bridge 从 Encoder 解绑时触发，做清理
	 *
	 * 所有回调均为可选（optional），不需要的留 NULL 即可。
	 */
	const struct drm_bridge_funcs *funcs;

	/**
	 * @driver_private: Bridge 驱动的私有上下文指针
	 *
	 * Bridge 驱动将自己的私有数据结构指针存于此，
	 * 在 @funcs 回调中通过 container_of 或直接转型访问：
	 *
	 *   struct my_bridge {
	 *       struct drm_bridge bridge;  ← 内嵌，用 container_of 访问
	 *       struct i2c_client *client;
	 *       ...
	 *   };
	 *
	 * 或者：
	 *   bridge->driver_private = my_priv;  ← 外挂，直接赋值访问
	 *
	 * 两种模式均常见，取决于驱动的设计风格。
	 */
	void *driver_private;
};

void drm_bridge_add(struct drm_bridge *bridge);
void drm_bridge_remove(struct drm_bridge *bridge);
struct drm_bridge *of_drm_find_bridge(struct device_node *np);
int drm_bridge_attach(struct drm_encoder *encoder, struct drm_bridge *bridge,
		      struct drm_bridge *previous);

bool drm_bridge_mode_fixup(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode);
enum drm_mode_status drm_bridge_mode_valid(struct drm_bridge *bridge,
					   const struct drm_display_mode *mode);
void drm_bridge_disable(struct drm_bridge *bridge);
void drm_bridge_post_disable(struct drm_bridge *bridge);
void drm_bridge_mode_set(struct drm_bridge *bridge,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode);
void drm_bridge_pre_enable(struct drm_bridge *bridge);
void drm_bridge_enable(struct drm_bridge *bridge);

#ifdef CONFIG_DRM_PANEL_BRIDGE
struct drm_bridge *drm_panel_bridge_add(struct drm_panel *panel,
					u32 connector_type);
void drm_panel_bridge_remove(struct drm_bridge *bridge);
struct drm_bridge *devm_drm_panel_bridge_add(struct device *dev,
					     struct drm_panel *panel,
					     u32 connector_type);
#endif

#endif
