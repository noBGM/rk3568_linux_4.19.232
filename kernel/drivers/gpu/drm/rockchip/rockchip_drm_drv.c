/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <linux/devfreq.h>
#include <linux/dma-buf-cache.h>
#include <linux/dma-mapping.h>
#include <linux/dma-iommu.h>
#include <linux/genalloc.h>
#include <linux/pm_runtime.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_gem.h"

#include "../drm_internal.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_DATE	"20140818"
#define DRIVER_MAJOR	2
#define DRIVER_MINOR	0
#define DRIVER_PATCH	0

/***********************************************************************
 *  Rockchip DRM driver version
 *
 *  v2.0.0	: add basic version for linux 4.19 rockchip drm driver(hjc)
 *
 **********************************************************************/

#if IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
static bool is_support_iommu = false;
#else
static bool is_support_iommu = true;
#endif
static struct drm_driver rockchip_drm_driver;

struct rockchip_drm_mode_set {
	struct list_head head;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_display_mode *mode;
	int clock;
	int hdisplay;
	int vdisplay;
	int vrefresh;
	int flags;
	int picture_aspect_ratio;
	int crtc_hsync_end;
	int crtc_vsync_end;

	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;

	unsigned int brightness;
	unsigned int contrast;
	unsigned int saturation;
	unsigned int hue;

	bool mode_changed;
	bool force_output;
	int ratio;
};

static DEFINE_MUTEX(rockchip_drm_sub_dev_lock);
static LIST_HEAD(rockchip_drm_sub_dev_list);

void rockchip_drm_register_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_add_tail(&sub_dev->list, &rockchip_drm_sub_dev_list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_register_sub_dev);

void rockchip_drm_unregister_sub_dev(struct rockchip_drm_sub_dev *sub_dev)
{
	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_del(&sub_dev->list);
	mutex_unlock(&rockchip_drm_sub_dev_lock);
}
EXPORT_SYMBOL(rockchip_drm_unregister_sub_dev);

struct rockchip_drm_sub_dev *rockchip_drm_get_sub_dev(struct device_node *node)
{
	struct rockchip_drm_sub_dev *sub_dev = NULL;
	bool found = false;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->of_node == node) {
			found = true;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return found ? sub_dev : NULL;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev);

int rockchip_drm_get_sub_dev_type(void)
{
	int connector_type = DRM_MODE_CONNECTOR_Unknown;
	struct rockchip_drm_sub_dev *sub_dev = NULL;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	list_for_each_entry(sub_dev, &rockchip_drm_sub_dev_list, list) {
		if (sub_dev->connector->encoder) {
			connector_type = sub_dev->connector->connector_type;
			break;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return connector_type;
}
EXPORT_SYMBOL(rockchip_drm_get_sub_dev_type);

static const struct drm_display_mode rockchip_drm_default_modes[] = {
	/* 4 - 1280x720@60Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1390,
		   1430, 1650, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 16 - 1920x1080@60Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2008,
		   2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 31 - 1920x1080@50Hz 16:9 */
	{ DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500, 1920, 2448,
		   2492, 2640, 0, 1080, 1084, 1089, 1125, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 19 - 1280x720@50Hz 16:9 */
	{ DRM_MODE("1280x720", DRM_MODE_TYPE_DRIVER, 74250, 1280, 1720,
		   1760, 1980, 0, 720, 725, 730, 750, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 0x10 - 1024x768@60Hz */
	{ DRM_MODE("1024x768", DRM_MODE_TYPE_DRIVER, 65000, 1024, 1048,
		   1184, 1344, 0,  768, 771, 777, 806, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC) },
	/* 17 - 720x576@50Hz 4:3 */
	{ DRM_MODE("720x576", DRM_MODE_TYPE_DRIVER, 27000, 720, 732,
		   796, 864, 0, 576, 581, 586, 625, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .vrefresh = 50, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
	/* 2 - 720x480@60Hz 4:3 */
	{ DRM_MODE("720x480", DRM_MODE_TYPE_DRIVER, 27000, 720, 736,
		   798, 858, 0, 480, 489, 495, 525, 0,
		   DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
	  .vrefresh = 60, .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3, },
};

int rockchip_drm_add_modes_noedid(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	int i, count, num_modes = 0;

	mutex_lock(&rockchip_drm_sub_dev_lock);
	count = ARRAY_SIZE(rockchip_drm_default_modes);

	for (i = 0; i < count; i++) {
		const struct drm_display_mode *ptr = &rockchip_drm_default_modes[i];

		mode = drm_mode_duplicate(dev, ptr);
		if (mode) {
			if (!i)
				mode->type = DRM_MODE_TYPE_PREFERRED;
			drm_mode_probed_add(connector, mode);
			num_modes++;
		}
	}
	mutex_unlock(&rockchip_drm_sub_dev_lock);

	return num_modes;
}
EXPORT_SYMBOL(rockchip_drm_add_modes_noedid);

#if !defined(CONFIG_DMABUF_CACHE)
struct drm_prime_callback_data {
	struct drm_gem_object *obj;
	struct sg_table *sgt;
};
#endif

/*
 * ============================================================
 * Loader Logo 无缝衔接机制
 * ============================================================
 *
 * 【存在的目的】
 *
 * 解决一个体验问题：从按下电源键到看到 UI 界面，中间有一段"内核初始化时间"。
 * 如果什么都不做，这段时间屏幕会黑屏（uboot 结束后 VOP 停止扫描），
 * 体验极差。
 *
 * 这套机制的目标：让屏幕从 uboot 的 logo 画面，无缝过渡到合成器的第一帧，
 * 全程不出现黑屏。
 *
 * 【为什么用 #ifndef MODULE】
 *
 * MODULE 宏由构建系统自动定义：Kconfig 中选 =y（编进内核）时不定义，
 * 选 =m（编为 .ko 模块）时自动在编译命令中加 -DMODULE。
 *
 * 整套机制有一个硬性前提：驱动必须在内核启动阶段（initcall）就位，
 * 而不是之后由用户空间 modprobe 加载。
 *
 * built-in 驱动（=y，#ifndef MODULE 生效，量产手机/平板/嵌入式）：
 *   内核启动期间 module_init 运行 → 此时 uboot 写的像素还在保留内存里
 *   → 有效，可以接管 → 全程无黑屏
 *
 *   uboot logo ─────────────────────────────▶ 合成器第一帧
 *               [屏幕持续亮着，全程无黑屏]
 *
 * 可加载模块（=m，MODULE 被定义，这段代码整体消失，桌面 Linux 发行版）：
 *   modprobe 在用户空间启动后才运行 → 距 uboot 已过数秒
 *   → uboot 保留内存可能已被覆盖，FDT 属性可能已失效，接管无意义
 *   → 中间必然出现黑屏，桌面系统用 Plymouth 等开机动画框架另行掩盖
 *
 *   uboot logo ──▶ 黑屏 ──▶ 内核跑完 ──▶ modprobe ──▶ 合成器第一帧
 *                  [这段黑屏无法避免]
 *
 * 注意：uboot 显示 logo 本身与 =y/=m 无关，uboot 自己完成。
 * #ifndef MODULE 控制的只是"内核能否无缝接管 uboot 已显示的 logo"，
 * 即消灭上图中间那段黑屏。
 *
 * 【核心设计逻辑：三方接力】
 *
 *   第一棒：uboot
 *     ① 读 DTS route 节点的 logo,uboot = "logo.bmp"，加载并解码 BMP
 *     ② 将像素数据写入 drm-logo 保留内存（这块内存内核不会动它）
 *     ③ 将图像元数据回填进 FDT（动态写入，源码里看不到）：
 *          logo,offset / logo,width / logo,height / logo,bpp
 *     ④ 将 video,clock / video,hdisplay / video,vdisplay 等时序参数
 *        也回填进 FDT（uboot 知道当前屏幕在以哪种分辨率/刷新率显示）
 *     ⑤ 让 VOP 按当前时序扫描 drm-logo 保留内存 → 屏幕显示 logo
 *
 *   间歇期：内核启动（屏幕靠 uboot 设好的寄存器状态持续扫描）
 *     rockchip_clocks_loader_protect()（arch_initcall_sync，极早期运行）：
 *       对 aclk_vop / dclk_vop 等一系列时钟执行 clk_prepare_enable，
 *       使它们的引用计数 +1，防止内核时钟管理在驱动初始化前把它们关掉，
 *       从而保证 VOP 硬件持续工作，屏幕不黑。
 *
 *   第二棒：内核 DRM 驱动（show_loader_logo，在 rockchip_drm_bind 中调用）
 *     ① init_loader_memory()：
 *          从 DTS memory-region["drm-logo"] 找到保留内存的物理地址和大小，
 *          若启用了 IOMMU 则建立 1:1 物理地址→IOVA 映射（让 VOP DMA 能访问），
 *          把这块内存的元信息存入 private->logo。
 *     ② 遍历 route 子节点（只处理 status = "okay" 的）：
 *          of_parse_display_resource()：
 *            - 读 connect 属性 → 找到对应的 drm_crtc 和 drm_connector
 *            - get_framebuffer_by_node()：
 *                读 FDT 中 uboot 回填的 logo,offset/width/height/bpp，
 *                将保留内存包装成 drm_framebuffer
 *            - 读 video,clock/hdisplay/vdisplay/vrefresh 等时序参数
 *              （uboot 回填的，代表当前屏幕实际运行的时序）
 *          setup_initial_state()：
 *            - 调用 connector 的 get_modes()，获取屏幕支持的模式列表
 *            - 找到与 uboot 当前时序完全匹配的 drm_display_mode
 *            - 将 CRTC/connector/plane 状态设置为"已激活"
 *            - 启用 loader_protect 标志，阻止驱动重新初始化硬件
 *              （关键：此时硬件已在工作，不能重置，否则会闪屏）
 *     ③ drm_atomic_helper_swap_state()：
 *          将上述构造的状态作为 old_state 注入 DRM 状态机，
 *          让 DRM 框架"认为"这是当前硬件已有的状态——
 *          这样合成器来的第一次原子提交只会做增量更新，
 *          不会触发全量的 modeset（重新配时序），避免闪屏。
 *     ④ drm_atomic_commit()：
 *          提交一次原子操作，正式让 DRM 框架接管 VOP 对保留内存的扫描，
 *          此后合成器可以在任意 VBlank 切换 framebuffer。
 *     ⑤ rockchip_free_loader_memory()：
 *          IOMMU unmap + 释放保留内存 → 内核可以将这块内存归还给内存管理器。
 *          （在合成器第一帧提交后才真正调用，确保切换完成后再释放）
 *
 *   间歇期结束：
 *     rockchip_clocks_loader_unprotect()（late_initcall_sync）：
 *       对之前保护的时钟执行 clk_disable_unprepare，
 *       撤销 loader_protect 阶段的引用计数 +1，
 *       将时钟控制权完全交还给正常的时钟管理框架。
 *
 *   第三棒：合成器（weston / SurfaceFlinger）
 *     open /dev/dri/card0 → 探测 connector → 分配新 framebuffer
 *     → 原子提交第一帧 → VOP 切换到新 framebuffer → 屏幕显示 UI
 *     整个过程因为 old_state 的存在，DRM 框架知道"不需要重新 modeset"，
 *     VOP 输出时序全程不中断，用户看不到任何黑屏或闪烁。
 *
 * 【loader_protect 标志的作用】
 *
 * setup_initial_state() 会对 connector/encoder/crtc 设置 loader_protect = true。
 * 这个标志告诉各子驱动的 enable/disable 回调：
 * "硬件现在已经在工作，你不要重新初始化（不要发 DSI 初始化序列、不要重新上电）"
 * 直到合成器第一次真正做 modeset 时，loader_protect 才被清除，
 * 驱动才开始正常的 enable/disable 流程。
 * ============================================================
 */
#ifndef MODULE
static struct drm_crtc *find_crtc_by_node(struct drm_device *drm_dev, struct device_node *node)
{
	struct device_node *np_crtc;
	struct drm_crtc *crtc;

	np_crtc = of_get_parent(node);
	if (!np_crtc || !of_device_is_available(np_crtc))
		return NULL;

	drm_for_each_crtc(crtc, drm_dev) {
		if (crtc->port == np_crtc)
			return crtc;
	}

	return NULL;
}

static struct drm_connector *find_connector_by_node(struct drm_device *drm_dev,
						    struct device_node *node)
{
	struct device_node *np_connector;
	struct rockchip_drm_sub_dev *sub_dev;

	np_connector = of_graph_get_remote_port_parent(node);
	if (!np_connector || !of_device_is_available(np_connector))
		return NULL;

	sub_dev = rockchip_drm_get_sub_dev(np_connector);
	if (!sub_dev)
		return NULL;

	return sub_dev->connector;
}

static struct drm_connector *find_connector_by_bridge(struct drm_device *drm_dev,
						      struct device_node *node)
{
	struct device_node *np_encoder, *np_connector = NULL;
	struct drm_connector *connector = NULL;
	struct device_node *port, *endpoint;
	struct rockchip_drm_sub_dev *sub_dev;

	np_encoder = of_graph_get_remote_port_parent(node);
	if (!np_encoder || !of_device_is_available(np_encoder))
		goto err_put_encoder;

	port = of_graph_get_port_by_id(np_encoder, 1);
	if (!port) {
		dev_err(drm_dev->dev, "can't found port point!\n");
		goto err_put_encoder;
	}

	for_each_child_of_node(port, endpoint) {
		np_connector = of_graph_get_remote_port_parent(endpoint);
		if (!np_connector) {
			dev_err(drm_dev->dev,
				"can't found connector node, please init!\n");
			goto err_put_port;
		}
		if (!of_device_is_available(np_connector)) {
			of_node_put(np_connector);
			np_connector = NULL;
			continue;
		} else {
			break;
		}
	}
	if (!np_connector) {
		dev_err(drm_dev->dev, "can't found available connector node!\n");
		goto err_put_port;
	}

	sub_dev = rockchip_drm_get_sub_dev(np_connector);
	if (!sub_dev)
		goto err_put_port;
	connector = sub_dev->connector;

	of_node_put(np_connector);
err_put_port:
	of_node_put(port);
err_put_encoder:
	of_node_put(np_encoder);

	return connector;
}

void rockchip_free_loader_memory(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct rockchip_logo *logo;
	void *start, *end;

	if (!private || !private->logo || --private->logo->count)
		return;

	logo = private->logo;
	start = phys_to_virt(logo->dma_addr);
	end = phys_to_virt(logo->dma_addr + logo->size);

	if (private->domain) {
		u32 pg_size = 1UL << __ffs(private->domain->pgsize_bitmap);

		iommu_unmap(private->domain, logo->dma_addr, ALIGN(logo->size, pg_size));
	}

	memblock_free(logo->start, logo->size);
	free_reserved_area(start, end, -1, "drm_logo");
	kfree(logo);
	private->logo = NULL;
	private->loader_protect = false;
}

static int init_loader_memory(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_logo *logo;
	struct device_node *np = drm_dev->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	u32 pg_size = PAGE_SIZE;
	struct resource res;
	int ret, idx;

	idx = of_property_match_string(np, "memory-region-names", "drm-logo");
	if (idx >= 0)
		node = of_parse_phandle(np, "memory-region", idx);
	else
		node = of_parse_phandle(np, "logo-memory-region", 0);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	if (private->domain)
		pg_size = 1UL << __ffs(private->domain->pgsize_bitmap);
	start = ALIGN_DOWN(res.start, pg_size);
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	logo = kmalloc(sizeof(*logo), GFP_KERNEL);
	if (!logo)
		return -ENOMEM;

	logo->kvaddr = phys_to_virt(start);

	if (private->domain) {
		ret = iommu_map(private->domain, start, start, ALIGN(size, pg_size),
				IOMMU_WRITE | IOMMU_READ);
		if (ret) {
			dev_err(drm_dev->dev, "failed to create 1v1 mapping\n");
			goto err_free_logo;
		}
	}

	logo->dma_addr = start;
	logo->size = size;
	logo->count = 1;
	private->logo = logo;

	idx = of_property_match_string(np, "memory-region-names", "drm-cubic-lut");
	if (idx < 0)
		return 0;

	node = of_parse_phandle(np, "memory-region", idx);
	if (!node)
		return -ENOMEM;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = ALIGN_DOWN(res.start, pg_size);
	size = resource_size(&res);
	if (!size)
		return 0;

	private->cubic_lut_kvaddr = phys_to_virt(start);
	if (private->domain) {
		ret = iommu_map(private->domain, start, start, ALIGN(size, pg_size),
				IOMMU_WRITE | IOMMU_READ);
		if (ret) {
			dev_err(drm_dev->dev, "failed to create 1v1 mapping for cubic lut\n");
			goto err_free_logo;
		}
	}
	private->cubic_lut_dma_addr = start;

	return 0;

err_free_logo:
	kfree(logo);

	return ret;
}

static struct drm_framebuffer *
get_framebuffer_by_node(struct drm_device *drm_dev, struct device_node *node)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	u32 val;
	int bpp;

	if (WARN_ON(!private->logo))
		return NULL;

	if (of_property_read_u32(node, "logo,offset", &val)) {
		pr_err("%s: failed to get logo,offset\n", __func__);
		return NULL;
	}
	mode_cmd.offsets[0] = val;

	if (of_property_read_u32(node, "logo,width", &val)) {
		pr_err("%s: failed to get logo,width\n", __func__);
		return NULL;
	}
	mode_cmd.width = val;

	if (of_property_read_u32(node, "logo,height", &val)) {
		pr_err("%s: failed to get logo,height\n", __func__);
		return NULL;
	}
	mode_cmd.height = val;

	if (of_property_read_u32(node, "logo,bpp", &val)) {
		pr_err("%s: failed to get logo,bpp\n", __func__);
		return NULL;
	}
	bpp = val;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * bpp, 32) / 8;

	switch (bpp) {
	case 16:
		mode_cmd.pixel_format = DRM_FORMAT_RGB565;
		break;
	case 24:
		mode_cmd.pixel_format = DRM_FORMAT_RGB888;
		break;
	case 32:
		mode_cmd.pixel_format = DRM_FORMAT_XRGB8888;
		break;
	default:
		pr_err("%s: unsupported to logo bpp %d\n", __func__, bpp);
		return NULL;
	}

	return rockchip_fb_alloc(drm_dev, &mode_cmd, NULL, private->logo, 1);
}

static struct rockchip_drm_mode_set *
of_parse_display_resource(struct drm_device *drm_dev, struct device_node *route)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_drm_mode_set *set;
	struct device_node *connect;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	const char *string;
	u32 val;

	connect = of_parse_phandle(route, "connect", 0);
	if (!connect)
		return NULL;

	fb = get_framebuffer_by_node(drm_dev, route);
	if (IS_ERR_OR_NULL(fb))
		return NULL;

	crtc = find_crtc_by_node(drm_dev, connect);
	connector = find_connector_by_node(drm_dev, connect);
	if (!connector)
		connector = find_connector_by_bridge(drm_dev, connect);
	if (!crtc || !connector) {
		dev_warn(drm_dev->dev,
			 "No available crtc or connector for display");
		drm_framebuffer_put(fb);
		return NULL;
	}

	set = kzalloc(sizeof(*set), GFP_KERNEL);
	if (!set)
		return NULL;

	if (!of_property_read_u32(route, "video,clock", &val))
		set->clock = val;

	if (!of_property_read_u32(route, "video,hdisplay", &val))
		set->hdisplay = val;

	if (!of_property_read_u32(route, "video,vdisplay", &val))
		set->vdisplay = val;

	if (!of_property_read_u32(route, "video,crtc_hsync_end", &val))
		set->crtc_hsync_end = val;

	if (!of_property_read_u32(route, "video,crtc_vsync_end", &val))
		set->crtc_vsync_end = val;

	if (!of_property_read_u32(route, "video,vrefresh", &val))
		set->vrefresh = val;

	if (!of_property_read_u32(route, "video,flags", &val))
		set->flags = val;

	if (!of_property_read_u32(route, "video,aspect_ratio", &val))
		set->picture_aspect_ratio = val;

	if (!of_property_read_u32(route, "overscan,left_margin", &val))
		set->left_margin = val;

	if (!of_property_read_u32(route, "overscan,right_margin", &val))
		set->right_margin = val;

	if (!of_property_read_u32(route, "overscan,top_margin", &val))
		set->top_margin = val;

	if (!of_property_read_u32(route, "overscan,bottom_margin", &val))
		set->bottom_margin = val;

	if (!of_property_read_u32(route, "bcsh,brightness", &val))
		set->brightness = val;
	else
		set->brightness = 50;

	if (!of_property_read_u32(route, "bcsh,contrast", &val))
		set->contrast = val;
	else
		set->contrast = 50;

	if (!of_property_read_u32(route, "bcsh,saturation", &val))
		set->saturation = val;
	else
		set->saturation = 50;

	if (!of_property_read_u32(route, "bcsh,hue", &val))
		set->hue = val;
	else
		set->hue = 50;

	set->force_output = of_property_read_bool(route, "force-output");

	if (!of_property_read_u32(route, "cubic_lut,offset", &val)) {
		private->cubic_lut[crtc->index].enable = true;
		private->cubic_lut[crtc->index].offset = val;
	}

	set->ratio = 1;
	if (!of_property_read_string(route, "logo,mode", &string) &&
	    !strcmp(string, "fullscreen"))
		set->ratio = 0;

	set->fb = fb;
	set->crtc = crtc;
	set->connector = connector;

	return set;
}

static int rockchip_drm_fill_connector_modes(struct drm_connector *connector,
					     uint32_t maxX, uint32_t maxY,
					     bool force_output)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	const struct drm_connector_helper_funcs *connector_funcs =
		connector->helper_private;
	int count = 0;
	bool verbose_prune = true;
	enum drm_connector_status old_status;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n", connector->base.id,
		      connector->name);
	/* set all modes to the unverified state */
	list_for_each_entry(mode, &connector->modes, head)
		mode->status = MODE_STALE;

	if (force_output)
		connector->force = DRM_FORCE_ON;
	if (connector->force) {
		if (connector->force == DRM_FORCE_ON ||
		    connector->force == DRM_FORCE_ON_DIGITAL)
			connector->status = connector_status_connected;
		else
			connector->status = connector_status_disconnected;
		if (connector->funcs->force)
			connector->funcs->force(connector);
	} else {
		old_status = connector->status;

		if (connector->funcs->detect)
			connector->status = connector->funcs->detect(connector, true);
		else
			connector->status  = connector_status_connected;
		/*
		 * Normally either the driver's hpd code or the poll loop should
		 * pick up any changes and fire the hotplug event. But if
		 * userspace sneaks in a probe, we might miss a change. Hence
		 * check here, and if anything changed start the hotplug code.
		 */
		if (old_status != connector->status) {
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
				      connector->base.id,
				      connector->name,
				      old_status, connector->status);

			/*
			 * The hotplug event code might call into the fb
			 * helpers, and so expects that we do not hold any
			 * locks. Fire up the poll struct instead, it will
			 * disable itself again.
			 */
			dev->mode_config.delayed_event = true;
			if (dev->mode_config.poll_enabled)
				schedule_delayed_work(&dev->mode_config.output_poll_work,
						      0);
		}
	}

	/* Re-enable polling in case the global poll config changed. */
	if (!dev->mode_config.poll_running)
		drm_kms_helper_poll_enable(dev);

	dev->mode_config.poll_running = true;

	if (connector->status == connector_status_disconnected) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] disconnected\n",
			      connector->base.id, connector->name);
		drm_connector_update_edid_property(connector, NULL);
		verbose_prune = false;
		goto prune;
	}

	count = (*connector_funcs->get_modes)(connector);

	if (count == 0 && connector->status == connector_status_connected)
		count = drm_add_modes_noedid(connector, 1024, 768);
	if (force_output)
		count += rockchip_drm_add_modes_noedid(connector);
	if (count == 0)
		goto prune;

	drm_connector_list_update(connector);

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_driver(dev, mode);

		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_size(mode, maxX, maxY);

		/**
		 * if (mode->status == MODE_OK)
		 *	mode->status = drm_mode_validate_flag(mode, mode_flags);
		 */
		if (mode->status == MODE_OK && connector_funcs->mode_valid)
			mode->status = connector_funcs->mode_valid(connector,
								   mode);
		if (mode->status == MODE_OK)
			mode->status = drm_mode_validate_ycbcr420(mode,
								  connector);
	}

