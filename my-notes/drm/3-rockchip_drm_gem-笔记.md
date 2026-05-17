# Rockchip DRM GEM 子系统从0到1

## 目录
1. [概述：GEM是什么](#1-概述gem是什么)
2. [核心数据结构](#2-核心数据结构)
3. [系统架构全景图](#3-系统架构全景图)
4. [创建路径：四种入口](#4-创建路径四种入口)
5. [三种内存分配策略](#5-三种内存分配策略)
6. [IOMMU管理](#6-iommu管理)
7. [MMAP路径](#7-mmap路径)
8. [PRIME/DMA-BUF共享](#8-primedma-buf共享)
9. [销毁路径](#9-销毁路径)
10. [与Framebuffer的关系](#10-与framebuffer的关系)
11. [与VOP2硬件的关系](#11-与vop2硬件的关系)
12. [IOCTL接口汇总](#12-ioctl接口汇总)
13. [完整函数索引](#13-完整函数索引)

---

## 1. 概述：GEM是什么

GEM (Graphics Execution Manager) 是Linux DRM子系统的内存管理器。它提供：
- **GPU可访问的内存分配和释放**
- **多进程间的buffer共享** (PRIME/dmabuf)
- **用户空间mmap映射**
- **DMA操作的缓存同步**

在Rockchip DRM驱动中，GEM的内存来源有三种：
- **CMA**：DMA连续物理内存（`dma_alloc_attrs`）
- **SHMEM**：非连续页 + IOMMU（`drm_gem_get_pages` + `iommu_map_sg`）
- **SECURE**：安全内存池（`gen_pool_alloc`，TEE保护）

### 关键文件清单

| 文件 | 用途 |
|------|------|
| [rockchip_drm_gem.h](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.h) | GEM结构体、枚举、函数声明 |
| [rockchip_drm_gem.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c) (1155行) | GEM全部实现，38个函数 |
| [rockchip_drm_drv.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c) | drm_driver注册、IOMMU初始化 |
| [rockchip_drm_drv.h](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.h) | private结构体、常量 |
| [rockchip_drm_fb.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c) | Framebuffer创建，桥接GEM |
| [rockchip_drm_fbdev.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fbdev.c) | fbdev模拟，使用dumb buffer |
| [rockchip_drm_vop2.c](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c) | VOP2使用GEM DMA地址编程寄存器 |
| [rockchip_drm.h](../../kernel/include/uapi/drm/rockchip_drm.h) | 用户空间ABI结构体和IOCTL定义 |

---

## 2. 核心数据结构

### 2.1 `struct rockchip_gem_object` — GEM核心

**定义：[rockchip_drm_gem.h:26-45](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.h#L26)**

```c
struct rockchip_gem_object {
    struct drm_gem_object base;        // [27] DRM核心GEM对象 (必须是第一个成员)
    unsigned int flags;                // [28] 用户传入的标志位 (ROCKCHIP_BO_*)
    enum rockchip_gem_buf_type buf_type; // [29] CMA / SHMEM / SECURE 类型判别

    void *kvaddr;                      // [31] 内核虚拟地址 (CPU访问)
    void *cookie;                      // [32] 不透明cookie (保留)
    dma_addr_t dma_addr;               // [33] 设备可见地址 (有IOMMU时是IOVA，否则是物理地址)
    dma_addr_t dma_handle;             // [34] 物理地址 (始终保存物理地址)

    unsigned long dma_attrs;           // [37] DMA属性 (WRITE_COMBINE, NO_KERNEL_MAPPING)
    struct drm_mm_node mm;             // [40] IOMMU虚拟地址空间分配节点
    unsigned long num_pages;           // [41] 页数
    struct page **pages;               // [42] page指针数组
    struct sg_table *sgt;              // [43] scatter-gather表
    size_t size;                       // [44] IOMMU映射后的实际大小
};
```

**继承关系**：
```
drm_gem_object (DRM核心)
    ^
    | 嵌入为第一个成员 (struct embedding)
    |
rockchip_gem_object (Rockchip扩展)
```

- **向上转型**：隐式——任何需要`drm_gem_object *`的地方直接传`rk_obj`即可
- **向下转型**：使用宏 [`to_rockchip_obj(x) = container_of(x, struct rockchip_gem_object, base)`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.h#L18)

**关键字段语义**：
- `dma_addr`：**这是VOP2硬件寄存器编程用的地址**。有IOMMU时是IOVA（由`drm_mm`分配），无IOMMU时等于物理地址
- `dma_handle`：**始终是物理地址**，来自`dma_alloc_attrs`或`gen_pool_alloc`
- `kvaddr`：CPU可用的内核虚拟地址。CMA由DMA API提供，SHMEM由`vmap()`创建，SECURE永远为NULL
- `mm`：在`private->mm`（一个`drm_mm`区间分配器）中分配的IOVA范围节点

### 2.2 `enum rockchip_gem_buf_type` — 内存类型判别

**定义：[rockchip_drm_gem.h:20-24](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.h#L20)**

| 值 | 含义 | 分配函数 |
|----|------|---------|
| `ROCKCHIP_GEM_BUF_TYPE_CMA` | 物理连续DMA内存 | `rockchip_gem_alloc_dma()` |
| `ROCKCHIP_GEM_BUF_TYPE_SHMEM` | shmem页 + IOMMU映射 | `rockchip_gem_get_pages()` |
| `ROCKCHIP_GEM_BUF_TYPE_SECURE` | TEE安全内存池 | `rockchip_gem_alloc_secure()` |

### 2.3 用户空间标志位 (`drm_rockchip_gem_mem_type`)

**定义：[rockchip_drm.h:28-40](../../kernel/include/uapi/drm/rockchip_drm.h#L28)**

```c
ROCKCHIP_BO_CONTIG     = 1 << 0  // 强制物理连续 (CMA)
ROCKCHIP_BO_CACHABLE   = 1 << 1  // 可缓存映射
ROCKCHIP_BO_WC         = 1 << 2  // write-combine映射
ROCKCHIP_BO_SECURE     = 1 << 3  // 安全buffer (TEE保护)
ROCKCHIP_BO_ALLOC_KMAP = 1 << 4  // 保留内核虚拟地址映射
```

### 2.4 UAPI IOCTL结构体

**定义：[rockchip_drm.h:50-73](../../kernel/include/uapi/drm/rockchip_drm.h#L50)**

| 结构体 | 用途 | IOCTL |
|--------|------|-------|
| `drm_rockchip_gem_create { size, flags, handle }` | 创建GEM buffer | `ROCKCHIP_GEM_CREATE` |
| `drm_rockchip_gem_map_off { handle, pad, offset }` | 获取mmap偏移 | `ROCKCHIP_GEM_MAP_OFFSET` |
| `drm_rockchip_gem_phys { handle, phy_addr }` | 查询物理地址 | `ROCKCHIP_GEM_GET_PHYS` |

### 2.5 `struct page_info` — 页重排临时节点

**定义：[rockchip_drm_gem.c:30-33](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L30)**

仅在SHMEM页重排算法(`rockchip_gem_get_pages()`)中临时使用。将页面按物理地址bits [12:14]分到8个桶中，然后round-robin交错排列，优化DRAM bank交织和IOMMU TLB效率。

---

## 3. 系统架构全景图

```
用户空间
  │
  │ GEM_CREATE / ADDFB2 / ATOMIC / mmap
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│  DRM Core (drm_driver)                                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │ rockchip_drm_drv.c:3133-3166                          │  │
│  │ .dumb_create          = rockchip_gem_dumb_create      │  │
│  │ .dumb_map_offset      = rockchip_gem_dumb_map_offset  │  │
│  │ .gem_free_object      = rockchip_gem_free_object       │  │
│  │ .gem_prime_*          = rockchip_gem_prime_*           │  │
│  │ .ioctls               = rockchip_ioctls[]              │  │
│  │ .gem_vm_ops           = &drm_gem_cma_vm_ops            │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│  rockchip_drm_gem.c (GEM核心)                                │
│                                                             │
│  ┌───────────────────┐  ┌─────────────┐  ┌──────────────┐  │
│  │ 分配层 (alloc_buf) │  │ IOMMU层     │  │ 映射层 (mmap)│  │
│  │                    │  │             │  │              │  │
│  │ SECURE→gen_pool   │  │ drm_mm分配  │  │ SECURE→拒绝  │  │
│  │ CMA→dma_alloc     │  │ iommu_map   │  │ SHMEM→插页   │  │
│  │ SHMEM→get_pages   │  │ 建立页表    │  │ CMA→DMA mmap │  │
│  └───────────────────┘  └─────────────┘  └──────────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ PRIME导出/导入层                                      │    │
│  │ get_sg_table → import_sg_table → vmap/vunmap        │    │
│  │ begin/end_cpu_access (全量 & 部分范围)               │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
  │
  │ dma_addr (IOVA或物理地址)
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│  Framebuffer层 (rockchip_drm_fb.c)                           │
│  rockchip_fb_alloc() 将 GEM的dma_addr 复制到 fb->dma_addr[] │
└─────────────────────────────────────────────────────────────┘
  │
  │ rockchip_fb_get_dma_addr(fb, plane)
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│  VOP2 硬件层 (rockchip_drm_vop2.c)                           │
│  VOP_WIN_SET(vop2, win, yrgb_mst, vpstate->yrgb_mst)       │
│  硬件DMA引擎从 dma_addr 开始读取像素数据                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. 创建路径：四种入口

### 路径A：自定义IOCTL `DRM_IOCTL_ROCKCHIP_GEM_CREATE`

```
用户空间: ioctl(fd, DRM_IOCTL_ROCKCHIP_GEM_CREATE, &create)
    │  create.size, create.flags → 内核填充 create.handle
    ▼
[rockchip_gem_create_ioctl] (gem.c:1005)
    │
    ▼
[rockchip_gem_create_with_handle] (gem.c:761)
    │
    ├── [rockchip_gem_create_object] (gem.c:683)
    │       │
    │       ├── [rockchip_gem_alloc_object] (gem.c:655)
    │       │       ├── kzalloc(rockchip_gem_object)
    │       │       ├── drm_gem_object_init(drm, obj, size)  → 创建shmem backing
    │       │       └── mapping_set_gfp_mask()               → 设置页分配标志
    │       │
    │       └── [rockchip_gem_alloc_buf] (gem.c:428)  ← 核心分配派发
    │               │  详见第5节：三种分配策略
    │               │
    │               ├── if (!private->domain): 强制 flags |= BO_CONTIG
    │               ├── SECURE → rockchip_gem_alloc_secure (gem.c:360)
    │               ├── CONTIG → rockchip_gem_alloc_dma     (gem.c:271)
    │               └── SHMEM  → rockchip_gem_get_pages     (gem.c:131)
    │               │
    │               └── if (domain): rockchip_gem_iommu_map  (gem.c:37)
    │                   else: dma_addr = dma_handle
    │
    ├── drm_gem_handle_create(file_priv, obj, &handle)  → 注册到进程handle表
    └── drm_gem_object_put_unlocked(obj)                → 释放临时引用
```

### 路径B：Dumb Buffer `DRM_IOCTL_MODE_CREATE_DUMB`

```
[rockchip_gem_dumb_create] (gem.c:803)
    │  pitch = ALIGN(width * bpp / 8, 64)  ← 64字节对齐 (Mali GPU要求)
    │  size  = pitch * height
    ▼
[rockchip_gem_create_with_handle] (gem.c:761)
    └── (后续同路径A)
```

### 路径C：PRIME导入 `DRM_IOCTL_PRIME_FD_TO_HANDLE`

```
[rockchip_drm_gem_prime_import]         (drv.c:3051)
    │
    ▼
[rockchip_drm_gem_prime_import_dev]     (drv.c:2832)
    │  三个快速路径:
    │  1. 同驱动再导入 (比较dmabuf ops指针) → 直接返回已有obj
    │  2. CONFIG_DMABUF_CACHE 命中 → 返回缓存obj
    │  3. 完整导入 ↓
    │
    ├── get_dma_buf(fd)
    ├── dma_buf_attach(dma_buf, dev)
    ├── dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL) → 获取sg_table
    │
    ▼
[rockchip_gem_prime_import_sg_table]    (gem.c:904)
    │
    ├── rockchip_gem_alloc_object()     → 只分配壳，不分配内存
    ├── if IOMMU: rockchip_gem_iommu_map_sg (gem.c:871)
    │   else:     rockchip_gem_dma_map_sg    (gem.c:881)
    │             └── 验证DMA连续长度足够 (硬件需要)
    ├── drm_prime_sg_to_page_addr_arrays() → 构建pages[]
    └── drm_gem_handle_create()
```

### 路径D：内核内部分配 (fbdev, logo)

直接调用 `rockchip_gem_create_object()` 不创建用户空间handle。

---

## 5. 三种内存分配策略

**派发函数**：[`rockchip_gem_alloc_buf`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L428)

### 决策表

| 条件 | buf_type | 分配函数 | 物理布局 | IOMMU映射 | dma_addr值 |
|------|----------|---------|---------|----------|-----------|
| 无IOMMU | CMA | `rockchip_gem_alloc_dma()` | 物理连续 | 无 | dma_addr = dma_handle (物理地址) |
| IOMMU + BO_CONTIG | CMA | `rockchip_gem_alloc_dma()` | 物理连续 | `iommu_map_sg()` | IOVA (由drm_mm分配) |
| IOMMU + 无CONTIG | SHMEM | `rockchip_gem_get_pages()` | 非连续页 | `iommu_map_sg()` | IOVA (由drm_mm分配) |
| BO_SECURE | SECURE | `rockchip_gem_alloc_secure()` | 物理连续 | `iommu_map_sg()` | IOVA (由drm_mm分配) |

### 5.1 CMA分配 (`rockchip_gem_alloc_dma`)

[gem.c:271-341](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L271)

```c
// 1. 设置DMA属性
dma_attrs = DMA_ATTR_WRITE_COMBINE;
if (!alloc_kmap) dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;  // 节省vmalloc空间

// 2. 分配物理连续内存
kvaddr = dma_alloc_attrs(drm->dev, size, &dma_handle, GFP_KERNEL, dma_attrs);

// 3. 构建SG表
sg_alloc_table(sgt, 1, GFP_KERNEL);
dma_get_sgtable_attrs(drm->dev, sgt, kvaddr, dma_handle, size, dma_attrs);

// 4. 伪造DMA地址为物理地址 (用于dma_sync_*)
sg_dma_address(s) = sg_phys(s);  // gem.c:308

// 5. SG转pages数组
drm_prime_sg_to_page_addr_arrays(sgt, pages, NULL, npages);
```

**释放**：[`rockchip_gem_free_dma`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L496)
```c
drm_free_large(pages) → sg_free_table(sgt) → kfree(sgt) → dma_free_attrs()
```

### 5.2 SHMEM分配 (`rockchip_gem_get_pages`)

[gem.c:131-259](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L131)

这是文件中最复杂的函数，实现了**页重排算法**：

```c
// 1. 从shmem获取页
drm_gem_get_pages(&rk_obj->base);  // gem.c:153

// 2. 扫描连续块:
//    - 块 >7页 → 原样保留
//    - 块 ≤7页 → 按物理地址bits[12:14]分到8个桶
//                bit12_14 = (phys >> 12) & 0x7

// 3. Round-robin交错: 从8个桶各取1页，循环
//    目的: 将不同DRAM bank的页交错排列，优化带宽

// 4. 构建SG表
rockchip_gem_pages_to_sg(dst_pages, npages);  // gem.c:200

// 5. 伪造DMA地址 + 同步
sg_dma_address(s) = sg_phys(s);               // gem.c:243-244
dma_sync_sg_for_device(dev, sgt->sgl, sgt->nents, DMA_TO_DEVICE);

// 6. 替换pages
kvfree(pages);
rk_obj->pages = dst_pages;
```

**为什么伪造 `sg_dma_address = sg_phys`？**（注释在gem.c:236-241）
因为`dma_sync_sg_for_device/cpu`函数使用`sg_dma_address`来同步缓存。在IOMMU映射之前，需要这个假地址来确保`dma_sync_*`正确工作。真正的IOVA转换在IOMMU页表中完成。

### 5.3 SECURE分配 (`rockchip_gem_alloc_secure`)

[gem.c:360-413](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L360)

```c
// 1. 从gen_pool分配
paddr = gen_pool_alloc(private->secure_buffer_pool, size);

// 2. 逐页构建pages数组
for (i = 0; i < npages; i++)
    pages[i] = phys_to_page(paddr + i * PAGE_SIZE);

// 3. 构建SG表
rockchip_gem_pages_to_sg(pages, npages);
```

**特殊限制**：
- `kvaddr` 永远为 NULL（`rockchip_gem_alloc_buf:443` 拒绝 `alloc_kmap`）
- mmap 被明确拒绝（`rockchip_drm_gem_object_mmap:601` 返回 `-EINVAL`）
- 用于 TEE (Trusted Execution Environment) 内容保护

---

## 6. IOMMU管理

### 6.1 IOMMU域初始化

[`rockchip_drm_init_iommu`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L1542)

```c
private->domain = iommu_domain_alloc(&platform_bus_type);

// IOMMU虚拟地址空间 = geometry.aperture_start ~ aperture_end
drm_mm_init(&private->mm, start, end - start + 1);

// 注册fault handler
iommu_set_fault_handler(private->domain, rockchip_drm_fault_handler, drm_dev);
```

### 6.2 IOMMU启用/禁用检测

[drv.c:3434-3521](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L3434)

`rockchip_drm_platform_of_probe()` 遍历DT中所有VOP的 `iommus` 属性。**任何一个VOP没有IOMMU，全局禁用**（"短板原理"）——因为framebuffer可以在CRTC间共享。

```c
#if IS_ENABLED(CONFIG_DRM_ROCKCHIP_VVOP)
static bool is_support_iommu = false;  // VVOP始终不启用
#else
static bool is_support_iommu = true;   // 默认启用，由DT决定
#endif
```

### 6.3 IOMMU映射 (`rockchip_gem_iommu_map`)

[gem.c:37-76](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L37)

```c
// 1. 在IOMMU虚拟地址空间分配连续区间
drm_mm_insert_node_generic(&private->mm, &rk_obj->mm,
                           size, PAGE_SIZE, 0, 0);

// 2. IOVA ← 分配的虚拟地址
rk_obj->dma_addr = rk_obj->mm.start;  // gem.c:55

// 3. 建立IOMMU页表映射
//    IOMMU_TLB_SHOT_ENTIRE 是Rockchip特殊保护标志
iommu_map_sg(private->domain, rk_obj->dma_addr, rk_obj->sgt->sgl,
             rk_obj->sgt->nents, IOMMU_READ | IOMMU_WRITE | IOMMU_TLB_SHOT_ENTIRE);

rk_obj->size = ret;  // IOMMU映射后实际大小
```

### 6.4 IOMMU取消映射 (`rockchip_gem_iommu_unmap`)

[gem.c:78-92](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L78)

```c
iommu_unmap(private->domain, rk_obj->dma_addr, rk_obj->size);
drm_mm_remove_node(&rk_obj->mm);  // 归还IOVA
```

注意：**IOMMU unmap总是在释放物理内存之前进行**，防止遗留的TLB项指向已释放内存。

### 6.5 VOP2 IOMMU运行时附加/分离

[rockchip_drm_vop2.c:5788-5801](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5788)

VOP2在 `atomic_flush` 中需要时附加IOMMU，在 `vop2_disable` 中分离。这是延迟附加策略，节省IOMMU资源。

---

## 7. MMAP路径

### mmap两种入口

| 入口 | 函数 | 位置 | 场景 |
|------|------|------|------|
| DRM fd mmap | `rockchip_gem_mmap` | [gem.c:629](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L629) | `mmap(/dev/dri/card0)` |
| dmabuf fd mmap | `rockchip_gem_mmap_buf` | [gem.c:616](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L616) | `mmap(dmabuf_fd)` |

### mmap内部分派 (`rockchip_drm_gem_object_mmap`)

[gem.c:585-614](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L585)

```
1. if (BO_CACHABLE): 设置vma为可缓存 (默认WC)
2. 清除 VM_PFNMAP (因为使用struct page而非raw PFN)
3. 按buf_type分派:
   ├── SECURE → -EINVAL (禁止mmap)
   ├── SHMEM → [rockchip_drm_gem_object_mmap_iommu] (gem.c:562)
   │           └── [__vm_map_pages] (gem.c:537)
   │               └── 逐页调用 vm_insert_page() 插入shmem页
   └── CMA   → [rockchip_drm_gem_object_mmap_dma] (gem.c:575)
               └── dma_mmap_attrs() — 标准DMA mmap
```

### mmap偏移获取流程

```
1. 用户空间: ROCKCHIP_GEM_MAP_OFFSET ioctl → 获取fake offset
2. 用户空间: mmap(drm_fd, ..., offset, ...)
3. 内核: rockchip_gem_mmap → drm_gem_mmap (用fake offset查找GEM对象)
       → vma->vm_pgoff = 0 (重置，已用fake offset完成了查找)
       → 实际的物理页映射
```

---

## 8. PRIME/DMA-BUF共享

### 8.1 导出 (`gem_prime_get_sg_table`)

[gem.c:828-852](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L828)

```c
if (rk_obj->pages)  // SHMEM/SECURE
    return rockchip_gem_pages_to_sg(rk_obj->pages, rk_obj->num_pages);
else                // CMA: 通过DMA API获取SG
    dma_get_sgtable_attrs(dev, sgt, rk_obj->kvaddr, rk_obj->dma_handle,
                          rk_obj->base.size, rk_obj->dma_attrs);
```

### 8.2 导入 (`gem_prime_import_sg_table`)

[gem.c:904-947](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L904)

有IOMMU时直接映射外来SG到IOVA空间。无IOMMU时需验证DMA连续性。

### 8.3 vmap/vunmap

| 函数 | 位置 | 行为 |
|------|------|------|
| `rockchip_gem_prime_vmap` | [gem.c:949](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L949) | SHMEM/SECURE→`vmap(pages)`; CMA→返回`kvaddr` |
| `rockchip_gem_prime_vunmap` | [gem.c:967](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L967) | SHMEM/SECURE→`vunmap`; CMA→空操作 |

### 8.4 CPU Cache同步

| 函数 | 位置 | 行为 |
|------|------|------|
| `begin_cpu_access` | [gem.c:1053](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1053) | `dma_sync_sg_for_cpu()` |
| `end_cpu_access` | [gem.c:1067](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1067) | `dma_sync_sg_for_device()` |
| `begin_cpu_access_partial` | [gem.c:1120](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1120) | 遍历SG，只同步重叠部分 |
| `end_cpu_access_partial` | [gem.c:1138](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1138) | 同上，反方向 |

部分同步通过 [`rockchip_gem_prime_sgl_sync_range`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1081) 实现，逐SG条目计算重叠区域，只调用 `dma_sync_single_range_for_*` 在受影响的条目上。

---

## 9. 销毁路径

### `rockchip_gem_free_object` — 最终释放

[gem.c:724-752](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L724)

这是 DRM 核心回调 (`gem_free_object_unlocked`)，当 GEM 引用计数归零时被调用。

```
rockchip_gem_free_object(obj)
    │
    ├── if (import_attach) — 导入的buffer:
    │   ├── IOMMU: rockchip_gem_iommu_unmap()
    │   ├── 无IOMMU: dma_unmap_sg()
    │   ├── drm_free_large(pages)
    │   └── rockchip_gem_destroy / drm_prime_gem_destroy
    │       └── dma_buf_unmap_attachment → dma_buf_detach → dma_buf_put
    │
    ├── else — 本地分配的buffer:
    │   └── [rockchip_gem_free_buf] (gem.c:508)
    │       ├── IOMMU: rockchip_gem_iommu_unmap()
    │       ├── SHMEM: vunmap → rockchip_gem_put_pages
    │       │         sg_free_table → kfree(sgt) → drm_gem_put_pages
    │       ├── SECURE: drm_free_large(pages) → sg_free_table → kfree(sgt)
    │       │           → gen_pool_free
    │       └── CMA: drm_free_large(pages) → sg_free_table → kfree(sgt)
    │                → dma_free_attrs
    │
    └── [rockchip_gem_release_object] (gem.c:649)
        ├── drm_gem_object_release(&rk_obj->base)
        └── kfree(rk_obj)
```

**释放顺序保证**：IOMMU unmap 最先执行 → 确保没有硬件仍能访问 → 然后释放物理内存 → 最后释放GEM对象壳。

---

## 10. 与Framebuffer的关系

### `struct rockchip_drm_fb`

[rockchip_drm_fb.h:36-42](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.h#L36)

```c
struct rockchip_drm_fb {
    struct drm_framebuffer fb;
    dma_addr_t dma_addr[ROCKCHIP_MAX_FB_BUFFER];  // [38] 每plane一个DMA地址 (max 3)
    void *kvaddr[ROCKCHIP_MAX_FB_BUFFER];          // [39] 每plane一个CPU虚拟地址
    struct drm_gem_object *obj[ROCKCHIP_MAX_FB_BUFFER]; // [40] 回指GEM对象
    struct rockchip_logo *logo;                    // logo特殊处理
};
```

`to_rockchip_fb(x) = container_of(x, struct rockchip_drm_fb, fb)` — [fb.h:34](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.h#L34)

### FB创建关键流程

[`rockchip_user_fb_create`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L273)

```c
// 1. 解析pixel format
num_planes = drm_format_num_planes(mode_cmd->pixel_format);

// 2. 逐plane: GEM handle → GEM object
for (i = 0; i < num_planes; i++) {
    obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
    // 验证buffer大小:
    //   min_size = (height-1) * pitch + offset + width * cpp
    //   使用 (height-1)*pitch 而非 height*pitch,
    //   因为最后一行不需要padding字节
    if (obj->size < min_size) return -EINVAL;
}

// 3. 分配framebuffer
[rockchip_fb_alloc] (fb.c:108)
    // 关键步骤 — 从GEM复制DMA地址:
    for (i = 0; i < num_planes; i++) {
        rk_obj = to_rockchip_obj(obj[i]);
        rockchip_fb->dma_addr[i] = rk_obj->dma_addr;  // fb.c:139
        rockchip_fb->kvaddr[i]  = rk_obj->kvaddr;      // fb.c:140
    }
    drm_framebuffer_init(dev, &rockchip_fb->fb, &rockchip_drm_fb_funcs);
```

### DMA地址访问器

| 函数 | 位置 |
|------|------|
| `rockchip_fb_get_dma_addr(fb, plane)` → `rk_fb->dma_addr[plane]` | [fb.c:37-46](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L37) |
| `rockchip_fb_get_kvaddr(fb, plane)` → `rk_fb->kvaddr[plane]` | [fb.c:48-56](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L48) |

### fbdev模拟

[`rockchip_drm_fbdev.c`](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fbdev.c)

```
rockchip_drm_fbdev_init() → drm_fb_helper_initial_config()
    → rockchip_drm_fbdev_create()
        1. rk_obj = rockchip_gem_create_object(dev, size, true, 0)
        2. helper->fb = rockchip_drm_framebuffer_init(dev, &mode_cmd, &rk_obj->base)
        3. fbi->screen_base = rk_obj->kvaddr + offset
        4. fbi->screen_size = rk_obj->base.size
```

---

## 11. 与VOP2硬件的关系

### 完整数据流：从GEM Handle到VOP2硬件寄存器

```
Phase 1: GEM_CREATE
  → rockchip_gem_create_object()
  → rk_obj->dma_addr = IOVA (IOMMU) 或 物理地址 (无IOMMU)

Phase 2: ADDFB2
  → rockchip_user_fb_create()
  → drm_gem_object_lookup(handle) 解析GEM handle
  → rockchip_fb_alloc()
  → rk_fb->dma_addr[i] = rk_obj->dma_addr

Phase 3: ATOMIC ioctl
  → drm_atomic_set_fb_for_plane()
  → plane_state->fb = rk_fb->fb

Phase 4: atomic_check
  [vop2_plane_atomic_check] (vop2.c:3051)
  → dma_addr = rockchip_fb_get_dma_addr(fb, 0)
  → offset = (src_x * bpp/8) + (src_y * pitch)  (含mirror/rotation处理)
  → vpstate->yrgb_mst = dma_addr + offset + fb->offsets[0]  // vop2.c:3176-3184
  → vpstate->uv_mst   = dma_addr + ... (仅YUV格式)           // vop2.c:3185-3196

Phase 5: atomic_commit
  [drm_atomic_helper_commit_planes] (drm_atomic_helper.c:2361)
  → atomic_begin: vop2_crtc_atomic_begin()
  → atomic_update: [vop2_plane_atomic_update] (vop2.c:3281)
      VOP_WIN_SET(vop2, win, yrgb_mst, vpstate->yrgb_mst)  // ← DMA地址写入硬件寄存器!
      VOP_WIN_SET(vop2, win, uv_mst,   vpstate->uv_mst)     // vop2.c:3482, 3491
      VOP_WIN_SET(vop2, win, format,   vpstate->format)     // vop2.c:3479
      VOP_WIN_SET(vop2, win, yrgb_vir, stride)              // vop2.c:3481
  → atomic_flush: [vop2_crtc_atomic_flush] (vop2.c:5775)
      → IOMMU attach if needed
      → vop2_cfg_done() — 锁存寄存器

Phase 6: Next VBlank
  → VOP2 DMA引擎从 yrgb_mst 和 uv_mst 地址开始读取像素
  → IOMMU将IOVA转换为物理地址 (如果启用)
  → 像素数据流向显示输出
```

### VOP2 `vop2_plane_state` 关键字段

[vop2.c:241-265](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L241)

```c
struct vop2_plane_state {
    dma_addr_t yrgb_mst;    // Y/RGB plane DMA起始地址  (VOP2 WIN_MST寄存器)
    dma_addr_t uv_mst;      // UV/Chroma plane DMA起始地址
    int format;             // VOP2内部格式码 (由vop2_convert_format转换)
    struct drm_rect src;    // 源矩形 (.16.16 fixed point)
    struct drm_rect dest;   // 目标矩形
};
```

### 其他GEM使用场景

1. **AFBC** (ARM Frame Buffer Compression)：`VOP_AFBC_SET(vop2, win, hdr_ptr, vpstate->yrgb_mst)` — vop2.c:3456
2. **Writeback**：GEM作为DMA目标——`VOP_MODULE_SET(vop2, wb, yrgb_mst, wb_state->yrgb_addr)` — vop2.c:2587
3. **CUBIC LUT**：色彩校正表也存储在GEM对象中——`cubic_lut_mst = vp->cubic_lut_gem_obj->dma_addr` — vop2.c:2762

---

## 12. IOCTL接口汇总

### Rockchip专用IOCTL

| IOCTL (NR) | Handler | 位置 | 用途 |
|-----------|---------|------|------|
| `ROCKCHIP_GEM_CREATE` (0x00) | `rockchip_gem_create_ioctl` | [gem.c:1005](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1005) | 创建GEM buffer |
| `ROCKCHIP_GEM_MAP_OFFSET` (0x01) | `rockchip_gem_map_offset_ioctl` | [gem.c:1016](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1016) | 获取mmap偏移 |
| `ROCKCHIP_GEM_GET_PHYS` (0x04) | `rockchip_gem_get_phys_ioctl` | [gem.c:1025](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1025) | 查询物理地址(仅CONTIG) |

注册位置: [drv.c:2578-2585](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L2578)

### 标准DRM IOCTL (也经过GEM)

| IOCTL | 对应GEM操作 |
|-------|-------------|
| `MODE_CREATE_DUMB` | `rockchip_gem_dumb_create` — [gem.c:803](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L803) |
| `MODE_MAP_DUMB` | `rockchip_gem_dumb_map_offset` — [gem.c:979](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L979) |
| `PRIME_FD_TO_HANDLE` | `rockchip_gem_prime_import_sg_table` — [gem.c:904](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L904) |
| `PRIME_HANDLE_TO_FD` | `rockchip_gem_prime_get_sg_table` — [gem.c:828](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L828) |
| `mmap()` on drm fd | `rockchip_gem_mmap` — [gem.c:629](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L629) |
| `mmap()` on dmabuf fd | `rockchip_gem_mmap_buf` — [gem.c:616](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L616) |

---

## 13. 完整函数索引

### IOMMU相关 (7个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_iommu_map` | [gem.c:37](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L37) | 内部辅助 |
| `rockchip_gem_iommu_unmap` | [gem.c:78](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L78) | 内部辅助 |
| `rockchip_gem_iommu_map_sg` | [gem.c:871](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L871) | 内部辅助 (导入路径) |
| `rockchip_gem_dma_map_sg` | [gem.c:881](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L881) | 内部辅助 (导入·无IOMMU) |
| `rockchip_sg_get_contiguous_size` | [gem.c:854](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L854) | 内部辅助 |

### 页管理 (5个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_free_list` | [gem.c:94](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L94) | 错误清理 |
| `rockchip_gem_pages_to_sg` | [gem.c:107](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L107) | 内部辅助 |
| `rockchip_gem_get_pages` | [gem.c:131](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L131) | 内部辅助 (页重排) |
| `rockchip_gem_put_pages` | [gem.c:261](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L261) | 内部辅助 |
| `drm_calloc_large` / `drm_free_large` | [gem.c:343, 355](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L343) | 私有工具 |

### 内存分配/释放 (8个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_alloc_dma` | [gem.c:271](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L271) | 内部辅助 (CMA) |
| `rockchip_gem_free_dma` | [gem.c:496](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L496) | 内部辅助 (CMA) |
| `rockchip_gem_alloc_secure` | [gem.c:360](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L360) | 内部辅助 (SECURE) |
| `rockchip_gem_free_secure` | [gem.c:415](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L415) | 内部辅助 (SECURE) |
| `rockchip_gem_alloc_buf` | [gem.c:428](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L428) | **核心派发** |
| `rockchip_gem_free_buf` | [gem.c:508](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L508) | **核心派发** |

### 对象生命周期 (6个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_alloc_object` | [gem.c:655](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L655) | 内部辅助 (分配壳) |
| `rockchip_gem_create_object` | [gem.c:683](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L683) | **公开API** |
| `rockchip_gem_release_object` | [gem.c:649](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L649) | 内部辅助 (释放壳) |
| `rockchip_gem_free_object` | [gem.c:724](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L724) | DRM核心回调 |
| `rockchip_gem_destroy` | [gem.c:706](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L706) | 内部辅助 (dmabuf_cache) |
| `rockchip_gem_create_with_handle` | [gem.c:761](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L761) | 内部辅助 |

### MMAP (5个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `__vm_map_pages` | [gem.c:537](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L537) | 内部辅助 (逐页插入) |
| `rockchip_drm_gem_object_mmap_iommu` | [gem.c:562](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L562) | 内部辅助 (SHMEM路径) |
| `rockchip_drm_gem_object_mmap_dma` | [gem.c:575](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L575) | 内部辅助 (CMA路径) |
| `rockchip_drm_gem_object_mmap` | [gem.c:585](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L585) | 内部派发 |
| `rockchip_gem_mmap_buf` | [gem.c:616](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L616) | 公开API |
| `rockchip_gem_mmap` | [gem.c:629](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L629) | DRM核心回调 |

### PRIME/DMA-BUF (6个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_prime_get_sg_table` | [gem.c:828](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L828) | DRM核心回调 |
| `rockchip_gem_prime_import_sg_table` | [gem.c:904](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L904) | DRM核心回调 |
| `rockchip_gem_prime_vmap` | [gem.c:949](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L949) | DRM核心回调 |
| `rockchip_gem_prime_vunmap` | [gem.c:967](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L967) | DRM核心回调 |
| `rockchip_gem_prime_begin_cpu_access` | [gem.c:1053](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1053) | DRM核心回调 |
| `rockchip_gem_prime_end_cpu_access` | [gem.c:1067](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1067) | DRM核心回调 |
| `rockchip_gem_prime_sgl_sync_range` | [gem.c:1081](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1081) | 内部辅助 (部分同步) |
| `rockchip_gem_prime_begin_cpu_access_partial` | [gem.c:1120](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1120) | DRM核心回调 |
| `rockchip_gem_prime_end_cpu_access_partial` | [gem.c:1138](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1138) | DRM核心回调 |

### IOCTL/Dumb (5个)

| 函数 | 位置 | 类型 |
|------|------|------|
| `rockchip_gem_dumb_create` | [gem.c:803](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L803) | DRM核心回调 |
| `rockchip_gem_dumb_map_offset` | [gem.c:979](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L979) | DRM核心回调 |
| `rockchip_gem_create_ioctl` | [gem.c:1005](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1005) | IOCTL handler |
| `rockchip_gem_map_offset_ioctl` | [gem.c:1016](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1016) | IOCTL handler |
| `rockchip_gem_get_phys_ioctl` | [gem.c:1025](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_gem.c#L1025) | IOCTL handler |

### drm_driver注册 (drv.c中的GEM相关函数)

| 函数 | 位置 |
|------|------|
| `rockchip_drm_gem_prime_import` | [drv.c:3051](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L3051) |
| `rockchip_drm_gem_prime_export` | [drv.c:3089](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L3089) |
| `rockchip_drm_init_iommu` | [drv.c:1542](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L1542) |
| `rockchip_gem_pool_init` | [drv.c:1763](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_drv.c#L1763) |

### FB相关 (fb.c)

| 函数 | 位置 |
|------|------|
| `rockchip_fb_get_dma_addr` | [fb.c:37](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L37) |
| `rockchip_fb_get_kvaddr` | [fb.c:48](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L48) |
| `rockchip_fb_alloc` | [fb.c:108](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L108) |
| `rockchip_user_fb_create` | [fb.c:273](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L273) |
| `rockchip_drm_framebuffer_init` | [fb.c:832](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_fb.c#L832) |

### VOP2 (vop2.c中使用GEM地址的函数)

| 函数 | 位置 |
|------|------|
| `vop2_plane_atomic_check` (提取DMA地址) | [vop2.c:3051](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3051) |
| `vop2_plane_atomic_update` (写寄存器) | [vop2.c:3281](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L3281) |
| `vop2_crtc_atomic_flush` (IOMMU附加+cfg_done) | [vop2.c:5775](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L5775) |
| `vop2_wb_commit` (writeback路径) | [vop2.c:2543](../../kernel/drivers/gpu/drm/rockchip/rockchip_drm_vop2.c#L2543) |

---

## 关键设计特点总结

1. **三种内存类型统一管理**：CMA / SHMEM / SECURE 三种底层分配方式被统一在 `rockchip_gem_object` 中，通过 `buf_type` 和 `flags` 在运行时分派

2. **IOMMU可选**：当 `private->domain == NULL` 时，所有buffer强制 CMA (`BO_CONTIG`)，`dma_addr == dma_handle`（物理地址直通）

3. **页重排优化**：SHMEM分配时，通过物理地址bits[12:14]分桶交错排列，改善DRAM bank交织和IOMMU TLB效率

4. **SG的DMA地址伪造**：`sg_dma_address = sg_phys` — 这使得 `dma_sync_*` 函数在IOMMU映射前后都能正确工作

5. **安全内存隔离**：SECURE buffer禁止mmap和kmap，只能通过TEE访问

6. **PRIME导入缓存**：通过dmabuf ops指针比较和 `CONFIG_DMABUF_CACHE` 实现快速导入路径

7. **Mali 64字节对齐**：dumb buffer的pitch对齐到64字节以满足Mali GPU要求

8. **延迟IOMMU附加**：VOP2在 `atomic_flush` 时才附加IOMMU，在 `vop2_disable` 时分离