prune:
	drm_mode_prune_invalid(dev, &connector->modes, verbose_prune);

	if (list_empty(&connector->modes))
		return 0;

	list_for_each_entry(mode, &connector->modes, head)
		mode->vrefresh = drm_mode_vrefresh(mode);

	drm_mode_sort(&connector->modes);

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] probed modes :\n", connector->base.id,
		      connector->name);
	list_for_each_entry(mode, &connector->modes, head) {
		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);
		drm_mode_debug_printmodeline(mode);
	}

	return count;
}

static int setup_initial_state(struct drm_device *drm_dev,
			       struct drm_atomic_state *state,
			       struct rockchip_drm_mode_set *set)
{
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_connector *connector = set->connector;
	struct drm_crtc *crtc = set->crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_plane_state *primary_state;
	struct drm_display_mode *mode = NULL;
	const struct drm_connector_helper_funcs *funcs;
	const struct drm_encoder_helper_funcs *encoder_funcs;
	int pipe = drm_crtc_index(crtc);
	bool is_crtc_enabled = true;
	int hdisplay, vdisplay;
	int fb_width, fb_height;
	int found = 0, match = 0;
	int num_modes;
	int ret = 0;
	struct rockchip_crtc_state *s = NULL;

	if (!set->hdisplay || !set->vdisplay || !set->vrefresh)
		is_crtc_enabled = false;

	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);

	funcs = connector->helper_private;

	if (funcs->best_encoder)
		conn_state->best_encoder = funcs->best_encoder(connector);
	else
		conn_state->best_encoder = drm_atomic_helper_best_encoder(connector);

	if (funcs->loader_protect)
		funcs->loader_protect(connector, true);
	connector->loader_protect = true;
	encoder_funcs = conn_state->best_encoder->helper_private;
	if (encoder_funcs->loader_protect)
		encoder_funcs->loader_protect(conn_state->best_encoder, true);
	conn_state->best_encoder->loader_protect = true;
	num_modes = rockchip_drm_fill_connector_modes(connector, 4096, 4096, set->force_output);
	if (!num_modes) {
		dev_err(drm_dev->dev, "connector[%s] can't found any modes\n",
			connector->name);
		ret = -EINVAL;
		goto error_conn;
	}

	list_for_each_entry(mode, &connector->modes, head) {
		if (mode->clock == set->clock &&
		    mode->hdisplay == set->hdisplay &&
		    mode->vdisplay == set->vdisplay &&
		    mode->crtc_hsync_end == set->crtc_hsync_end &&
		    mode->crtc_vsync_end == set->crtc_vsync_end &&
		    drm_mode_vrefresh(mode) == set->vrefresh &&
		    /* we just need to focus on DRM_MODE_FLAG_ALL flag, so here
		     * we compare mode->flags with set->flags & DRM_MODE_FLAG_ALL.
		     */
		    mode->flags == (set->flags & DRM_MODE_FLAG_ALL) &&
		    mode->picture_aspect_ratio == set->picture_aspect_ratio) {
			found = 1;
			match = 1;
			break;
		}
	}

	if (!found) {
		ret = -EINVAL;
		connector->status = connector_status_disconnected;
		goto error_conn;
	}

	conn_state->tv.brightness = set->brightness;
	conn_state->tv.contrast = set->contrast;
	conn_state->tv.saturation = set->saturation;
	conn_state->tv.hue = set->hue;
	set->mode = mode;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto error_conn;
	}

	drm_mode_copy(&crtc_state->adjusted_mode, mode);
	if (!match || !is_crtc_enabled) {
		set->mode_changed = true;
	} else {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			goto error_conn;

		mode->picture_aspect_ratio = HDMI_PICTURE_ASPECT_NONE;
		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			goto error_conn;

		crtc_state->active = true;

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->loader_protect)
			priv->crtc_funcs[pipe]->loader_protect(crtc, true);
	}

	if (!set->fb) {
		ret = 0;
		goto error_crtc;
	}
	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state)) {
		ret = PTR_ERR(primary_state);
		goto error_crtc;
	}

	hdisplay = mode->hdisplay;
	vdisplay = mode->vdisplay;
	fb_width = set->fb->width;
	fb_height = set->fb->height;

	primary_state->crtc = crtc;
	primary_state->src_x = 0;
	primary_state->src_y = 0;
	primary_state->src_w = fb_width << 16;
	primary_state->src_h = fb_height << 16;
	if (set->ratio) {
		if (set->fb->width >= hdisplay) {
			primary_state->crtc_x = 0;
			primary_state->crtc_w = hdisplay;
		} else {
			primary_state->crtc_x = (hdisplay - fb_width) / 2;
			primary_state->crtc_w = set->fb->width;
		}

		if (set->fb->height >= vdisplay) {
			primary_state->crtc_y = 0;
			primary_state->crtc_h = vdisplay;
		} else {
			primary_state->crtc_y = (vdisplay - fb_height) / 2;
			primary_state->crtc_h = fb_height;
		}
	} else {
		primary_state->crtc_x = 0;
		primary_state->crtc_y = 0;
		primary_state->crtc_w = hdisplay;
		primary_state->crtc_h = vdisplay;
	}
	s = to_rockchip_crtc_state(crtc->state);
	s->output_type = connector->connector_type;

	return 0;

error_crtc:
	if (priv->crtc_funcs[pipe] && priv->crtc_funcs[pipe]->loader_protect)
		priv->crtc_funcs[pipe]->loader_protect(crtc, false);
error_conn:
	if (funcs->loader_protect)
		funcs->loader_protect(connector, false);
	connector->loader_protect = false;
	if (encoder_funcs->loader_protect)
		encoder_funcs->loader_protect(conn_state->best_encoder, false);
	conn_state->best_encoder->loader_protect = false;

	return ret;
}

static int update_state(struct drm_device *drm_dev,
			struct drm_atomic_state *state,
			struct rockchip_drm_mode_set *set,
			unsigned int *plane_mask)
{
	struct drm_crtc *crtc = set->crtc;
	struct drm_connector *connector = set->connector;
	struct drm_display_mode *mode = set->mode;
	struct drm_plane_state *primary_state;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	int ret;
	struct rockchip_crtc_state *s;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);
	conn_state = drm_atomic_get_connector_state(state, connector);
	if (IS_ERR(conn_state))
		return PTR_ERR(conn_state);
	s = to_rockchip_crtc_state(crtc_state);
	s->left_margin = set->left_margin;
	s->right_margin = set->right_margin;
	s->top_margin = set->top_margin;
	s->bottom_margin = set->bottom_margin;

	if (set->mode_changed) {
		ret = drm_atomic_set_crtc_for_connector(conn_state, crtc);
		if (ret)
			return ret;

		ret = drm_atomic_set_mode_for_crtc(crtc_state, mode);
		if (ret)
			return ret;

		crtc_state->active = true;
	} else {
		const struct drm_encoder_helper_funcs *encoder_helper_funcs;
		const struct drm_connector_helper_funcs *connector_helper_funcs;
		struct drm_encoder *encoder;

		connector_helper_funcs = connector->helper_private;
		if (!connector_helper_funcs)
			return -ENXIO;
		if (connector_helper_funcs->best_encoder)
			encoder = connector_helper_funcs->best_encoder(connector);
		else
			encoder = drm_atomic_helper_best_encoder(connector);
		if (!encoder)
			return -ENXIO;
		encoder_helper_funcs = encoder->helper_private;
		if (!encoder_helper_funcs->atomic_check)
			return -ENXIO;
		ret = encoder_helper_funcs->atomic_check(encoder, crtc->state,
							 conn_state);
		if (ret)
			return ret;

		if (encoder_helper_funcs->atomic_mode_set)
			encoder_helper_funcs->atomic_mode_set(encoder,
							      crtc_state,
							      conn_state);
		else if (encoder_helper_funcs->mode_set)
			encoder_helper_funcs->mode_set(encoder, mode, mode);
	}

	primary_state = drm_atomic_get_plane_state(state, crtc->primary);
	if (IS_ERR(primary_state))
		return PTR_ERR(primary_state);

	crtc_state->plane_mask = 1 << drm_plane_index(crtc->primary);
	*plane_mask |= crtc_state->plane_mask;

	drm_atomic_set_fb_for_plane(primary_state, set->fb);
	drm_framebuffer_put(set->fb);
	ret = drm_atomic_set_crtc_for_plane(primary_state, crtc);

	return ret;
}

static void show_loader_logo(struct drm_device *drm_dev)
{
	struct drm_atomic_state *state, *old_state;
	struct device_node *np = drm_dev->dev->of_node;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct device_node *root, *route;
	struct rockchip_drm_mode_set *set, *tmp, *unset;
	struct list_head mode_set_list;
	struct list_head mode_unset_list;
	unsigned int plane_mask = 0;
	int ret, i;

	root = of_get_child_by_name(np, "route");
	if (!root) {
		dev_warn(drm_dev->dev, "failed to parse display resources\n");
		return;
	}

	if (init_loader_memory(drm_dev)) {
		dev_warn(drm_dev->dev, "failed to parse loader memory\n");
		return;
	}

	INIT_LIST_HEAD(&mode_set_list);
	INIT_LIST_HEAD(&mode_unset_list);
	drm_modeset_lock_all(drm_dev);
	state = drm_atomic_state_alloc(drm_dev);
	if (!state) {
		dev_err(drm_dev->dev, "failed to alloc atomic state\n");
		ret = -ENOMEM;
		goto err_unlock;
	}

	state->acquire_ctx = mode_config->acquire_ctx;

	for_each_child_of_node(root, route) {
		if (!of_device_is_available(route))
			continue;

		set = of_parse_display_resource(drm_dev, route);
		if (!set)
			continue;

		if (setup_initial_state(drm_dev, state, set)) {
			drm_framebuffer_put(set->fb);
			INIT_LIST_HEAD(&set->head);
			list_add_tail(&set->head, &mode_unset_list);
			continue;
		}
		INIT_LIST_HEAD(&set->head);
		list_add_tail(&set->head, &mode_set_list);
	}

	/*
	 * the mode_unset_list store the unconnected route, if route's crtc
	 * isn't used, we should close it.
	 */
	list_for_each_entry_safe(unset, tmp, &mode_unset_list, head) {
		struct rockchip_drm_mode_set *tmp_set;
		int find_used_crtc = 0;

		list_for_each_entry_safe(set, tmp_set, &mode_set_list, head) {
			if (set->crtc == unset->crtc) {
				find_used_crtc = 1;
				continue;
			}
		}

		if (!find_used_crtc) {
			struct drm_crtc *crtc = unset->crtc;
			int pipe = drm_crtc_index(crtc);
			struct rockchip_drm_private *priv =
							drm_dev->dev_private;

			if (unset->hdisplay && unset->vdisplay) {
				if (priv->crtc_funcs[pipe] &&
				    priv->crtc_funcs[pipe]->loader_protect)
					priv->crtc_funcs[pipe]->loader_protect(crtc, true);
				priv->crtc_funcs[pipe]->crtc_close(crtc);
				if (priv->crtc_funcs[pipe] &&
				    priv->crtc_funcs[pipe]->loader_protect)
					priv->crtc_funcs[pipe]->loader_protect(crtc, false);
			}
		}

		list_del(&unset->head);
		kfree(unset);
	}

	if (list_empty(&mode_set_list)) {
		dev_warn(drm_dev->dev, "can't not find any loader display\n");
		ret = -ENXIO;
		goto err_free_state;
	}

	/*
	 * The state save initial devices status, swap the state into
	 * drm devices as old state, so if new state come, can compare
	 * with this state to judge which status need to update.
	 */
	WARN_ON(drm_atomic_helper_swap_state(state, false));
	drm_atomic_state_put(state);
	old_state = drm_atomic_helper_duplicate_state(drm_dev,
						      mode_config->acquire_ctx);
	if (IS_ERR(old_state)) {
		dev_err(drm_dev->dev, "failed to duplicate atomic state\n");
		ret = PTR_ERR_OR_ZERO(old_state);
		goto err_free_state;
	}

	state = drm_atomic_helper_duplicate_state(drm_dev,
						  mode_config->acquire_ctx);
	if (IS_ERR(state)) {
		dev_err(drm_dev->dev, "failed to duplicate atomic state\n");
		ret = PTR_ERR_OR_ZERO(state);
		goto err_free_old_state;
	}
	state->acquire_ctx = mode_config->acquire_ctx;
	list_for_each_entry(set, &mode_set_list, head)
		/*
		 * We don't want to see any fail on update_state.
		 */
		WARN_ON(update_state(drm_dev, state, set, &plane_mask));

	for (i = 0; i < state->num_connector; i++) {
		if (state->connectors[i].new_state->connector->status !=
		    connector_status_connected)
			state->connectors[i].new_state->best_encoder = NULL;
	}

	ret = drm_atomic_commit(state);
	/**
	 * todo
	 * drm_atomic_clean_old_fb(drm_dev, plane_mask, ret);
	 */

	list_for_each_entry_safe(set, tmp, &mode_set_list, head) {
		if (set->force_output)
			set->connector->force = DRM_FORCE_UNSPECIFIED;
		list_del(&set->head);
		kfree(set);
	}

	/*
	 * Is possible get deadlock here?
	 */
	WARN_ON(ret == -EDEADLK);

	if (ret) {
		/*
		 * restore display status if atomic commit failed.
		 */
		WARN_ON(drm_atomic_helper_swap_state(old_state, false));
		goto err_free_state;
	}

	rockchip_free_loader_memory(drm_dev);
	drm_atomic_state_put(old_state);
	drm_atomic_state_put(state);

	private->loader_protect = true;
	drm_modeset_unlock_all(drm_dev);
	return;
err_free_old_state:
	drm_atomic_state_put(old_state);
err_free_state:
	drm_atomic_state_put(state);
err_unlock:
	drm_modeset_unlock_all(drm_dev);
	if (ret)
		dev_err(drm_dev->dev, "failed to show loader logo\n");
}

static const char *const loader_protect_clocks[] __initconst = {
	"hclk_vio",
	"hclk_vop",
	"hclk_vopb",
	"hclk_vopl",
	"aclk_vio",
	"aclk_vio0",
	"aclk_vio1",
	"aclk_vop",
	"aclk_vopb",
	"aclk_vopl",
	"aclk_vo_pre",
	"aclk_vio_pre",
	"dclk_vop",
	"dclk_vop0",
	"dclk_vop1",
	"dclk_vopb",
	"dclk_vopl",
};

static struct clk **loader_clocks __initdata;
static int __init rockchip_clocks_loader_protect(void)
{
	int nclocks = ARRAY_SIZE(loader_protect_clocks);
	struct clk *clk;
	int i;

	loader_clocks = kcalloc(nclocks, sizeof(void *), GFP_KERNEL);
	if (!loader_clocks)
		return -ENOMEM;

	for (i = 0; i < nclocks; i++) {
		clk = __clk_lookup(loader_protect_clocks[i]);

		if (clk) {
			loader_clocks[i] = clk;
			clk_prepare_enable(clk);
		}
	}

	return 0;
}
arch_initcall_sync(rockchip_clocks_loader_protect);

static int __init rockchip_clocks_loader_unprotect(void)
{
	int i;

	if (!loader_clocks)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(loader_protect_clocks); i++) {
		struct clk *clk = loader_clocks[i];

		if (clk)
			clk_disable_unprepare(clk);
	}
	kfree(loader_clocks);

	return 0;
}
late_initcall_sync(rockchip_clocks_loader_unprotect);
#endif

int rockchip_drm_crtc_send_mcu_cmd(struct drm_device *drm_dev,
				   struct device_node *np_crtc,
				   u32 type, u32 value)
{
	struct drm_crtc *crtc;
	int pipe = 0;
	struct rockchip_drm_private *priv;

	if (!np_crtc || !of_device_is_available(np_crtc))
		return -EINVAL;

	drm_for_each_crtc(crtc, drm_dev) {
		if (of_get_parent(crtc->port) == np_crtc)
			break;
	}

	pipe = drm_crtc_index(crtc);
	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;
	priv = crtc->dev->dev_private;
	if (priv->crtc_funcs[pipe]->crtc_send_mcu_cmd)
		priv->crtc_funcs[pipe]->crtc_send_mcu_cmd(crtc, type, value);

	return 0;
}
EXPORT_SYMBOL(rockchip_drm_crtc_send_mcu_cmd);

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	int ret;

	if (!is_support_iommu)
		return 0;

	ret = iommu_attach_device(private->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain *domain = private->domain;

	if (!is_support_iommu)
		return;

	iommu_detach_device(domain, dev);
}

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return -EINVAL;

	priv->crtc_funcs[pipe] = crtc_funcs;

	return 0;
}

void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc)
{
	int pipe = drm_crtc_index(crtc);
	struct rockchip_drm_private *priv = crtc->dev->dev_private;

	if (pipe >= ROCKCHIP_MAX_CRTC)
		return;

	priv->crtc_funcs[pipe] = NULL;
}

/**
 * rockchip_drm_fault_handler - IOMMU page fault 回调，用于事后诊断
 * @iommu: 触发 fault 的 IOMMU domain
 * @dev:   触发 fault 的主设备（此处为 VOP2 的 platform_device）
 * @iova:  触发 fault 的虚拟地址（IOVA，即 DMA 地址）
 * @flags: fault 类型掩码，来自 <linux/iommu.h>：
 *           IOMMU_FAULT_READ        (bit0) VOP2 读取时触发（扫帧 DMA 最常见）
 *           IOMMU_FAULT_WRITE       (bit1) VOP2 写入时触发（Writeback 场景）
 *           IOMMU_FAULT_TRANSLATION (bit2) IOVA 未建立页表映射（最典型：buffer 已 unmap）
 *           IOMMU_FAULT_PERMISSION  (bit3) 页表存在但权限不足（如只读页被写）
 *           IOMMU_FAULT_EXTERNAL    (bit4) 外部硬件总线错误（非正常页表 miss）
 *           IOMMU_FAULT_TRANSACTION_STALLED (bit5) 事务被 IOMMU 挂起等待处理
 * @arg:   注册时传入的私有数据，即 drm_device 指针
 *
 * ## 触发时机
 *
 * VOP2 通过 AXI 总线向 IOMMU 发起 DMA 读请求（取帧缓冲像素数据），
 * IOMMU 在 page table walk 中发现该 IOVA 没有有效映射，
 * 硬件产生 fault 中断，驱动层注册的此函数被调用。
 *
 * 常见触发根因（结合 flags 分析）：
 *
 *   ① TRANSLATION fault（最频繁）：
 *      GEM buffer 的 IOVA 已通过 iommu_unmap() 删除，但 VOP2 的扫描地址寄存器
 *      还没更新到新帧（等待下一个 VBlank 锁存），导致硬件访问已失效的 IOVA。
 *      典型场景：disable CRTC 流程与 VOP2 硬件 DMA 之间的竞态窗口。
 *
 *   ② PERMISSION fault：
 *      IOVA 映射存在，但映射时指定了只读（IOMMU_READ），而 VOP2 Writeback 路径
 *      尝试写入，驱动设置权限有误。
 *
 *   ③ EXTERNAL fault：
 *      AXI 总线错误（如 NoC 超时、电源域未打开时强行访问），与页表无关。
 *
 * ## 此函数的设计定位：诊断，不恢复
 *
 * 返回 0 表示"已处理"（让内核继续运行，不 panic），但实际上
 * VOP2 已经读到了无效数据，当前帧的显示输出可能出现：
 *   - 花屏（随机数据被当像素渲染）
 *   - 黑屏（显示流水线停止响应）
 *   - VOP2 内部状态机卡死（需要完整 reset 才能恢复）
 *
 * 因此此函数的主要价值是在 fault 发生后**留下尽可能详细的现场快照**，
 * 供开发者事后分析，定位 unmap/scan 时序问题或驱动 bug。【笔记钩子】
 */
static int rockchip_drm_fault_handler(struct iommu_domain *iommu,
				      struct device *dev,
				      unsigned long iova, int flags, void *arg)
{
	struct drm_device *drm_dev = arg;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	/* 打印故障基本信息：flags 十六进制值可与上方 IOMMU_FAULT_* 宏对照分析 */
	DRM_ERROR("iommu fault handler flags: 0x%x\n", flags);

	/*
	 * 遍历所有 CRTC（对应 VOP2 的每个 Video Port），转储其硬件状态。
	 *
	 * 之所以转储所有 CRTC 而非仅 fault 的那个，是因为：
	 * VOP2 多个 VP 共享同一个 IOMMU domain，任一 VP 的 DMA 故障
	 * 都可能影响其他 VP 的状态，需要完整快照才能还原现场。
	 */
	drm_for_each_crtc(crtc, drm_dev) {
		/*
		 * drm_crtc_index() 返回 CRTC 在 drm_device 中的全局序号（0 起）,
		 * 对应 priv->crtc_funcs[] 数组的下标，最大为 ROCKCHIP_MAX_CRTC-1。
		 * crtc_funcs 由 vop2_bind() 通过 rockchip_register_crtc_funcs()
		 * 在 CRTC 注册时填入，若该 CRTC 尚未注册或已注销则为 NULL。
		 */
		int pipe = drm_crtc_index(crtc);

		/*
		 * regs_dump：转储 VOP2 Video Port 的**硬件寄存器**原始值。
		 *
		 * 对应 vop2_crtc_regs_dump()，输出内容包括：
		 *   - WIN 图层的帧缓冲地址寄存器（YRGB_MST / CBR_MST）← fault IOVA 的直接来源
		 *   - 显示时序寄存器（HTOTAL / VTOTAL / HSYNC / VSYNC）
		 *   - 颜色空间、格式、缩放等控制寄存器
		 *
		 * seq_file 传 NULL 表示直接通过 DRM_ERROR/pr_err 输出到内核日志，
		 * 而非写入 debugfs 文件（debugfs 路径下 seq_file 非 NULL）。
		 */
		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->regs_dump)
			priv->crtc_funcs[pipe]->regs_dump(crtc, NULL);

		/*
		 * debugfs_dump：转储更高层的**软件状态**信息。
		 *
		 * 对应 vop2_crtc_debugfs_dump()，输出内容包括：
		 *   - 当前 drm_plane_state 中的 GEM buffer 信息（fb、IOVA 等）
		 *   - atomic commit 状态、flip pending 标志
		 *   - 带宽统计、颜色空间、HDR 状态等
		 *
		 * 与 regs_dump 配合使用：
		 *   regs_dump 反映"硬件实际在用什么"，
		 *   debugfs_dump 反映"软件认为硬件应该在用什么"，
		 *   两者不一致时往往就是 bug 所在。
		 */
		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, NULL);
	}

	/*
	 * 返回 0：告知 IOMMU 框架"fault 已处理，继续运行"。
	 * 硬件层面 VOP2 此次 DMA 事务已经失败，当前帧输出可能异常，
	 * 但内核不会因此 panic，后续新的 atomic commit 可能会恢复正常。
	 */
	return 0;
}

/**
 * rockchip_drm_init_iommu - 初始化 DRM 设备的 IOMMU 映射域和虚拟地址分配器
 * @drm_dev: Rockchip DRM 设备
 *
 * 返回值：0 成功；-ENOMEM 分配 IOMMU domain 失败
 *
 * ## 背景：为什么 DRM 需要 IOMMU？
 *
 * VOP2（显示控制器）作为 DMA master，从 DDR 读取帧缓冲数据时，
 * 其 DMA 地址默认是**物理地址**，要求帧缓冲必须物理连续（CMA 内存）。
 *
 * 引入 IOMMU 后，VOP2 的 DMA 使用的是 **IOVA（I/O 虚拟地址）**，
 * 由 IOMMU MMU 硬件将 IOVA → 物理页面的映射，
 * 帧缓冲无需物理连续，普通的离散页即可，极大减少对 CMA 内存的依赖。
 *
 * ## 整体 IOMMU 使用流程
 *
 *  本函数：iommu_domain_alloc() → 创建共享 IOMMU domain
 *         drm_mm_init()        → 初始化 IOVA 虚拟地址分配器
 *
 *  VOP2 bind 时：rockchip_drm_dma_attach_device()
 *         iommu_attach_device(domain, vop_dev) → 将 VOP2 挂入 domain，
 *         此后 VOP2 的所有 DMA 事务均经过此 domain 的 MMU 翻译
 *
 *  GEM buffer 分配时：rockchip_gem_create()
 *         drm_mm_insert_node() → 从 private->mm 分配一段 IOVA
 *         iommu_map()          → 建立 IOVA → 物理页的映射（填充页表）
 *         VOP2 的 DMA 地址寄存器写入该 IOVA 即可
 *
 *  GEM buffer 释放时：
 *         iommu_unmap()        → 拆除 IOVA → 物理页的映射
 *         drm_mm_remove_node() → 归还 IOVA 段
 *
 * ## 本函数的三步初始化
 */
static int rockchip_drm_init_iommu(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain_geometry *geometry;
	u64 start, end;

	/*
	 * is_support_iommu = false（由 rockchip_drm_platform_of_probe 设置）
	 * 说明至少有一个 VOP 没有 IOMMU 支持，全系统退回物理连续内存模式，
	 * 无需创建 IOMMU domain，直接返回。
	 */
	if (!is_support_iommu)
		return 0;

	/*
	 * 步骤 1：为 platform_bus 类型的设备分配一个共享 IOMMU domain。
	 *
	 * iommu_domain_alloc(&platform_bus_type) 创建一个"空白"地址空间域，
	 * 内部分配页表（通常是 4KB 粒度的二/三级页表），
	 * 并由 IOMMU 驱动（如 Rockchip IOMMU 驱动）初始化域的属性，
	 * 包括 geometry（地址孔径）和页表格式（ARMv8 Short-descriptor 等）。
	 *
	 * 所有 VOP2 的 VP（CRTC）共享同一个 domain，意味着：
	 *   - 它们使用同一套 IOVA 地址空间（不会重叠，由 drm_mm 统一分配）
	 *   - 同一块 GEM buffer 可以被多个 CRTC 同时扫描，无需重复映射
	 */
	private->domain = iommu_domain_alloc(&platform_bus_type);
	if (!private->domain)
		return -ENOMEM;

	/*
	 * 步骤 2：读取 IOMMU domain 的地址孔径（aperture），初始化 drm_mm。
	 *
	 * geometry->aperture_start / aperture_end 是此 domain 允许映射的
	 * IOVA 地址范围，由 IOMMU 硬件/驱动决定：
	 *   Rockchip IOMMU 通常为 0x10000000 ~ 0xFFFFFFFF（约 3.75GB IOVA 空间）
	 *
	 * drm_mm 是 DRM 内置的区间分配器（类似 malloc 的地址空间管理），
	 * 以 aperture 范围初始化后，负责从该范围内分配和回收 IOVA 段：
	 *   drm_mm_insert_node(mm, node, size) → 分配一段连续的 IOVA
	 *   drm_mm_remove_node(node)           → 归还该 IOVA 段
	 *
	 * mm_lock 保护 drm_mm 的并发访问（多个线程可能同时分配/释放 GEM buffer）。
	 *
	 * 可通过 debugfs 查看当前 IOVA 分配状态：【笔记钩子】
	 *   cat /sys/kernel/debug/dri/0/mm_dump
	 */
	geometry = &private->domain->geometry;
	start = geometry->aperture_start;
	end = geometry->aperture_end;

	DRM_DEBUG("IOMMU context initialized (aperture: %#llx-%#llx)\n",
		  start, end);
	drm_mm_init(&private->mm, start, end - start + 1);
	mutex_init(&private->mm_lock);

	/*
	 * 步骤 3：注册 IOMMU 缺页故障处理函数。
	 *
	 * 当 VOP2 的 DMA 访问了 domain 中**未建立映射**的 IOVA 时，
	 * IOMMU 硬件触发 page fault，调用此回调 rockchip_drm_fault_handler()。
	 *
	 * fault handler 的处理逻辑：
	 *   1. 打印错误日志（包含故障 flags：READ/WRITE/EXTERNAL 等）
	 *   2. 遍历所有 CRTC，调用各 CRTC 的 regs_dump()（转储 VOP 寄存器状态）
	 *      和 debugfs_dump()（转储更详细的显示状态信息）
	 *   3. 返回 0（不杀进程，让内核继续）
	 *
	 * 常见触发原因：
	 *   - GEM buffer 已 unmap 但 VOP2 寄存器尚未更新（时序问题）
	 *   - DRM 驱动 bug 导致写入了无效的帧缓冲地址
	 *   - 热插拔 disable CRTC 期间，GEM buffer IOVA 被提前 unmap，
	 *     而 VOP2 硬件 DMA 尚未完全停止，导致访问已失效的 IOVA
	 */
	iommu_set_fault_handler(private->domain, rockchip_drm_fault_handler,
				drm_dev);

	return 0;
}

static void rockchip_iommu_cleanup(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!is_support_iommu)
		return;

	drm_mm_takedown(&private->mm);
	iommu_domain_free(private->domain);
}

#ifdef CONFIG_DEBUG_FS
static int rockchip_drm_mm_dump(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;

	struct drm_printer p = drm_seq_file_printer(s);

	if (!priv->domain)
		return 0;

	mutex_lock(&priv->mm_lock);

	drm_mm_print(&priv->mm, &p);

	mutex_unlock(&priv->mm_lock);

	return 0;
}

static int rockchip_drm_summary_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct rockchip_drm_private *priv = drm_dev->dev_private;
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_dump)
			priv->crtc_funcs[pipe]->debugfs_dump(crtc, s);
	}

	return 0;
}

static struct drm_info_list rockchip_debugfs_files[] = {
	{ "summary", rockchip_drm_summary_show, 0, NULL },
	{ "mm_dump", rockchip_drm_mm_dump, 0, NULL },
};

static int rockchip_drm_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	int ret;

	ret = drm_debugfs_create_files(rockchip_debugfs_files,
				       ARRAY_SIZE(rockchip_debugfs_files),
				       minor->debugfs_root,
				       minor);
	if (ret) {
		dev_err(dev->dev, "could not install rockchip_debugfs_list\n");
		return ret;
	}

	drm_for_each_crtc(crtc, dev) {
		int pipe = drm_crtc_index(crtc);

		if (priv->crtc_funcs[pipe] &&
		    priv->crtc_funcs[pipe]->debugfs_init)
			priv->crtc_funcs[pipe]->debugfs_init(minor, crtc);
	}

	return 0;
}
#endif

static int rockchip_drm_create_properties(struct drm_device *dev)
{
	struct drm_property *prop;
	struct rockchip_drm_private *private = dev->dev_private;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "EOTF", 0, 5);
	if (!prop)
		return -ENOMEM;
	private->eotf_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "COLOR_SPACE", 0, 12);
	if (!prop)
		return -ENOMEM;
	private->color_space_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "GLOBAL_ALPHA", 0, 255);
	if (!prop)
		return -ENOMEM;
	private->global_alpha_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "BLEND_MODE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->blend_mode_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ALPHA_SCALE", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->alpha_scale_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "ASYNC_COMMIT", 0, 1);
	if (!prop)
		return -ENOMEM;
	private->async_commit_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "SHARE_ID", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	private->share_id_prop = prop;

	prop = drm_property_create_range(dev, DRM_MODE_PROP_ATOMIC,
					 "CONNECTOR_ID", 0, 0xf);
	if (!prop)
		return -ENOMEM;
	private->connector_id_prop = prop;

	return drm_mode_create_tv_properties(dev, 0, NULL);
}

static int rockchip_gem_pool_init(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct device_node *np = drm->dev->of_node;
	struct device_node *node;
	phys_addr_t start, size;
	struct resource res;
	int ret;

	node = of_parse_phandle(np, "secure-memory-region", 0);
	if (!node)
		return -ENXIO;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;
	start = res.start;
	size = resource_size(&res);
	if (!size)
		return -ENOMEM;

	private->secure_buffer_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!private->secure_buffer_pool)
		return -ENOMEM;

	gen_pool_add(private->secure_buffer_pool, start, size, -1);

	return 0;
}

static void rockchip_gem_pool_destroy(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;

	if (!private->secure_buffer_pool)
		return;

	gen_pool_destroy(private->secure_buffer_pool);
}

/**
 * rockchip_attach_connector_property - 为所有 connector 附加 TV 显示属性
 * @drm: DRM 设备对象
 *
 * 遍历系统中的所有显示输出接口（connector），为每个接口附加 TV 相关的
 * 显示属性（亮度、对比度、饱和度、色调），并设置默认值为 50。
 *
 * 为什么存在多个 connector？
 * 一个 SoC 通常有多个物理显示输出端口，每个端口对应一个 connector：
 * - HDMI 接口（通过 inno_hdmi.c 或 cdn-dp-core.c 创建）
 * - MIPI DSI 接口（通过 dw-mipi-dsi.c 创建，用于连接 LCD 屏幕）
 * - eDP 接口（嵌入式 DisplayPort）
 * - LVDS 接口（用于传统 LCD）
 *
 * 例如 RK3568 芯片可能同时支持：
 * - 1 个 HDMI 输出（连接电视/显示器）
 * - 2 个 MIPI DSI 输出（连接手机屏或平板屏）
 * 这样就会有 3 个 connector 对象
 *
 * 每个 connector 可以独立配置显示参数，支持多屏异显或同显。
 * RK3568 示例架构：
┌──────────────────────────────────────┐
│         RK3568 SoC                   │
│                                      │
│  ┌────────┐   ┌──────────────┐      │
│  │  VOP2  │──→│ HDMI TX      │─────→│ HDMI Connector (connector 0)
│  │(显示控制)│   └──────────────┘      │
│  │        │   ┌──────────────┐      │
│  │        │──→│ MIPI DSI0    │─────→│ MIPI DSI Connector (connector 1)
│  │        │   └──────────────┘      │
│  │        │   ┌──────────────┐      │
│  │        │──→│ MIPI DSI1    │─────→│ MIPI DSI Connector (connector 2)
│  └────────┘   └──────────────┘      │
│                ┌──────────────┐      │
│               │ eDP          │─────→│ eDP Connector (connector 3)
│                └──────────────┘      │
└──────────────────────────────────────┘
 */
static void rockchip_attach_connector_property(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_connector_list_iter conn_iter;

	/* 加锁保护 mode_config 配置操作 */
	mutex_lock(&drm->mode_config.mutex);

	/* 定义属性附加宏，简化重复代码 */
#define ROCKCHIP_PROP_ATTACH(prop, v) \
		drm_object_attach_property(&connector->base, prop, v)

	/* 初始化 connector 迭代器并遍历所有 connector */
	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		/* 为当前 connector 附加 4 个 TV 属性，默认值均为 50 */
		ROCKCHIP_PROP_ATTACH(conf->tv_brightness_property, 50);  /* 亮度 */
		ROCKCHIP_PROP_ATTACH(conf->tv_contrast_property, 50);    /* 对比度 */
		ROCKCHIP_PROP_ATTACH(conf->tv_saturation_property, 50);  /* 饱和度 */
		ROCKCHIP_PROP_ATTACH(conf->tv_hue_property, 50);         /* 色调 */
	}
	drm_connector_list_iter_end(&conn_iter);
#undef ROCKCHIP_PROP_ATTACH

	mutex_unlock(&drm->mode_config.mutex);
}

static void rockchip_drm_set_property_default(struct drm_device *drm)
{
	struct drm_connector *connector;
	struct drm_mode_config *conf = &drm->mode_config;
	struct drm_atomic_state *state;
	int ret;
	struct drm_connector_list_iter conn_iter;

	drm_modeset_lock_all(drm);

	state = drm_atomic_helper_duplicate_state(drm, conf->acquire_ctx);
	if (!state) {
		DRM_ERROR("failed to alloc atomic state\n");
		goto err_unlock;
	}
	state->acquire_ctx = conf->acquire_ctx;

	drm_connector_list_iter_begin(drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *connector_state;

		connector_state = drm_atomic_get_connector_state(state,
								 connector);
		if (IS_ERR(connector_state)) {
			DRM_ERROR("Connector[%d]: Failed to get state\n", connector->base.id);
			continue;
		}

		connector_state->tv.brightness = 50;
		connector_state->tv.contrast = 50;
		connector_state->tv.saturation = 50;
		connector_state->tv.hue = 50;
	}
	drm_connector_list_iter_end(&conn_iter);

	ret = drm_atomic_commit(state);
	WARN_ON(ret == -EDEADLK);
	if (ret)
		DRM_ERROR("Failed to update properties\n");
	drm_atomic_state_put(state);

err_unlock:
	drm_modeset_unlock_all(drm);
}

static bool is_support_hotplug(uint32_t output_type)
{
	switch (output_type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_DVIA:
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
	case DRM_MODE_CONNECTOR_TV:
		return true;
	default:
		return false;
	}
}

/**
 * rockchip_drm_bind - Rockchip DRM 显示子系统的聚合绑定入口
 * @dev: display-subsystem platform_device.dev
 *
 * 本函数是 component 框架调用的 master .bind() 回调，
 * 在所有子组件（VOP2、DSI、HDMI 等）都通过 component_add() 注册后触发。
 * 完成整个 DRM 设备的完整初始化，最终向用户空间开放 /dev/dri/card0。
 *
 * ## 初始化阶段总览
 *
 *  阶段 1：创建 drm_device 骨架
 *  阶段 2：初始化 Rockchip 私有数据（锁、devfreq、PLL、PSR）
 *  阶段 3：初始化 IOMMU 内存映射域
 *  阶段 4：初始化 KMS 模式配置框架（mode_config）
 *  阶段 5：绑定所有子组件（注册 CRTC/Encoder/Connector/Plane）
 *  阶段 6：初始化 VBlank、属性默认值、中断模式
 *  阶段 7：初始化热插拔轮询、GEM 内存池、Loader logo、fbdev 兼容层
 *  阶段 8：向用户空间注册 DRM 设备
 *
 * ## 错误回滚路径（goto 标签，逆序释放）
 *
 *  err_fbdev_fini         → 释放 fbdev
 *  err_kms_helper_poll_fini → 释放 GEM 池 + 停止 KMS poll
 *  err_unbind_all         → 解绑所有子组件
 *  err_mode_config_cleanup→ 清理 mode_config + IOMMU
 *  err_free               → 清理 drm_dev
 */
static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;
	struct device_node *np = dev->of_node;
	struct device_node *parent_np;
	struct drm_crtc *crtc;

	/* ================================================================
	 * 阶段 1：创建 drm_device 骨架
	 * ================================================================ */

	/*
	 * 分配并初始化 drm_device，绑定 rockchip_drm_driver 操作集。
	 * drm_device 是整个 DRM 子系统的顶层容器，持有 mode_config、
	 * filelist、vblank 等所有核心状态。
	 */
	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	/* 将 drm_dev 存入平台设备的 drvdata，方便后续通过 dev 反查 */
	dev_set_drvdata(dev, drm_dev);

	/* ================================================================
	 * 阶段 2：初始化 Rockchip 私有数据
	 * ================================================================ */

	/*
	 * 分配 rockchip_drm_private，使用 devm_kzalloc 绑定到 drm_dev->dev，
	 * 设备卸载时自动释放，无需手动 kfree。
	 */
	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	/*
	 * 初始化两把互斥锁和一个异步提交工作队列：
	 *   commit_lock：保护 rockchip_atomic_commit 对象，序列化异步原子提交
	 *   ovl_lock：保护 VOP2 共享 overlay 资源（OVL_LAYER_SEL/OVL_PORT_SEL 寄存器），
	 *             多个 VP 可能竞争同一组全局 overlay 路由寄存器
	 *   commit_work：非阻塞原子提交的工作队列执行体，在 kthread_worker 中
	 *               异步等待 VBlank 并写硬件寄存器，允许合成器立即返回渲染下一帧
	 */
	mutex_init(&private->commit_lock);
	mutex_init(&private->ovl_lock);
	INIT_WORK(&private->commit_work, rockchip_drm_atomic_work);
	drm_dev->dev_private = private;

	/*
	 * 初始化 DMC（Dynamic Memory Controller）动态内存频率调节支持。
	 *
	 * devfreq_get_devfreq_by_phandle() 通过 DTS 的 "devfreq" 属性获取
	 * DMC 的 devfreq 设备（动态频率调节框架的句柄）。
	 * DMC 频率调节可以在显示刷新间隙降低 DDR 频率以节省功耗。
	 *
	 * 三种结果：
	 *   成功获取：dmc_support = true，后续可动态调节 DDR 频率
	 *   -EPROBE_DEFER（DMC 驱动尚未就绪）：
	 *     若 DTS 中 devfreq 节点存在且可用 → dmc_support = true，
	 *       但 devfreq 指针置 NULL（等待重试）
	 *     若 DTS 中 devfreq 节点不存在或已禁用 → dmc_support = false
	 *   其他错误（节点不存在等）：devfreq = NULL，dmc_support = false
	 */
	private->dmc_support = false;
	private->devfreq = devfreq_get_devfreq_by_phandle(dev, 0);
	if (IS_ERR(private->devfreq)) {
		if (PTR_ERR(private->devfreq) == -EPROBE_DEFER) {
			parent_np = of_parse_phandle(np, "devfreq", 0);
			if (parent_np &&
			    of_device_is_available(parent_np)) {
				private->dmc_support = true;
				dev_warn(dev, "defer getting devfreq\n");
			} else {
				dev_info(dev, "dmc is disabled\n");
			}
		} else {
			dev_info(dev, "devfreq is not set\n");
		}
		private->devfreq = NULL;
	} else {
		private->dmc_support = true;
		dev_info(dev, "devfreq is ready\n");
	}

	/*
	 * 获取 HDMI TMDS PLL 时钟句柄（可选）。
	 *
	 * HDMI 输出需要精确的 TMDS 时钟（148.5MHz@1080p60 等），
	 * 由独立的 HDMI PLL 提供。VOP2 输出 HDMI 时通过此 PLL 获取像素时钟。
	 *
	 * -ENOENT：DTS 中未配置 "hdmi-tmds-pll"（如无 HDMI 输出的方案），
	 *   置 NULL 表示不使用，继续初始化
	 * -EPROBE_DEFER：PLL 驱动尚未就绪，整体推迟重试
	 * 其他错误：硬件异常，直接失败
	 */
	private->hdmi_pll.pll = devm_clk_get(dev, "hdmi-tmds-pll");
	if (PTR_ERR(private->hdmi_pll.pll) == -ENOENT) {
		private->hdmi_pll.pll = NULL;
	} else if (PTR_ERR(private->hdmi_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->hdmi_pll.pll)) {
		dev_err(dev, "failed to get hdmi-tmds-pll\n");
		ret = PTR_ERR(private->hdmi_pll.pll);
		goto err_free;
	}

	/*
	 * 获取默认 VOP PLL 时钟句柄（可选）。
	 *
	 * 当 HDMI PLL 不可用时（未接 HDMI 或 HDMI 已释放 PLL），
	 * VOP2 使用此默认 PLL 为其他输出接口（DSI、eDP）提供像素时钟。
	 * 同样为可选项，-ENOENT 表示硬件方案无此 PLL，置 NULL 跳过。
	 */
	private->default_pll.pll = devm_clk_get(dev, "default-vop-pll");
	if (PTR_ERR(private->default_pll.pll) == -ENOENT) {
		private->default_pll.pll = NULL;
	} else if (PTR_ERR(private->default_pll.pll) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto err_free;
	} else if (IS_ERR(private->default_pll.pll)) {
		dev_err(dev, "failed to get default vop pll\n");
		ret = PTR_ERR(private->default_pll.pll);
		goto err_free;
	}

	/*
	 * 初始化 PSR（Panel Self-Refresh）设备列表。
	 * PSR 允许屏幕在画面静止时自行刷新（不需要 SoC 持续发送帧数据），
	 * 大幅降低笔记本/平板等电池设备的显示功耗。
	 * psr_list 挂载所有支持 PSR 的 connector，psr_list_lock 保护并发访问。
	 */
	INIT_LIST_HEAD(&private->psr_list);
	mutex_init(&private->psr_list_lock);

	/* ================================================================
	 * 阶段 3：初始化 IOMMU 内存映射域
	 * ================================================================ */

	/*
	 * 为 VOP2 创建并挂载 iommu_domain。
	 * 若 is_support_iommu = true，VOP2 DMA 通过 IOMMU 使用虚拟地址，
	 * GEM buffer 无需物理连续（节省 CMA 内存）；
	 * 若 is_support_iommu = false，此函数为空操作，所有帧缓冲使用物理连续内存。
	 */
	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_free;

	/* ================================================================
	 * 阶段 4：初始化 KMS 模式配置框架
	 * ================================================================ */

	/*
	 * drm_mode_config_init：初始化 drm_mode_config 结构体，
	 * 包括 CRTC/Plane/Connector/Encoder 对象链表、全局互斥锁、
	 * property_blob_list 等，是所有 KMS 对象注册的前提。
	 */
	drm_mode_config_init(drm_dev);

	/*
	 * rockchip_drm_mode_config_init：设置 Rockchip 特定的 mode_config 参数：
	 *   min/max_width、min/max_height（支持的分辨率范围）
	 *   funcs（.fb_create、.atomic_check、.atomic_commit 等回调）
	 *   helper_private（.atomic_commit_tail 回调，即 atomic_commit_tail 钩子）
	 */
	rockchip_drm_mode_config_init(drm_dev);

	/*
	 * 创建 Rockchip 私有 DRM 属性（drm_property）：
	 *   "CONNECTOR_ID"：标识 connector 的私有编号，供 Rockchip userspace 使用
	 *   各种 Rockchip 扩展属性（色彩增强、scaling 策略等）
	 * 这些属性在 component_bind_all() 之前创建，子组件 bind 时可直接附加。
	 */
	rockchip_drm_create_properties(drm_dev);

	/* ================================================================
	 * 阶段 5：绑定所有子组件（核心步骤）
	 * ================================================================ */

	/*
	 * component_bind_all：遍历 master->match 列表，依次调用每个子组件的
	 * ops->bind(comp_dev, master_dev, drm_dev)。
	 *
	 * 各子组件 bind 完成后：
	 *   vop2_bind()         → 注册 drm_crtc（VP0/VP1/VP2）和 drm_plane（Win0~Win3）
	 *   dw_mipi_dsi_bind()  → 注册 drm_encoder（DSI）和 drm_connector（DSI）
	 *   dw_hdmi_bind()      → 注册 drm_encoder（TMDS）和 drm_connector（HDMI）
	 *   rockchip_dp_bind()  → 注册 drm_encoder（eDP）和 drm_connector（eDP）
	 *
	 * 完成后 mode_config 中的 CRTC/Plane/Encoder/Connector 链表均已填充，
	 * 显示拓扑完整建立。
	 */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_mode_config_cleanup;

	/*
	 * 为所有已注册的 connector 附加 TV 显示调节属性
	 * （brightness、saturation、hue、contrast）。
	 * 这些属性允许用户空间调节输出画面的色彩效果。
	 */
	rockchip_attach_connector_property(drm_dev);

	/* ================================================================
	 * 阶段 6：VBlank、属性默认值、中断模式
	 * ================================================================ */

	/*
	 * 为每个 CRTC 初始化 VBlank 管理结构（drm_vblank_crtc）：
	 *   分配 vblank 计数器、时间戳数组、引用计数等
	 *   注册 VBlank 等待队列（供 drm_wait_vblank ioctl 使用）
	 * num_crtc 在 vop2_bind() 时已由 drm_crtc_init_with_planes() 自动统计。
	 */
	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	/*
	 * drm_mode_config_reset：将所有 CRTC/Plane/Connector 的 state 重置为初始值，
	 * 调用各对象的 funcs->reset() 回调，确保原子状态机从已知干净状态启动。
	 */
	drm_mode_config_reset(drm_dev);

	/*
	 * 为各 connector 和 CRTC 的属性设置 Rockchip 平台的合理默认值
	 * （如 HDR 参数、色彩空间、scaling 模式等）。
	 */
	rockchip_drm_set_property_default(drm_dev);

	/*
	 * 开启 DRM IRQ 模式。
	 * irq_enabled = true 后，VBlank 中断处理函数可以正常调用
	 * drm_handle_vblank()，VBlank 事件机制（page flip 完成通知等）得以工作。
	 */
	drm_dev->irq_enabled = true;

	/* ================================================================
	 * 阶段 7：热插拔轮询、GEM 池、Loader logo、fbdev 兼容层
	 * ================================================================ */

	/*
	 * 初始化 KMS 输出轮询机制（用于 VGA 等无 HPD 中断的接口）。
	 * 对于 DSI/HDMI（有 HPD 中断），此机制不会实际运行，
	 * 但框架仍需初始化以统一管理热插拔事件通知路径。
	 */
	drm_kms_helper_poll_init(drm_dev);

	/*
	 * 初始化 Rockchip GEM 内存池（secure_buffer_pool）。
	 * 预先分配安全（TEE/secure）内存区域，用于 DRM 内容保护（HDCP）场景，
	 * 防止普通进程访问受保护的视频帧缓冲。
	 */
	rockchip_gem_pool_init(drm_dev);

#ifndef MODULE
	/*
	 * 显示 bootloader/uboot 阶段已加载的 logo（开机画面无缝衔接）。
	 * 仅在内建驱动（非模块）时执行，确保内核启动时 logo 持续显示，
	 * 避免内核初始化期间屏幕黑屏。模块加载时跳过此步骤。
	 */
	show_loader_logo(drm_dev);
#endif

	/*
	 * 将 DTS 中 memory-region 指定的保留内存区域关联到 drm_dev->dev。
	 * 保留内存用于：
	 *   "drm-logo"：uboot logo framebuffer（无缝显示开机画面）
	 *   "drm-cubic-lut"：3D LUT 数据（色彩校准查找表，需要大块连续内存）
	 * 失败时仅打印 debug 日志，不影响主流程（保留内存是可选优化）。
	 */
	ret = of_reserved_mem_device_init(drm_dev->dev);
	if (ret)
		DRM_DEBUG_KMS("No reserved memory region assign to drm\n");

	/*
	 * 初始化 fbdev 兼容层（/dev/fb0），供不支持 DRM 的旧式应用使用。
	 * 创建一个覆盖主显示器全屏的 framebuffer，将 fb_ops 映射到 DRM 原子提交。
	 */
	ret = rockchip_drm_fbdev_init(drm_dev);
	if (ret)
		goto err_kms_helper_poll_fini;

	/*
	 * 为支持热插拔的 CRTC（输出类型为 HDMI/DVI/DP/TV 等）增加 fbdev framebuffer
	 * 的额外引用计数。
	 *
	 * 原因：支持热插拔的接口（如 HDMI）在屏幕拔出后会触发 connector 断开，
	 * DRM 核心可能尝试释放关联的 framebuffer。增加额外引用确保 fbdev 的 fb 对象
	 * 在 HDMI 热插拔期间不会被意外释放，保持 /dev/fb0 的持续可用性。
	 * DSI/eDP 等嵌入式固定屏幕无需此处理（不会热插拔）。
	 */
	drm_for_each_crtc(crtc, drm_dev) {
		struct drm_fb_helper *helper = private->fbdev_helper;
		struct rockchip_crtc_state *s = NULL;

		if (!helper)
			break;

		s = to_rockchip_crtc_state(crtc->state);
		if (is_support_hotplug(s->output_type))
			drm_framebuffer_get(helper->fb); /* +1 引用，防止热插拔时意外释放 */
	}

	/*
	 * 允许使用带 modifier 的帧缓冲格式（如 AFBC 压缩格式）。
	 * modifier 描述帧缓冲在内存中的特殊布局（如 AFBC tile 排列），
	 * 开启后 GPU 输出的 AFBC buffer 可直接被 VOP2 硬件解压扫描，
	 * 节省 DDR 带宽约 30~50%。
	 */
	drm_dev->mode_config.allow_fb_modifiers = true;

	/* ================================================================
	 * 阶段 8：向用户空间注册 DRM 设备
	 * ================================================================ */

	/*
	 * drm_dev_register：将 drm_dev 注册到内核设备模型，
	 * 创建 /dev/dri/card0 和 /dev/dri/renderD128 字符设备节点，
	 * 从此刻起用户空间可以 open/ioctl DRM 设备，合成器可以开始工作。
	 * 这是整个 DRM 初始化的最后一步，也是对外"开门"的时刻。
	 */
	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_fbdev_fini;

	return 0;

	/* ================================================================
	 * 错误回滚：逆序释放已初始化的资源
	 * ================================================================ */
err_fbdev_fini:
	rockchip_drm_fbdev_fini(drm_dev);
err_kms_helper_poll_fini:
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);       /* 停止 KMS 输出轮询工作队列 */
err_unbind_all:
	component_unbind_all(dev, drm_dev);       /* 逆序调用所有子组件的 unbind() */
err_mode_config_cleanup:
	drm_mode_config_cleanup(drm_dev);         /* 释放所有 KMS 对象和属性 */
	rockchip_iommu_cleanup(drm_dev);          /* 卸载 IOMMU domain，解除映射 */
err_free:
	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);                     /* 减引用，触发 drm_dev 内存释放 */
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	rockchip_drm_fbdev_fini(drm_dev);
	rockchip_gem_pool_destroy(drm_dev);
	drm_kms_helper_poll_fini(drm_dev);

	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(dev, drm_dev);
	drm_mode_config_cleanup(drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev->dev_private = NULL;
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
}

/**
 * rockchip_drm_crtc_cancel_pending_vblank - 取消某进程在指定 CRTC 上的待处理 VBlank 事件
 * @crtc: 目标 CRTC
 * @file_priv: 要清理的进程的 DRM 文件句柄
 *
 * 当用户空间进程关闭 DRM 文件描述符时调用（在 rockchip_drm_postclose 中）。
 *
 * 典型场景：
 *   1. 用户空间进程请求了 VBlank 事件（如等待 page flip 完成）
 *   2. 但在事件到来之前，进程崩溃或主动关闭了 /dev/dri/card0
 *   3. 内核需要清理这些"孤儿事件"，避免内存泄漏和野指针
 *
 * 本函数委托给具体的 CRTC 实现（如 VOP2）来执行清理，
 * 因为不同硬件的事件管理机制可能不同。
 */
static void rockchip_drm_crtc_cancel_pending_vblank(struct drm_crtc *crtc,
						    struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = crtc->dev->dev_private;
	int pipe = drm_crtc_index(crtc);

	/*
	 * 三重安全检查：
	 * 1. pipe < ROCKCHIP_MAX_CRTC: 防止数组越界
	 * 2. priv->crtc_funcs[pipe]: 确保该 CRTC 已注册
	 * 3. cancel_pending_vblank: 确保该 CRTC 实现了取消回调
	 *
	 * 为什么需要这些检查？
	 * - 某些 CRTC 可能尚未初始化（系统启动早期）
	 * - 旧硬件可能不支持这个回调（向后兼容）
	 */
	if (pipe < ROCKCHIP_MAX_CRTC &&
	    priv->crtc_funcs[pipe] &&
	    priv->crtc_funcs[pipe]->cancel_pending_vblank)
		priv->crtc_funcs[pipe]->cancel_pending_vblank(crtc, file_priv);
}

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, dev)
		crtc->primary->fb = NULL;

	return 0;
}

/**
 * rockchip_drm_postclose - 进程关闭 DRM 文件描述符后的清理回调
 * @dev: DRM 设备
 * @file_priv: 正在关闭的文件句柄
 *
 * 当用户空间进程执行 close(/dev/dri/card0) 时，DRM 核心会调用此回调。
 *
 * 职责：清理该进程留下的所有"未完成的事务"，避免资源泄漏。
 *
 * 具体操作：
 *   遍历所有 CRTC，取消该进程在每个 CRTC 上的待处理 VBlank 事件。
 *
 * 为什么要遍历所有 CRTC？
 *   一个进程可能同时在多个显示器（多个 CRTC）上请求了 VBlank 事件，
 *   关闭文件时需要全部清理。
 */
static void rockchip_drm_postclose(struct drm_device *dev,
				   struct drm_file *file_priv)
{
	struct drm_crtc *crtc;

	/* 遍历所有 CRTC，清理该进程的待处理事件 */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		rockchip_drm_crtc_cancel_pending_vblank(crtc, file_priv);
}

static void rockchip_drm_lastclose(struct drm_device *dev)
{
	struct rockchip_drm_private *priv = dev->dev_private;

	if (!priv->logo)
		drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev_helper);
}

/**
 * rockchip_drm_add_vcnt_event - 创建一个 vcnt（垂直计数）事件
 *
 * Rockchip 自定义的扩展机制：vcnt 事件。
 * 与标准 DRM VBlank 事件不同，vcnt 由 VOP2 硬件的行计数器驱动
 * （见 vop2_read_vcnt()），可以精确获取当前扫描到第几行。
 *
 * 本函数负责：
 * 1. 分配一个 pending event 结构
 * 2. 填充事件类型为 DRM_EVENT_ROCKCHIP_CRTC_VCNT（Rockchip 私有事件类型）
 * 3. 将用户空间传入的 signal（用户自定义标识）保存到 user_data
 * 4. 注册到 DRM 事件系统，等待 vop2_handle_vcnt() 在中断中发送给用户空间
 *
 * @crtc: 目标 CRTC
 * @vblwait: 用户空间传入的 vblank 请求参数（复用标准 VBlank 的数据格式）
 * @file_priv: 发起请求的用户空间进程的 DRM 文件句柄
 */
static struct drm_pending_vblank_event *
rockchip_drm_add_vcnt_event(struct drm_crtc *crtc, union drm_wait_vblank *vblwait,
			    struct drm_file *file_priv)
{
	struct drm_pending_vblank_event *e;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return NULL;

	/* 填充事件基本信息 */
	e->pipe = drm_crtc_index(crtc);
	e->event.base.type = DRM_EVENT_ROCKCHIP_CRTC_VCNT; /* Rockchip 私有事件类型 0xf */
	e->event.base.length = sizeof(e->event.vbl);
	e->event.vbl.crtc_id = crtc->base.id;
	e->event.vbl.user_data = vblwait->request.signal; /* 用户自定义标识，原样回传 */

	/* 注册到 DRM 事件队列，后续可通过 read(/dev/dri/card0) 读取 */
	spin_lock_irqsave(&dev->event_lock, flags);
	drm_event_reserve_init_locked(dev, file_priv, &e->base, &e->event.base);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return e;
}

/**
 * rockchip_drm_get_vcnt_event_ioctl - Rockchip 私有 ioctl：注册 vcnt 事件
 *
 * 用户空间通过 DRM_IOCTL_ROCKCHIP_GET_VCNT_EVENT 调用此函数。
 *
 * 工作流程：
 * 1. 用户空间发起 ioctl，携带目标 CRTC 编号和 _DRM_ROCKCHIP_VCNT_EVENT 标志
 * 2. 本函数从请求中解析出 pipe（CRTC 索引）和 flags
 * 3. 创建 pending event 并挂到 priv->vcnt[pipe].event
 * 4. 当下一次 VOP2 中断到来时，vop2_handle_vcnt() 会：
 *    - 填充时间戳和序列号
 *    - 通过 drm_send_event() 发送给用户空间
 *    - 用户空间通过 read() 或 poll() 接收事件
 *
 * pipe 编号解析逻辑（兼容标准 VBlank 的编码方式）：
 * - 高位编码（_DRM_VBLANK_HIGH_CRTC_MASK）：支持多 CRTC（>2 个）
 * - 低位编码（_DRM_VBLANK_SECONDARY）：简单的 0/1 选择（旧式，仅两个 CRTC）
 *
 * rockchip_drm_get_vcnt_event_ioctl - 处理用户空间请求注册 vcnt 事件的 ioctl 接口
 *
 * @dev:       DRM 核心的 drm_device 设备指针
 * @data:      用户空间传入的参数（类型为 union drm_wait_vblank *，包含请求信息）
 * @file_priv: 当前发起请求的 drm_file 句柄，代表调用进程
 */
static int rockchip_drm_get_vcnt_event_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file_priv)
{
	struct rockchip_drm_private *priv = dev->dev_private;

	/*
	 * data 是用户空间传入的 ioctl 参数。
	 * 复用了标准 VBlank 的数据结构 union drm_wait_vblank，里面有：
	 *   .request.type:   标志位 + CRTC 编号（打包在一个 u32 里）
	 *   .request.signal: 用户自定义数据（会原样回传给用户空间）
	 */

	/*
	 * drm_wait_vblank 是 DRM 子系统用于等待垂直同步（VBlank）和事件通知的标准用户空间请求结构。
	 * 它通常作为ioctl参数传递，包括：
	 *   union drm_wait_vblank {
	 *       struct drm_wait_vblank_request {
	 *           __u32 type;     // 类型和相关标志位。编码了CRTC编号、事件类型、行为等信息。
	 *           __u32 sequence; // 用于绝对或相对的帧序列号等待
	 *           __u32 signal;   // 用户自定义数据，事件到达时原样返回，便于用户做关联
	 *       } request;
	 *       struct drm_wait_vblank_reply {
	 *           __u32 type;         // 返回的类型与请求时一致
	 *           __u32 sequence;     // 当前/实际生效的帧序列号
	 *           __s64 tval_sec;     // 时间戳（秒）
	 *           __s64 tval_usec;    // 时间戳（微秒）
	 *       } reply;
	 *   };
	 * 在本驱动的 VCNT 扩展ioctl/事件里，利用了其中的 request.type（编码flags和CRTC），
	 * request.signal（用户上下文），由内核在事件完成时填充 reply 结构返回给用户。
	 */
	union drm_wait_vblank *vblwait = data;
	/* 指向等待发送到用户空间的 VBlank 事件（用于通知应用新的垂直同步/VSync 到来，例如页面翻转完成）。 */
	struct drm_pending_vblank_event *e;
	struct drm_crtc *crtc;
	unsigned int flags, pipe;

	/*
	 * 从 type 字段提取标志位。
	 *
	 * type 字段的位布局（定义在 uapi/drm/drm.h）：
	 *
	 *   bit 31: _DRM_ROCKCHIP_VCNT_EVENT (0x80000000) ← Rockchip 私有
	 *   bit 30: _DRM_VBLANK_SIGNAL       (0x40000000) ← 已废弃
	 *   bit 29: _DRM_VBLANK_SECONDARY    (0x20000000) ← 旧式 CRTC 选择
	 *   bit 28: _DRM_VBLANK_NEXTONMISS   (0x10000000) ← 错过则等下一个
	 *   bit 26: _DRM_VBLANK_EVENT        (0x04000000) ← 发送事件而非阻塞
	 *   bit 1-5: HIGH_CRTC_MASK          (0x0000003e) ← 新式 CRTC 编号
	 *   bit 0:   类型（ABSOLUTE/RELATIVE）
	 *
	 * 这里只关心标志位部分 + Rockchip 私有的 vcnt 标志。
	 */
	flags = vblwait->request.type & (_DRM_VBLANK_FLAGS_MASK | _DRM_ROCKCHIP_VCNT_EVENT);

	/*
	 * 从 type 字段解析 CRTC 编号（pipe）。
	 *
	 * 历史演进：
	 *
	 * 旧方案（只支持 2 个 CRTC）：
	 *   bit 29 (_DRM_VBLANK_SECONDARY)：
	 *     0 → pipe 0（主显示器）
	 *     1 → pipe 1（副显示器）
	 *
	 * 新方案（支持最多 16 个 CRTC）：
	 *   bit 1-5 (_DRM_VBLANK_HIGH_CRTC_MASK)：
	 *     存储 CRTC 编号（右移 1 位后得到实际索引）
	 *     例如：bit[1:5] = 0b00010 → 右移 1 位 → pipe = 1
	 *           bit[1:5] = 0b00100 → 右移 1 位 → pipe = 2
	 *
	 * 优先使用新方案（非零说明用户使用了新编码）；
	 * 如果新方案字段为 0，回退到旧方案。
	 */
	pipe = (vblwait->request.type & _DRM_VBLANK_HIGH_CRTC_MASK);
	if (pipe)
		pipe = pipe >> _DRM_VBLANK_HIGH_CRTC_SHIFT; /* 新方案：右移得到索引 */
	else
		pipe = flags & _DRM_VBLANK_SECONDARY ? 1 : 0; /* 旧方案：0 或 1 */

	/* 通过 pipe 索引找到对应的 drm_crtc 结构体 */
	crtc = drm_crtc_from_index(dev, pipe);

	/*
	 * 如果用户设置了 Rockchip 私有的 vcnt 事件标志，
	 * 创建一个 pending event 并挂到对应 pipe 的 vcnt 槽位上。
	 *
	 * 只有一个槽位（priv->vcnt[pipe].event），所以：
	 * - 每个 CRTC 同时只能有一个 vcnt 事件在等待
	 * - 新的请求会覆盖旧的（如果旧的还没被处理）
	 * - vop2_handle_vcnt() 在中断中会消费掉这个 event 并清空槽位
	 */
	if (flags & _DRM_ROCKCHIP_VCNT_EVENT) {
		e = rockchip_drm_add_vcnt_event(crtc, vblwait, file_priv);
		priv->vcnt[pipe].event = e;
	}

	return 0;
}

//ROCKCHIP_GEM_CREATE等定义在kernel\include\uapi\drm\rockchip_drm.h文件中
static const struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_gem_create_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			  rockchip_gem_map_offset_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET_PHYS, rockchip_gem_get_phys_ioctl,
			  DRM_UNLOCKED | DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GET_VCNT_EVENT, rockchip_drm_get_vcnt_event_ioctl,
			  DRM_UNLOCKED),
};

static const struct file_operations rockchip_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = rockchip_gem_mmap,
	.poll = drm_poll,
	.read = drm_read, //读在这里
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.release = drm_release,
};

static int rockchip_drm_gem_dmabuf_begin_cpu_access(struct dma_buf *dma_buf,
						    enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_begin_cpu_access(obj, dir);
}

static int rockchip_drm_gem_dmabuf_end_cpu_access(struct dma_buf *dma_buf,
						  enum dma_data_direction dir)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_end_cpu_access(obj, dir);
}

static int rockchip_drm_gem_begin_cpu_access_partial(
	struct dma_buf *dma_buf,
	enum dma_data_direction dir,
	unsigned int offset, unsigned int len)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_begin_cpu_access_partial(obj, dir, offset, len);
}

static int rockchip_drm_gem_end_cpu_access_partial(
	struct dma_buf *dma_buf,
	enum dma_data_direction dir,
	unsigned int offset, unsigned int len)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return rockchip_gem_prime_end_cpu_access_partial(obj, dir, offset, len);
}

static const struct dma_buf_ops rockchip_drm_gem_prime_dmabuf_ops = {
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.release = drm_gem_dmabuf_release,
	.map = drm_gem_dmabuf_kmap,
	.unmap = drm_gem_dmabuf_kunmap,
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
	.begin_cpu_access = rockchip_drm_gem_dmabuf_begin_cpu_access,
	.end_cpu_access = rockchip_drm_gem_dmabuf_end_cpu_access,
	.begin_cpu_access_partial = rockchip_drm_gem_begin_cpu_access_partial,
	.end_cpu_access_partial = rockchip_drm_gem_end_cpu_access_partial,
};

/*
 * ============================================================
 * PRIME / DMA-BUF 跨设备缓冲区共享
 * ============================================================
 *
 * 【背景：为什么需要 PRIME？】
 *
 * 在 SoC 系统中，多个硬件单元需要共享同一块内存：
 *
 *   VPU（视频解码）──解码帧──► DRM（VOP2 显示）
 *   Camera ISP ──捕获帧──► GPU（渲染）──► DRM（合成显示）
 *   GPU（渲染）──► VPU（视频编码）
 *
 * 如果没有统一的共享机制，每次传递都必须：CPU 读取 → 拷贝 → 写入 → 通知
 * 这意味着巨大的带宽浪费和延迟。
 *
 * PRIME（DRM Buffer Sharing）通过 dma_buf 机制解决了这个问题：
 *   - 内存只分配一次，多设备共享同一物理页
 *   - 通过文件描述符在进程/驱动间传递"权柄"
 *   - 每个设备建立自己的 IOMMU 映射（dma_buf_attachment），无需数据拷贝
 *
 * 【关键数据结构关系】
 *
 *   用户空间 fd（整数）
 *        │  dma_buf_get(fd)
 *        ▼
 *   struct dma_buf                  ← 共享缓冲区的"身份证"
 *     .ops   = rockchip_drm_gem_prime_dmabuf_ops
 *     .priv  = struct drm_gem_object  ← 导出方的 GEM 对象
 *     .resv  = reservation_object     ← fence 同步对象
 *        │  dma_buf_attach(dmabuf, dev)
 *        ▼
 *   struct dma_buf_attachment       ← "某设备对该 buffer 的使用声明"
 *     .dmabuf = dma_buf
 *     .dev    = attach_dev
 *        │  dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL)
 *        ▼
 *   struct sg_table                 ← 该设备可用的物理地址散列表
 *     （IOMMU 会将其映射为 IOVA 供硬件 DMA 使用）
 *        │  gem_prime_import_sg_table(dev, attach, sgt)
 *        ▼
 *   struct drm_gem_object (import)  ← 导入方的 GEM 对象，包装上述 sgt
 *     .import_attach = attach
 *
 * 【CONFIG_DMABUF_CACHE 说明】
 *
 *   这是 Rockchip 对标准 dma_buf 的性能优化扩展：
 *   - 标准路径：每次 import 都要 attach + map（IOMMU 建表，耗时）
 *   - 缓存路径：attach/map 结果被缓存，同一 dma_buf 再次 import 时
 *               直接复用已有的 attachment 和 sgt，跳过 IOMMU 重映射
 *
 *   非缓存路径（!CONFIG_DMABUF_CACHE）使用 release callback 机制：
 *   在 dma_buf 上挂一个回调，当 dma_buf 引用计数归零时，
 *   自动清理本驱动为其创建的 import 资源（sgt、attachment、GEM 对象）。
 */

/*
 * drm_gem_prime_dmabuf_release_callback - dma_buf 释放时的清理回调
 *
 * 仅在 !CONFIG_DMABUF_CACHE 时编译。
 *
 * 【触发时机】
 *   当某个 *外部* dma_buf 的文件引用计数归零（即所有持有者都关闭了 fd），
 *   dma_buf 核心层会调用此回调，通知曾经 import 过这个 dma_buf 的 Rockchip DRM 驱动。
 *
 * 【清理步骤】
 *   1. dma_buf_unmap_attachment()：撤销当初 dma_buf_map_attachment() 建立的 sg_table
 *      映射，释放 IOMMU 表项（IOVA → 物理地址的映射）。
 *   2. dma_buf_detach()：断开 attachment，通知 dma_buf 导出方此设备不再使用该 buffer。
 *   3. drm_gem_object_put_unlocked()：释放 Rockchip GEM import 对象的引用计数，
 *      若计数归零则销毁该 GEM 对象并释放其持有的资源。
 *   4. kfree(cb_data)：释放回调数据结构本身。
 *
 * 【为什么需要这个回调，而不是在 GEM 对象释放时清理？】
 *   import GEM 对象的生命周期 ≤ 被 import 的 dma_buf 的生命周期。
 *   当 dma_buf 即将释放时，必须先撤销 attach/map，否则导出方的内存页
 *   被释放后，attachment 和 sgt 将成为野指针。
 *   此回调确保以正确的顺序、在正确的时机清理资源。
 *
 * @data: 指向 struct drm_prime_callback_data，包含 obj（GEM对象）和 sgt（散列表）
 */
#if !defined(CONFIG_DMABUF_CACHE)
static void drm_gem_prime_dmabuf_release_callback(void *data)
{
	struct drm_prime_callback_data *cb_data = data;

	if (cb_data && cb_data->obj && cb_data->obj->import_attach) {
		struct dma_buf_attachment *attach = cb_data->obj->import_attach;
		struct sg_table *sgt = cb_data->sgt;

		/* 步骤1：解除 IOMMU 映射，释放 sg_table 中的 IOVA 资源 */
		if (sgt)
			dma_buf_unmap_attachment(attach, sgt,
						 DMA_BIDIRECTIONAL);
		/* 步骤2：断开 attachment，通知导出方本设备已离场 */
		dma_buf_detach(attach->dmabuf, attach);
		/* 步骤3：递减 import GEM 对象引用计数，可能触发其销毁 */
		drm_gem_object_put_unlocked(cb_data->obj);
		/* 步骤4：释放回调数据结构 */
		kfree(cb_data);
	}
}
#endif

/*
 * rockchip_drm_gem_prime_import_dev - 将外部 dma_buf 导入为 Rockchip GEM 对象
 *
 * 这是 PRIME import 的核心实现，包含三条代码路径，从快到慢依次尝试：
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  路径1（最快）：自导入（Self-Import）                                     │
 * │  条件：dma_buf 是由本 Rockchip DRM 设备导出的（ops 指针匹配，且 dev 相同）  │
 * │  动作：直接递增 GEM 对象引用计数并返回，完全跳过 IOMMU 操作               │
 * │  场景：用户空间先 export 一个 GEM buffer 为 fd，再将该 fd 传回本驱动 import │
 * │        （如 wayland compositor 的零拷贝共享路径）                         │
 * │                                                                          │
 * │  路径2（较快，仅 !CONFIG_DMABUF_CACHE）：回调缓存命中                      │
 * │  条件：本驱动之前已经 import 过该 dma_buf，并留下了 release callback       │
 * │  动作：从回调数据中取出已有的 GEM 对象，递增引用计数返回                   │
 * │  场景：同一个 dma_buf 被反复 import（如多次提交同一帧 buffer）              │
 * │  优点：避免重复的 dma_buf_attach() 和 IOMMU 重新建表（后者代价较高）        │
 * │                                                                          │
 * │  路径3（完整流程）：首次 import                                            │
 * │  步骤：attach → get_dma_buf → (alloc cb_data) → map_attachment            │
 * │         → import_sg_table → 记录 import_attach                           │
 * │         → (set_release_callback + dma_buf_put + gem_get)                 │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * 【路径3详解：完整 import 流程】
 *
 *   ① dma_buf_attach(dma_buf, attach_dev)
 *      - 向 dma_buf 导出方声明"本设备要使用此 buffer"
 *      - 导出方驱动的 .attach 回调被触发（如 drm_gem_map_attach()），
 *        可以做设备相关的准备（如 pin 住内存页，禁止迁移）
 *      - 返回 struct dma_buf_attachment，代表此设备与该 buffer 的绑定关系
 *
 *   ② get_dma_buf(dma_buf)
 *      - 递增 dma_buf 的文件引用计数（file->f_count）
 *      - 目的：防止在我们还持有 attachment 期间，dma_buf 被其他路径释放
 *      - 对应的 dma_buf_put() 在后续恰当时机调用（见下文 ③-d）
 *
 *   ③-a dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL)
 *      - 这是最耗时的步骤：触发导出方驱动的 .map_dma_buf 回调
 *      - 回调内部通常调用 dma_map_sg()，在 IOMMU 中建立
 *        "IOVA（设备虚拟地址）→ 物理地址" 的映射表
 *      - 返回 struct sg_table，包含了 attach_dev 可用的 IOVA 地址列表
 *      - CONFIG_DMABUF_CACHE 就是为了缓存这一步的结果
 *
 *   ③-b gem_prime_import_sg_table(dev, attach, sgt)
 *      - 驱动自定义的回调（Rockchip 为 rockchip_gem_prime_import_sg_table()）
 *      - 将 sg_table 包装成一个 GEM 对象（struct drm_gem_object）
 *      - GEM 对象将成为 DRM 框架内代表此 buffer 的句柄
 *
 *   ③-c obj->import_attach = attach
 *      - 记录 attachment，以便后续 GEM 对象销毁时知道如何清理
 *
 *   ③-d（仅 !CONFIG_DMABUF_CACHE）注册 release callback
 *      - dma_buf_set_release_callback()：将 drm_gem_prime_dmabuf_release_callback
 *        和 cb_data（含 obj、sgt 指针）绑定到 dma_buf 上
 *      - dma_buf_put()：与步骤 ② 的 get_dma_buf() 配对，释放本函数持有的引用
 *        此后 dma_buf 的生命周期由其他持有者（用户空间 fd 等）维持
 *      - drm_gem_object_get()：额外增加 GEM 对象的引用计数
 *        保证即使用户空间不再持有 GEM handle，GEM 对象也能存活到 dma_buf 释放
 *        （因为 release callback 还需要访问 obj）
 *
 * 【引用计数平衡总结（!CONFIG_DMABUF_CACHE 路径）】
 *
 *   dma_buf 引用（f_count）：
 *     get_dma_buf()   +1  →  dma_buf_put()   -1  （本函数内平衡）
 *   GEM 对象引用：
 *     import_sg_table 创建时 = 1  （基础引用，由用户空间 GEM handle 持有）
 *     drm_gem_object_get()    +1  （release callback 持有，直到 dma_buf 释放）
 *
 * @dev:        Rockchip DRM device，目标 GEM 对象归属的设备
 * @dma_buf:    要 import 的外部共享缓冲区
 * @attach_dev: 执行 DMA 操作的实际设备（通常即 dev->dev，但 IOMMU 场景下可指定子设备）
 * @return:     成功返回新 GEM 对象指针，失败返回 ERR_PTR(-errno)
 */
static struct drm_gem_object *rockchip_drm_gem_prime_import_dev(struct drm_device *dev,
								struct dma_buf *dma_buf,
								struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct drm_gem_object *obj;
#if !defined(CONFIG_DMABUF_CACHE)
	struct drm_prime_callback_data *cb_data = NULL;
#endif
	int ret;

	/*
	 * 【路径1：自导入快速路径】
	 * 检查 dma_buf 是否由本 Rockchip DRM 设备导出：
	 *   - ops 指针相同（同一套操作函数表）→ 是 Rockchip DRM 格式的 dma_buf
	 *   - obj->dev == dev → 是本设备导出的（而不是另一个 Rockchip DRM 实例）
	 *
	 * 此时无需经过 IOMMU 重映射，直接递增 GEM 对象的引用计数即可。
	 * 语义：import 等同于"我也持有这个 GEM 对象"，引用计数 +1 表示新的持有者。
	 */
	if (dma_buf->ops == &rockchip_drm_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * 从自身导出的 dma_buf 再次 import 时，
			 * 递增 GEM 对象自身引用计数（而非 dma_buf 文件引用计数），
			 * 因为 GEM 对象就是 dma_buf 的 priv，两者共生。
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}


/*

## 为什么是 `!defined(CONFIG_DMABUF_CACHE)`？
### 问题背景：重复 import 的开销

`dma_buf_attach()` + `dma_buf_map_attachment()` 
在 IOMMU 场景下代价很高（需要建 IOVA 映射表）。
同一个 `dma_buf` 如果被反复 import（比如同一个视频帧 buffer 每帧都提交一次），
就会反复触发这些昂贵操作。

### 两种解决方案

**方案 A：`CONFIG_DMABUF_CACHE`（启用时）**

Rockchip 在 `dma_buf` 框架层面直接做了缓存：
- `dma_buf_cache_attach()` / `dma_buf_cache_map_attachment()` 替换了原生 API
- attachment 和 sgt 的结果**永久缓存在 dma_buf 结构体内**
- 任何驱动再次 attach/map 同一个 dma_buf 时，**直接命中缓存，不走 IOMMU 重映射**
- 这是一个**系统级缓存**，对所有使用者透明

此时，路径2和 release callback 完全**没有存在的必要**，因为缓存机制已经在更低层解决了重复开销的问题。

---

**方案 B：`!CONFIG_DMABUF_CACHE`（未启用时）**

没有系统级缓存，Rockchip DRM 驱动就在**自己这一层**做了一个"软缓存"：
- 首次 import 成功后，通过 `dma_buf_set_release_callback()` 在 `dma_buf` 上挂一个钩子，附带已建好的 `obj` 和 `sgt`
- 再次 import 同一个 `dma_buf` 时，`dma_buf_get_release_callback_data()` 发现钩子存在，直接取出缓存的 `obj` 返回（路径2）
- 这是一个**驱动私有缓存**，只对 Rockchip DRM 有效

---

对比总结：
			CONFIG_DMABUF_CACHE	      !CONFIG_DMABUF_CACHE
缓存层次	dma_buf 框架层（系统级）	Rockchip DRM 驱动层（私有）
缓存内容	attachment + sg_table	  GEM对象 + sg_table
缓存粒度	以（dmabuf, device）为 key，链表支持多设备	以（dmabuf, 本驱动）为 key，单条目
缓存方式	替换 attach/map API，dmabuf->dtor_data	release callback + cb_data
有效范围	所有驱动共享			   仅 Rockchip DRM 自己用
release callback	不需要	          必须，作为"缓存容器"

两套方案的核心都是缓存 sg_table，
因为 dma_buf_map_attachment() 触发 IOMMU 建表是最贵的操作。
区别在于缓存粒度：DMABUF_CACHE 更细（可复用 attachment），
release callback 方案更粗（直接复用整个 GEM 对象）。 

*/

#if !defined(CONFIG_DMABUF_CACHE)
	/*
	 * 【路径2：回调缓存命中（!CONFIG_DMABUF_CACHE 专用）】
	 * 查询 dma_buf 上是否已经挂了本驱动的 release callback：
	 *   - 若存在，说明本驱动之前已经完整 import 过此 dma_buf
	 *   - 从 cb_data 中取出之前创建的 GEM 对象，直接递增引用计数返回
	 *   - 避免重复 attach + map，节省 IOMMU 建表开销
	 *
	 * 注意：还需确认 cb_data->obj->dev == dev，
	 * 防止不同 Rockchip DRM 实例之间互相误用。
	 */
	cb_data = dma_buf_get_release_callback_data(dma_buf,
					drm_gem_prime_dmabuf_release_callback);
	if (cb_data && cb_data->obj && cb_data->obj->dev == dev) {
		drm_gem_object_get(cb_data->obj);
		return cb_data->obj;
	}
#endif

	/* 驱动必须实现 gem_prime_import_sg_table 才能接受外部 buffer */
	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);

	/*
	 * 【路径3：完整 import 流程 —— 步骤①】
	 * attach：向 dma_buf 导出方声明本设备将使用此 buffer。
	 * 触发导出方驱动的 .attach 回调，可能做 pin 内存等准备工作。
	 */
	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	/*
	 * 步骤②：递增 dma_buf 文件引用计数，防止在 map_attachment 过程中被释放。
	 * 对应的 dma_buf_put() 在注册完 release callback 后立即调用（见下文），
	 * 此后 dma_buf 的生命周期交由调用者和 release callback 共同管理。
	 */
	get_dma_buf(dma_buf);

#if !defined(CONFIG_DMABUF_CACHE)
	/* 预先分配 release callback 的数据结构，失败则回滚 attach */
	cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
	if (!cb_data) {
		ret = -ENOMEM;
		goto fail_detach;
	}
#endif

	/*
	 * 步骤③-a：map_attachment — 最核心也是最耗时的步骤。
	 * 触发 .map_dma_buf 回调（drm_gem_map_dma_buf），内部调用 dma_map_sg()，
	 * 在 IOMMU 中建立"IOVA → 物理地址"的映射表，
	 * 返回 sg_table（设备可用的散列地址表）。
	 * CONFIG_DMABUF_CACHE 通过缓存此 sgt，避免重复建表。
	 */
	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	/*
	 * 步骤③-b：将 sg_table 包装为 Rockchip GEM 对象。
	 * 实际调用 rockchip_gem_prime_import_sg_table()，
	 * 该函数分配 struct rockchip_gem_object，记录物理地址或 IOVA，
	 * 以便 VOP2 硬件 DMA 直接读取帧数据。
	 */
	obj = dev->driver->gem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	/*
	 * 步骤③-c：记录 attachment，GEM 对象销毁时据此清理 map/attach 资源。
	 * 若未来调用 drm_gem_object_free()，会通过此指针调用
	 * dma_buf_unmap_attachment() 和 dma_buf_detach()。
	 */
	obj->import_attach = attach;

#if !defined(CONFIG_DMABUF_CACHE)
	/*
	 * 步骤③-d（!CONFIG_DMABUF_CACHE）：注册 release callback 并平衡引用计数。
	 *
	 * ① 填充 cb_data，记录 obj 和 sgt，供回调清理时使用。
	 * ② dma_buf_set_release_callback()：将回调绑定到 dma_buf，
	 *    当 dma_buf 最终释放时，自动调用 drm_gem_prime_dmabuf_release_callback()
	 *    完成 unmap → detach → gem_put 的清理序列。
	 * ③ dma_buf_put()：与步骤② 的 get_dma_buf() 配对，释放本函数的临时引用。
	 *    此后 dma_buf 由用户空间 fd 持有，本驱动通过 release callback 感知其释放。
	 * ④ drm_gem_object_get()：额外增加 GEM 对象引用，防止 GEM handle 被关闭后
	 *    GEM 对象提前销毁（release callback 还需要访问 obj 进行清理）。
	 *    此引用由 release callback 中的 drm_gem_object_put_unlocked() 释放。
	 */
	cb_data->obj = obj;
	cb_data->sgt = sgt;
	dma_buf_set_release_callback(dma_buf,
			drm_gem_prime_dmabuf_release_callback, cb_data);
	dma_buf_put(dma_buf);        /* ③ 释放本函数的临时 dma_buf 引用 */
	drm_gem_object_get(obj);     /* ④ release callback 的额外持有引用 */
#endif

	return obj;

fail_unmap:
	/* map_attachment 之后失败：先撤销 IOMMU 映射 */
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
#if !defined(CONFIG_DMABUF_CACHE)
	kfree(cb_data);
#endif
	/* 撤销 attach，通知导出方本设备放弃使用 */
	dma_buf_detach(dma_buf, attach);
	/* 与 get_dma_buf() 配对，释放对 dma_buf 的临时引用 */
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

/*
 * rockchip_drm_gem_prime_import - PRIME import 的标准入口
 *
 * 挂载到 struct drm_driver.gem_prime_import，
 * 由 DRM 核心在用户空间调用 DRM_IOCTL_PRIME_FD_TO_HANDLE 时触发。
 *
 * 这是一个薄封装，固定以 dev->dev（Rockchip DRM 平台设备）作为
 * attach_dev 参数传入 rockchip_drm_gem_prime_import_dev()。
 *
 * attach_dev 的作用：dma_buf_attach(dma_buf, attach_dev) 会根据
 * attach_dev 所属的 IOMMU group，在该设备的 IOMMU domain 中建立
 * IOVA → 物理地址的页表映射。
 *
 * RK3568 上 VOP2 的所有 VP 共享同一个 IOMMU domain（即 dev->dev 的 domain），
 * 因此所有 import 统一用 dev->dev 即可，无需区分具体的 VP。
 */
static struct drm_gem_object *rockchip_drm_gem_prime_import(struct drm_device *dev,
							    struct dma_buf *dma_buf)
{
	return rockchip_drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}

/*
 * rockchip_drm_gem_prime_export - 将 Rockchip GEM 对象导出为 dma_buf
 *
 * 这是挂载到 struct drm_driver.gem_prime_export 的标准回调，
 * 由 DRM 核心在用户空间调用 DRM_IOCTL_PRIME_HANDLE_TO_FD 时触发。
 *
 * 【导出流程】
 *   1. 填充 dma_buf_export_info，描述要导出的 buffer 的属性和操作函数表。
 *   2. 若驱动实现了 gem_prime_res_obj()，将 GEM 的 reservation_object
 *      （即 fence 同步对象）传递给 dma_buf，使其他驱动 import 后
 *      能感知本 buffer 上的 GPU fence，实现跨设备显式同步。
 *   3. drm_gem_dmabuf_export()：创建 dma_buf 文件并返回。
 *      用户空间通过 DRM_IOCTL_PRIME_HANDLE_TO_FD 获得该文件的 fd，
 *      再将 fd 传递给其他进程或驱动（如 VPU、Camera 驱动）进行 import。
 *
 * 【exp_info 字段说明】
 *   .exp_name = KBUILD_MODNAME：调试用名称（"rockchip"），
 *               注释 "white lie for debug" 表明这只是标识符，并非精确来源。
 *   .owner    = dev->driver->fops->owner：模块所有者，防止 dma_buf 存活期间模块被卸载。
 *   .ops      = rockchip_drm_gem_prime_dmabuf_ops：Rockchip 定制的 dma_buf 操作函数表，
 *               包含 attach/detach/map/unmap/cpu_access 等回调，
 *               也是路径1自导入检测的"指纹"（通过 ops 指针比较识别来源）。
 *   .size     = obj->size：buffer 大小（字节），暴露给 import 方。
 *   .flags    = flags：O_CLOEXEC 等标志，控制 fd 行为。
 *   .priv     = obj：dma_buf->priv 指向 GEM 对象，是 import 路径1快速返回的基础。
 *   .resv     = reservation_object：fence 同步对象，实现"GPU写完，显示才读"的显式同步。
 *
 * @dev:   Rockchip DRM device
 * @obj:   要导出的 GEM 对象
 * @flags: 文件标志（O_CLOEXEC 等）
 * @return: 指向新创建的 dma_buf 的指针，或 ERR_PTR(-errno)
 */
static struct dma_buf *rockchip_drm_gem_prime_export(struct drm_device *dev,
						     struct drm_gem_object *obj,
						     int flags)
{
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
		/* 挂载 Rockchip 定制操作表，也作为自导入快速路径的"指纹" */
		.ops = &rockchip_drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		/* dma_buf->priv 指向 GEM 对象，自导入路径据此直接返回 obj */
		.priv = obj,
	};

	/* 【笔记钩子】
	 * 【跨设备同步问题】
	 * GPU 渲染一帧后，VPU/Camera 等其他设备通过 PRIME fd 拿到这块 buffer 并读取。
	 * 但 GPU 的写操作是异步的——用户空间提交 draw call 后 GPU 尚未完成，
	 * 其他设备若立即 DMA 读取，就会拿到渲染到一半的"脏帧"。
	 *
	 * 解决方案是 reservation_object（内含 exclusive fence）：
	 *   - GPU 提交工作时在 resv 上挂一个 exclusive fence（表示"我还没写完"）
	 *   - GPU 真正写完后，fence 触发（signal）
	 *   - import 方在 DMA 读取前调用 dma_buf_reservation_object_get() 拿到 resv，
	 *     等待 exclusive fence 触发，确认 GPU 写完后再开始读取
	 *
	 * 【为何要填 exp_info.resv】
	 * GEM 对象本身已经携带了一个 reservation_object（GPU 驱动往里写 fence）。
	 * 若不填 exp_info.resv，dma_buf 框架会给这个 dma_buf 分配一个全新的、
	 * 独立的 reservation_object——它和 GEM 的 resv 是两个不同的对象，
	 * GPU 往 GEM resv 里写的 fence，import 方从 dma_buf resv 里根本看不到，
	 * 跨设备同步就彻底失效了。
	 *
	 * 因此，通过 gem_prime_res_obj() 取出 GEM 自身的 resv 填入 exp_info，
	 * 让 dma_buf 与 GEM 共享同一个 reservation_object，
	 * 保证 import 方等到的 fence 就是 GPU 真正写完时触发的那个。
	 */
	if (dev->driver->gem_prime_res_obj)
		exp_info.resv = dev->driver->gem_prime_res_obj(obj);

	return drm_gem_dmabuf_export(dev, &exp_info);
}

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM |
				  DRIVER_PRIME | DRIVER_ATOMIC |
				  DRIVER_RENDER,
	.postclose		= rockchip_drm_postclose,
	.lastclose		= rockchip_drm_lastclose,
	.open			= rockchip_drm_open,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.gem_free_object_unlocked = rockchip_gem_free_object,
	.dumb_create		= rockchip_gem_dumb_create,
	.dumb_map_offset	= rockchip_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= rockchip_drm_gem_prime_import,
	.gem_prime_export	= rockchip_drm_gem_prime_export,
	.gem_prime_get_sg_table	= rockchip_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	.gem_prime_vmap		= rockchip_gem_prime_vmap,
	.gem_prime_vunmap	= rockchip_gem_prime_vunmap,
	.gem_prime_mmap		= rockchip_gem_mmap_buf,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= rockchip_drm_debugfs_init,
#endif
	.ioctls			= rockchip_ioctls,
	.num_ioctls		= ARRAY_SIZE(rockchip_ioctls),
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
	.patchlevel	= DRIVER_PATCH,
};

#ifdef CONFIG_PM_SLEEP
static void rockchip_drm_fb_suspend(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 1);
	console_unlock();
}

static void rockchip_drm_fb_resume(struct drm_device *drm)
{
	struct rockchip_drm_private *priv = drm->dev_private;

	console_lock();
	drm_fb_helper_set_suspend(priv->fbdev_helper, 0);
	console_unlock();
}

static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv;

	if (!drm)
		return 0;

	drm_kms_helper_poll_disable(drm);
	rockchip_drm_fb_suspend(drm);

	priv = drm->dev_private;
	priv->state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(priv->state)) {
		rockchip_drm_fb_resume(drm);
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(priv->state);
	}

	return 0;
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct rockchip_drm_private *priv;

	if (!drm)
		return 0;

	priv = drm->dev_private;
	drm_atomic_helper_resume(drm, priv->state);
	rockchip_drm_fb_resume(drm);
	drm_kms_helper_poll_enable(drm);

	return 0;
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

#define MAX_ROCKCHIP_SUB_DRIVERS 16
static struct platform_driver *rockchip_sub_drivers[MAX_ROCKCHIP_SUB_DRIVERS];
static int num_rockchip_sub_drivers;

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static void rockchip_drm_match_remove(struct device *dev)
{
	struct device_link *link;

	list_for_each_entry(link, &dev->links.consumers, s_node)
		device_link_del(link);
}

/**
 * rockchip_drm_match_add - 扫描总线，为 component master 构建子组件匹配列表
 * @dev: Rockchip DRM master 设备（display-subsystem 节点对应的 platform_device）
 *
 * 返回值：成功返回填充好的 component_match 指针；失败返回 ERR_PTR(-ENODEV)
 *
 * ## 背景：component 框架的工作原理
 *
 * Linux component 框架用于解决多驱动聚合问题：
 * 一个复杂设备（如 Rockchip DRM）由多个相互独立的子驱动构成
 * （VOP2、DSI、HDMI、eDP 等），只有当**所有子组件**都 probe 完成后，
 * master 驱动才能完整地初始化整个显示子系统。
 *
 * 流程概览：
 *   ① 内核启动时，子驱动（如 dw_mipi_dsi_driver）probe 成功后
 *      调用 component_add()，将自己注册为"待聚合的子组件"
 *   ② master（本驱动）probe 时调用本函数，扫描总线找出所有匹配的子设备，
 *      构建 match 列表，再调用 component_master_add_with_match()
 *   ③ component 框架检查 match 列表中的所有子组件是否都已就绪，
 *      若全部就绪则调用 master 的 .bind()（rockchip_drm_bind），
 *      否则等待剩余子组件 probe 完成后自动触发
 *
 * ## 本函数的核心逻辑：遍历所有已注册的子驱动，找到对应的设备实例
 *
 * rockchip_sub_drivers[] 是在 rockchip_drm_init() 中通过
 * ADD_ROCKCHIP_SUB_DRIVER 宏注册的子驱动指针数组，包含：
 *   vop2_platform_driver        → VOP2（显示控制器，提供 CRTC/Plane）
 *   dw_mipi_dsi_driver          → MIPI DSI 控制器（提供 Encoder/Connector）
 *   dw_hdmi_rockchip_pltfm_driver → HDMI 控制器
 *   rockchip_dp_driver          → eDP/DP 控制器
 *   ... 等
 *
 * 对于每一个子驱动，需要找到总线上**所有**由该驱动管理的设备实例。
 * 之所以是"所有"而非"一个"，是因为同一个驱动可能管理多个设备
 * （如 DSI0 和 DSI1 都由 dw_mipi_dsi_driver 管理，需要都加入 match）。
 */
static struct component_match *rockchip_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		struct platform_driver *drv = rockchip_sub_drivers[i];
		struct device *p = NULL, *d;

		/*
		 * 内层 do-while 循环：在 platform_bus 上迭代查找由 drv 管理的所有设备。
		 *
		 * bus_find_device() 语义：
		 *   从链表中 p 节点的**下一个**开始遍历，逐个调用 match 回调，
		 *   返回第一个匹配的设备（已增加引用计数）。
		 *   传入 platform_bus_type.match 作为匹配函数，它检查设备的 driver 字段
		 *   是否等于 &drv->driver，即"该设备是否由此驱动管理"。
		 *
		 * 迭代机制（游标前进）：
		 *   p = NULL  → 第一次调用，从链表头开始搜索，找到第一个匹配设备 d1
		 *   put_device(p=NULL) → 对 NULL 无操作
		 *   p = d1    → 下一次从 d1 之后继续搜索，找到 d2（若存在）
		 *   put_device(d1)     → 释放上一个设备的引用，防止引用计数泄漏
		 *   p = d2    → 继续...
		 *   ...
		 *   d = NULL  → 没有更多匹配设备，break 退出循环
		 *   put_device(p=最后一个d) → 释放最后找到的设备引用
		 *
		 * 注意：找到设备 d 后，d 的引用计数已被 bus_find_device 增加，
		 * 在 put_device(p) 之前 p 仍持有上一个设备的引用，
		 * 赋值 p = d 转移了对 d 的"持有权"，循环末尾通过 put_device(p) 归还。
		 */
		do {
			d = bus_find_device(&platform_bus_type, p, &drv->driver,
					    (void *)platform_bus_type.match);
			put_device(p);  /* 释放上一轮找到的设备引用，p=NULL 时安全 */
			p = d;          /* 游标前进：下次从 d 之后继续搜索 */

			if (!d)
				break;  /* 此驱动在总线上已无更多设备，换下一个驱动 */

			/*
			 * device_link_add：在 master 设备与子设备之间建立设备依赖链接。
			 *
			 * DL_FLAG_STATELESS：无状态链接，不参与 PM 同步，
			 * 仅作为"master 依赖此子设备"的拓扑记录，
			 * 供 rockchip_drm_match_remove() 遍历并删除时使用。
			 *
			 * 建立链接的作用：
			 *   - 确保子设备在 master 之前 probe（内核 PM 域依赖顺序）
			 *   - master remove 时可通过遍历 dev->links.consumers 批量删除链接
			 *     （见 rockchip_drm_match_remove）
			 */
			device_link_add(dev, d, DL_FLAG_STATELESS);

			/*
			 * component_match_add：将设备 d 加入 match 列表。
			 *
			 * compare_dev 是比较函数：component_master 在检查某个 component
			 * 是否属于本 match 时，调用 compare_dev(component_dev, d)，
			 * 直接比较指针是否相等（见 compare_dev 实现）。
			 *
			 * component_match_add 内部动态分配/扩展 match 数组，
			 * 若分配失败则 match 被设为 ERR_PTR，后续统一检查。
			 */
			component_match_add(dev, &match, compare_dev, d);
		} while (true);
	}

	if (IS_ERR(match))
		rockchip_drm_match_remove(dev); /* 分配失败，清理已建立的所有 device_link */

	/*
	 * match == NULL 意味着没有找到任何子组件（所有子驱动在总线上均无对应设备），
	 * 通常发生在子驱动尚未 probe（返回 -ENODEV 触发 probe defer）或
	 * DTS 中没有使能任何显示相关节点的情况。
	 */
	return match ?: ERR_PTR(-ENODEV);
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

/**
 * rockchip_drm_platform_of_probe - 从 DTS 预检 VOP 拓扑并确定 IOMMU 支持模式
 * @dev: display-subsystem 节点对应的 platform_device.dev
 *
 * 返回值：0 成功；-ENODEV DTS 缺少 ports 属性或所有 VOP 均被禁用
 *
 * ## DTS 拓扑背景
 *
 * rk3568.dtsi 中的 display-subsystem 节点通过 "ports" 属性引用所有 VOP 输出端口：
 *
 *   display-subsystem {
 *       compatible = "rockchip,display-subsystem";
 *       ports = <&vop_out>;   ← phandle 数组，每项指向一个 VOP 的 ports 节点
 *   };
 *
 *   vop: vop@fe040000 {
 *       iommus = <&vop_mmu>;  ← VOP 的 IOMMU 节点
 *       vop_out: ports {      ← 被 display-subsystem.ports[0] 引用
 *           vp0: port@0 { ... }   ← VP0（CRTC0）
 *           vp1: port@1 { ... }   ← VP1（CRTC1）
 *           vp2: port@2 { ... }   ← VP2（CRTC2）
 *       };
 *   };
 *
 * 注意：ports 属性引用的是 VOP 的 ports **容器节点**（vop_out），
 * 而非具体的 VP 端口，port->parent 才是真正的 VOP 设备节点（vop@fe040000）。
 *
 * ## 函数职责（两项检查）
 *
 * ### 检查 1：确认至少有一个 VOP 可用
 *
 * 遍历 "ports" phandle 数组，跳过 parent 节点（VOP 设备）已禁用的条目。
 * 若所有 ports 条目的 parent 均处于 disabled 状态，返回 -ENODEV，
 * DRM master probe 失败，不进行后续的 component 聚合。
 *
 * ### 检查 2：全局 IOMMU 支持模式决策（"短板效应"）
 *
 * is_support_iommu 默认值：
 *   CONFIG_DRM_ROCKCHIP_VVOP 开启 → false（虚拟 VOP 无 IOMMU）
 *   正常硬件驱动            → true（假设所有 VOP 都有 IOMMU）
 *
 * 对每个可用 VOP，检查其 "iommus" 属性：
 *   情况 A：iommus 属性存在且对应节点可用（vop_mmu status = "okay"）
 *     → 该 VOP 支持 IOMMU，is_support_iommu 保持不变
 *
 *   情况 B：iommus 属性缺失，或 vop_mmu 节点被禁用
 *     → 该 VOP 不支持 IOMMU，强制 is_support_iommu = false
 *
 * "短板效应"：只要有一个 VOP 不支持 IOMMU，全局标志立即降为 false，
 * 后续所有 VOP 的帧缓冲分配都退回到物理连续内存（CMA）模式，
 * 不再尝试通过 IOMMU 做虚拟地址映射。
 *
 * 原因：DRM 的帧缓冲是全局共享的，合成器可能将同一块 GEM buffer
 * 同时显示在多个 CRTC 上，若不同 CRTC 所属 VOP 的 IOMMU 能力不一致，
 * 统一使用物理连续内存是最安全的保底策略。
 *
 * ## is_support_iommu 对后续流程的影响
 *
 * rockchip_gem_create()：
 *   is_support_iommu = true  → dma_alloc_attrs()，允许使用非连续内存 + IOMMU 映射
 *   is_support_iommu = false → dma_alloc_coherent()，强制物理连续内存（CMA）
 *
 * rockchip_drm_iommu_attach_device()：
 *   is_support_iommu = false → 直接跳过 IOMMU 初始化，不挂载 iommu_domain
 */
static int rockchip_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false; /* 是否找到至少一个可用 VOP */
	int i;

	/* display-subsystem 必须有 of_node，否则无法读取 ports 属性 */
	if (!np)
		return -ENODEV;

	/*
	 * 遍历 "ports" phandle 数组（ports[0]、ports[1]、...），
	 * of_parse_phandle(np, "ports", i) 返回第 i 个 phandle 指向的节点，
	 * 超出数组范围时返回 NULL，循环终止。
	 *
	 * 对于 RK3568，ports = <&vop_out>（只有一个元素），
	 * port 指向 vop_out（ports 容器节点），port->parent 是 vop@fe040000。
	 */
	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break; /* 数组已遍历完 */

		/*
		 * 检查 port 的父节点（VOP 设备节点）是否可用（status != "disabled"）。
		 * 若 VOP 被禁用，其下的所有 VP 端口也无意义，跳过本条目。
		 * of_node_put() 释放 of_parse_phandle 增加的引用计数，防止泄漏。
		 */
		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		/*
		 * 检查 VOP 设备节点的 "iommus" 属性：
		 * of_parse_phandle(port->parent, "iommus", 0) 获取第一个 IOMMU 节点
		 * （如 vop_mmu@fe043e00），再检查其父节点（MMU 控制器本体）是否可用。
		 *
		 * 两种情况触发降级（is_support_iommu = false）：
		 *   1. iommu == NULL：VOP DTS 节点根本没有 "iommus" 属性
		 *   2. !of_device_is_available(iommu->parent)：
		 *      iommu->parent 是 MMU 控制器节点，其 status = "disabled"
		 *      说明 IOMMU 硬件未启用
		 *
		 * 一旦降级，全局所有 VOP 都使用物理连续内存（"短板效应"）。
		 */
		iommu = of_parse_phandle(port->parent, "iommus", 0);
		if (!iommu || !of_device_is_available(iommu->parent)) {
			DRM_DEV_DEBUG(dev,
				      "no iommu attached for %pOF, using non-iommu buffers\n",
				      port->parent);
			/*
			 * 只要有一个 VOP 不支持 IOMMU，强制全局降级。
			 * 后续 rockchip_gem_create() 将使用物理连续内存。
			 */
			is_support_iommu = false;
		}

		found = true; /* 找到至少一个可用的 VOP */

		of_node_put(iommu); /* 释放 iommu 节点引用（iommu 为 NULL 时安全） */
		of_node_put(port);  /* 释放 port 节点引用 */
	}

	/*
	 * i == 0 说明 of_parse_phandle 第一次调用就返回了 NULL，
	 * 即 display-subsystem 节点完全没有 "ports" 属性，DTS 配置有误。
	 */
	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	/*
	 * i > 0 但 found == false：ports 数组非空，但所有引用的 VOP 均被禁用。
	 * 说明 DTS 中虽然列出了 VOP，但没有一个处于可用状态，无法初始化显示。
	 */
	if (!found) {
		DRM_DEV_ERROR(dev,
			      "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * rockchip_drm_platform_probe - Rockchip DRM master 驱动的 probe 函数
 * @pdev: display-subsystem 节点对应的 platform_device
 *
 * 本函数是整个 Rockchip DRM 显示子系统的"启动入口"，
 * 匹配 DTS 中 compatible = "rockchip,display-subsystem" 的节点后被调用。
 *
 * ## 三步初始化流程
 *
 * ### 第一步：OF 拓扑预检（rockchip_drm_platform_of_probe）
 *
 * 从 DTS 的 display-subsystem 节点出发，遍历其 "ports" 属性列出的所有
 * VOP 端口，做两项检查：
 *   检查 1：是否至少有一个可用的 VOP 端口（父节点 status != "disabled"）
 *   检查 2：每个 VOP 端口是否配置了 IOMMU（iommus 属性）
 *            若任意一个 VOP 没有 IOMMU，则全局设置 is_support_iommu = false，
 *            所有帧缓冲都退回到物理连续内存分配（CMA），放弃 IOMMU 地址映射。
 *
 * VVOP（虚拟 VOP，用于模拟器/测试）例外：跳过 OF 检查，直接继续。
 *
 * ### 第二步：构建子组件 match 列表（rockchip_drm_match_add）
 *
 * 扫描 platform_bus 上所有由 rockchip_sub_drivers[] 管理的设备实例，
 * 将它们加入 match 列表，并建立 device_link 依赖关系。
 * match 列表是 component 框架判断"所有子组件是否就绪"的依据。
 *
 * 若 match 为 ERR_PTR（所有子驱动均未 probe 或 DTS 中无相关节点），
 * 直接返回错误，rockchip_drm_bind() 不会被调用。
 *
 * ### 第三步：注册为 component master（component_master_add_with_match）
 *
 * 将本设备注册为 component master，并传入 match 列表和操作集：
 *   rockchip_drm_ops.bind   = rockchip_drm_bind   → 所有子组件就绪时调用
 *   rockchip_drm_ops.unbind = rockchip_drm_unbind → 卸载时调用
 *
 * component_master_add_with_match 内部：
 *   → 立即检查 match 列表中是否已有足够多的子组件注册（调用 component_add 的）
 *   → 若全部就绪：当场调用 rockchip_drm_bind()，完成 DRM 设备创建
 *   → 若部分就绪：挂起等待，每当一个新子组件注册（component_add）时
 *                 重新检查，直到 match 列表全部满足后触发 bind
 *
 * ## DMA 掩码配置
 *
 * bind 成功后设置 coherent_dma_mask = DMA_BIT_MASK(64)，
 * 允许 DMA 使用完整的 64 位物理地址空间，支持超过 4GB 的大内存平台。
 * （RK3568 的 DDR 地址空间上限为 8GB）
 *
 * ## 完整时序图
 *
 *  rockchip_drm_init()
 *    ├─ platform_register_drivers(sub_drivers)  → 注册 VOP2/DSI/HDMI 等子驱动
 *    │    └─ 各子驱动 probe → component_add()   → 子组件就绪
 *    └─ platform_driver_register(master_driver)
 *         └─ rockchip_drm_platform_probe()      ← 本函数
 *              ├─ OF 预检
 *              ├─ rockchip_drm_match_add()       → 构建 match 列表
 *              └─ component_master_add_with_match()
 *                   └─ 所有子组件就绪 → rockchip_drm_bind()
 *                        ├─ drm_dev_alloc()      → 创建 drm_device
 *                        ├─ 各子组件 bind()      → 注册 CRTC/Encoder/Connector
 *                        └─ drm_dev_register()   → 对用户空间开放 /dev/dri/card0
 */
static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	int ret;

	/*
	 * 第一步：OF 拓扑预检。
	 * 确认 DTS 中至少有一个可用的 VOP 端口，并确定是否使用 IOMMU 内存模式。
	 * VVOP 模式（虚拟 VOP，用于模拟测试）跳过此检查。
	 */
	ret = rockchip_drm_platform_of_probe(dev);
#if !IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
	if (ret)
		return ret;
#endif

	/*
	 * 第二步：扫描总线，构建子组件 match 列表。
	 * 找不到任何子组件时返回 -ENODEV（子驱动尚未 probe，可能触发 defer）。
	 */
	match = rockchip_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	/*
	 * 第三步：注册为 component master，绑定 match 列表。
	 * 若此时所有子组件已就绪，rockchip_drm_bind() 在本调用内同步执行；
	 * 否则异步等待，直到最后一个子组件 component_add() 时触发绑定。
	 * 失败时清理 device_link，防止悬空依赖。
	 */
	ret = component_master_add_with_match(dev, &rockchip_drm_ops, match);
	if (ret < 0) {
		rockchip_drm_match_remove(dev);
		return ret;
	}

	/* 配置 64 位 DMA 掩码，支持超过 4GB 的大内存平台（RK3568 最大 8GB DDR） */
	dev->coherent_dma_mask = DMA_BIT_MASK(64);

	return 0;
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	rockchip_drm_match_remove(&pdev->dev);

	return 0;
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (drm) {
		drm_kms_helper_poll_fini(drm);
		drm_atomic_helper_shutdown(drm);
	}
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
	.shutdown = rockchip_drm_platform_shutdown,
	.driver = {
		.name = "rockchip-drm",
		.of_match_table = rockchip_drm_dt_ids,
		.pm = &rockchip_drm_pm_ops,
	},
};

#define ADD_ROCKCHIP_SUB_DRIVER(drv, cond) { \
	if (IS_ENABLED(cond) && \
	    !WARN_ON(num_rockchip_sub_drivers >= MAX_ROCKCHIP_SUB_DRIVERS)) \
		rockchip_sub_drivers[num_rockchip_sub_drivers++] = &drv; \
}

static int __init rockchip_drm_init(void)
{
	int ret;

	num_rockchip_sub_drivers = 0;
#if IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
	ADD_ROCKCHIP_SUB_DRIVER(vvop_platform_driver, CONFIG_DRM_ROCKCHIP_VVOP);
#else
	ADD_ROCKCHIP_SUB_DRIVER(vop_platform_driver, CONFIG_ROCKCHIP_VOP);
	ADD_ROCKCHIP_SUB_DRIVER(vop2_platform_driver, CONFIG_ROCKCHIP_VOP2);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver,
				CONFIG_ROCKCHIP_LVDS);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_dp_driver,
				CONFIG_ROCKCHIP_ANALOGIX_DP);
	ADD_ROCKCHIP_SUB_DRIVER(cdn_dp_driver, CONFIG_ROCKCHIP_CDN_DP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(inno_hdmi_driver, CONFIG_ROCKCHIP_INNO_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_tve_driver,
				CONFIG_ROCKCHIP_DRM_TVE);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_rgb_driver, CONFIG_ROCKCHIP_RGB);
#endif
	ret = platform_register_drivers(rockchip_sub_drivers,
					num_rockchip_sub_drivers);
	if (ret)
		return ret;

	ret = platform_driver_register(&rockchip_drm_platform_driver);
	if (ret)
		goto err_unreg_drivers;

	return 0;

err_unreg_drivers:
	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
	return ret;
}

static void __exit rockchip_drm_fini(void)
{
	platform_driver_unregister(&rockchip_drm_platform_driver);

	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
}

module_init(rockchip_drm_init);
module_exit(rockchip_drm_fini);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");
