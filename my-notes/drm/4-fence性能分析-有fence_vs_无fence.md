# DRM Fence 机制性能分析：有 Fence vs 无 Fence

## 前提说明

本文对比的是两个方案的**各自最优设计**：

- **无 Fence 方案最优实现**：单设备场景用 `request_threaded_irq()` + threaded handler 直接操作寄存器；多消费者场景用 per-consumer 专用高优 workqueue。不使用 system_wq 的单 worker 扇出（那是次优实现）。
- **有 Fence 方案最优实现**：使用 `dma_fence` + `drm_sched` 的标准 DRM 框架路径。

下文用 DJI 典型视频管线为例：ISP 采集 → DSP 降噪/稳像 → 显示输出，4K@30fps。

---

## 一、无 Fence 的最优设计

### 核心机制：IRQ + wait_queue + work_struct 链

```
                        IRQ 触发 CPU 执行中断处理
                        │
                        ▼
              ┌─────────────────────┐
              │  ISR (硬中断上下文)    │
              │  更新完成状态寄存器     │
              │  wake_up(&waitq)     │  ← 仅唤醒，不做重活
              └──────────┬──────────┘
                         │
                         ▼ (返回后，内核调度)
              ┌─────────────────────┐
              │  等待者被唤醒         │
              │  wait_event_timeout  │  ← 精确唤醒，非轮询
              │  返回 true           │
              │  触发下一阶段 work    │
              └──────────┬──────────┘
                         │
                         ▼
              ┌─────────────────────┐
              │  work_struct 执行    │
              │  写入下一级硬件寄存器  │
              │  启动下一级硬件       │
              └─────────────────────┘
```

**关键点**：CPU 不需要循环读寄存器。硬件完成 → IRQ → wake_up → 精确唤醒等待者。Rockchip 的 [rockchip_drm_fb.c:504](rockchip_drm_fb.c#L504) 正是这样做的：

```c
wait_event_timeout(dev->vblank[i].queue,
    old_state->crtcs[i].last_vblank_count != drm_crtc_vblank_count(crtc),
    msecs_to_jiffies(50));
```

VBlank 硬件触发 IRQ → IRQ handler 递增 vblank count → wake_up 唤醒 wait_queue → `wait_event_timeout` 立刻返回（通常 <100us）。50ms timeout 只是安全网，正常路径不走超时。

### 流水线设计

```
                    IRQ_A                 IRQ_B                 IRQ_C
                    (ISP完成)             (DSP完成)             (VBlank)
                      │                     │                     │
                      ▼                     ▼                     ▼
帧 N
┌─────────┐   ①     ┌─────────┐   ②     ┌──────────┐   ③
│  ISP    │────────│  DSP    │────────│ 显示控制 │
│ 采集图像 │         │ 图像处理 │         │ 扫描输出  │
│ (2ms)   │         │ (8ms)   │         │ (16ms)   │
└─────────┘         └─────────┘         └──────────┘
                      │
                      ▼
                waitq_B 上阻塞的
                dsp_worker 被唤醒
                写 DSP 寄存器
                启动 DSP

帧 N+1
          ┌─────────┐   ④     ┌─────────┐   ⑤     ┌──────────┐   ⑥
          │  ISP    │────────│  DSP    │────────│ 显示控制 │
          │ 采集图像 │         │ 图像处理 │         │ 扫描输出  │
          │ (2ms)   │         │ (8ms)   │         │ (16ms)   │
          └─────────┘         └─────────┘         └──────────┘

帧 N+2
                    ┌─────────┐   ⑦     ┌─────────┐   ⑧     ┌──────────┐   ⑨
                    │  ISP    │────────│  DSP    │────────│ 显示控制 │
                    │ 采集图像 │         │ 图像处理 │         │ 扫描输出  │
                    │ (2ms)   │         │ (8ms)   │         │ (16ms)   │
                    └─────────┘         └─────────┘         └──────────┘


各序号详解（无 fence，但基于 IRQ + wait_queue）：

① ISP 采集完帧N → 硬件触发 IRQ_A → ISR 调用 wake_up(&isp_waitq)
   → commit_worker 被精确唤醒 → 写入 DSP 寄存器 → 启动 DSP 处理帧N
   → commit_worker 随后在 waitq_B 上阻塞等 DSP 完成
   → CPU 耗时：~15us（中断处理 + work 调度 + 寄存器写入）

② DSP 处理完帧N → 硬件触发 IRQ_B → ISR 调用 wake_up(&dsp_waitq)
   → commit_worker 被精确唤醒 → 写入 VOP 寄存器（帧N的 buffer 地址）→ 启动显示扫描
   → commit_worker 返回
   → CPU 耗时：~15us

③ 显示扫描完帧N → VBlank IRQ 触发 → vblank count++ → wake_up
   → 用户态在 vblank event 上阻塞的线程被唤醒 → 帧N 的 buffer 可复用
   → CPU 耗时：~5us

④⑤⑥ 帧N+1 同理，三个硬件同时在处理不同帧
```

### 单设备流水线结论

在**不涉及跨设备 dma_buf 共享**的场景下，无 fence 方案能做到：

- 三级流水线并行，吞吐量等同于有 fence 方案
- CPU 开销极低（IRQ + wait_queue，每帧每级~15us，总共 ~35us/帧）
- Buffer 需求：2 个即可轮换（seqno 追踪精确知道何时可复用）

**单设备单消费者场景下，无 fence 方案和有 fence 方案性能相当。**

---

## 二、有 Fence 的最优设计

### 核心机制：dma_fence + drm_sched 标准化框架

```
                        硬件完成 → IRQ
                        │
                        ▼
              ┌─────────────────────┐
              │  ISR                │
              │  dma_fence_signal() │  ← 框架统一入口
              └──────────┬──────────┘
                         │
          ┌──────────────┼──────────────┐
          │              │              │
          ▼              ▼              ▼
   drm_sched 唤醒   任一 waiter     sync_file 通知
   提交下一个 job   被精确唤醒      用户态（fd poll）
```

**与无 fence 方案相比，多出的关键能力不在 ISR 路径上，而在 fence 作为"标准化的跨设备同步令牌"上。**

### 流水线设计（结构上与无 fence 方案等价）

```
帧 N
┌─────────┐  fence_A   ┌─────────┐  fence_B   ┌──────────┐
│  ISP    │───────────│  DSP    │───────────│ 显示控制 │
│ (2ms)   │            │ (8ms)   │            │ (16ms)   │
└─────────┘            └─────────┘            └──────────┘
                                                         │
                              fence_B signaled ──────────→ out_fence fd 通知用户态

帧 N+1
          ┌─────────┐  fence_C   ┌─────────┐  fence_D   ┌──────────┐
          │  ISP    │───────────│  DSP    │───────────│ 显示控制 │
          │ (2ms)   │            │ (8ms)   │            │ (16ms)   │
          └─────────┘            └─────────┘            └──────────┘
```

每级中断路径上的 CPU 开销与无 fence 方案相同（~15us/级）。

---

## 三、真正分出胜负的场景：跨设备 dma_buf 共享

这才是 fence 的核心价值所在。无 fence 方案在单设备流水线上可以做到同等性能，但一旦 buffer 要通过 dma_buf 在**两个驱动之间**传递，无 fence 方案的缺陷就暴露了。

### 场景：外部 GPU 渲染 → dma_buf → djidrm 显示

```
                  GPU 驱动                              djidrm 显示驱动
              (独立驱动，不同厂商)
                     │                                       │
                     │   GPU 异步渲染帧                       │
                     │   在 buffer->resv 挂 exclusive fence   │
                     │                                       │
                     │   GPU 完成 → IRQ                       │
                     │   dma_fence_signal(excl_fence)         │
                     │                                       │
                     ├──── dma_buf (fd) ─────────────────────→│
                     │                                       │
                     │                      djidrm import dma_buf
                     │                      从 dma_buf->resv 取 fence
                     │                      ↓
                     │                      dma_fence_wait(excl_fence)
                     │                      ↓ (fence 已 signaled，立即返回)
                     │                      安全地提交显示扫描 ← 确定 GPU 已写完
```

### 无 Fence 方案：跨设备同步断裂

```
                  GPU 驱动                              djidrm 显示驱动
                     │                                       │
                     │   GPU 异步渲染帧                       │
                     │   buffer 内部记录了 seqno=7             │
                     │   (但这是 GPU 驱动的私有数据)            │
                     │                                       │
                     ├──── dma_buf (fd) ─────────────────────→│
                     │                                       │
                     │                      djidrm import dma_buf
                     │                      读 dma_buf->resv → NULL
                     │                      读 dma_buf->ops → 无 fence 相关回调
                     │                      ↓
                     │                      无法感知 GPU 的完成状态
                     │                      ↓
                     │                      两种选择：
                     │                      A. 直接提交扫描 → 可能撕裂
                     │                      B. 硬等 50ms 超时 → 浪费延迟
```

Rockchip 驱动代码 [rockchip_drm_drv.c:3127-3128](rockchip_drm_drv.c#L3127-L3128) 正是这个问题的精确记录：

```c
if (dev->driver->gem_prime_res_obj)
    exp_info.resv = dev->driver->gem_prime_res_obj(obj);
// gem_prime_res_obj 从未赋值，exp_info.resv 永远是 NULL
// dma_buf 得到一个全新、独立的 reservation_object
// GPU 往 GEM resv 写的 fence，import 方从 dma_buf resv 里看不到
```

### 为什么无 Fence 方案无法靠自己解决这个问题

这不是设计水平的问题，而是**没有标准化的跨驱动同步令牌**。两个独立驱动之间要同步，必须有一个双方都理解的接口：

| 方案 | 可行性 |
|------|--------|
| GPU 驱动把 seqno 写到 dma_buf 的某个自定义字段 | djidrm 需要知道 GPU 驱动的私有结构体布局，每换一个 GPU 就失效 |
| 用户态协调：等 GPU fd 可读后，再通知 djidrm | 可用，但需要额外的用户态线程和上下文切换 |
| 内核统一接口：`dma_fence` 挂在 `dma_buf->resv` 上 | **这就是 fence 方案** |

**无 fence 方案的瓶颈不在性能，在互操作性。** 它无法用通用的方式表达"这个 buffer 上的这个操作做完了吗"——除非把 dma_fence 本身引入，但那就变成了有 fence 方案。

---

## 四、用户态通知对比

另一个关键差异在于如何通知用户态"buffer 可以复用了"：

```
有 Fence:
  fence_F signaled → sync_file fd → 用户态 poll(fd) 返回
  → 精确到微秒级，标准接口，Wayland/Vulkan/EGL 原生支持

无 Fence:
  VBlank IRQ → vblank count++ → drm_crtc_send_vblank_event()
  → 用户态 DRM_IOCTL_WAIT_VBLANK 或 poll(drm fd) 返回
  → 也是精确的（IRQ 驱动），但粒度为"整个显示管线的 VBlank"
  
差异：
  - 有 fence：每个 buffer 有独立的完成时间点，可以异步追踪多个 buffer
  - 无 fence：只能知道"当前 VBlank 到了"，要通过 vblank count 反推哪个 buffer 完成了
  - 对单显示管线影响不大（每个 VBlank 只提交一帧）
```

---

## 五、最终对比表

| 维度 | 无 Fence（最优设计） | 有 Fence |
|------|---------------------|----------|
| **单设备流水线吞吐量** | 等同于有 fence（IRQ+wait_queue 也能做三级并行） | 30fps（三级并行） |
| **CPU 每帧开销** | ~35us（IRQ + work 调度 + 寄存器写入） | ~20us（IRQ + fence_signal，框架消除了一些样板代码） |
| **Buffer 需求（单设备）** | 2 个（seqno 可精确追踪） | 2 个 |
| **Buffer 需求（跨设备）** | 3~4 个（保守策略：不知道外部生产者何时完成） | 2 个（fence 精确告知） |
| **跨设备同步正确性** | **无法保证**（dma_buf resv 断链，无法感知外部生产者） | fence 挂载在 dma_buf->resv，import 方可直接等待 |
| **用户态 buffer 释放通知** | VBlank event（粒度 = 整管线，单帧 OK，多帧需反推） | out_fence fd（粒度 = 单 buffer，精确） |
| **Wayland/Vulkan 互操作** | 不支持（没有 sync_file） | 原生支持（sync_file = fence fd） |
| **内存占用（4K，3 buffers）** | 96MB | 64MB（跨设备仅需 2） |
| **跨设备延迟** | +50ms（超时安全网）或不可靠 | 0（fence 已 signaled 则立即返回） |

---

## 六、结论

**单设备内三级流水线**：无 fence 方案通过 IRQ + wait_queue + work_struct 链式调度，可以达到与有 fence 方案同等的并行度和 CPU 开销。这一点上两者持平。

**跨设备 dma_buf 共享**：这是 fence 的不可替代价值。`dma_fence` 作为内核中唯一的标准跨驱动同步令牌，让 GPU/ISP/DSP 驱动的完成状态可以被任何 import 方感知。没有它，跨设备同步要么不可靠，要么需要每个生产-消费配对之间定义私有协议。

**对大疆 djidrm 的建议**：如果 djidrm 只用在自己的显示管线上（DSP→显示，单一驱动控制全部硬件），最优的无 fence 设计已经够用。如果 buffer 需要与其他驱动（ISP、外置 GPU、相机子系统）通过 dma_buf 共享，则必须引入 `dma_fence`，因为这是 Linux 内核跨驱动同步的通用语言。

---

## 七、手持设备场景：DJI Pocket 4

### 设备特征

Pocket 4 是一台手持云台相机，与无人机的关键差异：

| 特征 | 无人机 | Pocket 4 |
|------|--------|----------|
| 散热条件 | 螺旋桨风冷 | 被动散热，无气流 |
| 电池容量 | 大（飞行动力电池） | 小（手持设备电池） |
| 显示用途 | FPV 图传（驾驶参考） | 取景预览（触摸屏，交互密集） |
| 主要输出 | 图传画面 | 录制视频文件 |
| 核心功能 | 飞行+图传 | 录制+AI追踪+预览 |
| 并行管线数 | 1 条主流水线 | 3 条管线同时运行 |

### Pocket 4 的真实管线

```
                    ┌──────────────────────────────────────────┐
                    │              ISP (V4L2 驱动)              │
                    │         采集原始 Bayer 帧到 DDR            │
                    │              ~2ms/frame                   │
                    └──────────────────┬───────────────────────┘
                                       │
                            raw_frame_buffer
                                       │
                    ┌──────────────────▼───────────────────────┐
                    │              DSP (自有驱动)               │
                    │     EIS电子防抖 + 降噪 + 色彩处理           │
                    │              ~8ms/frame                   │
                    └──────────────────┬───────────────────────┘
                                       │
                         processed_frame_buffer  ← 一帧输出，三个消费者
                                       │
            ┌──────────────────────────┼──────────────────────────┐
            │                          │                          │
            ▼                          ▼                          ▼
   ┌────────────────┐        ┌────────────────┐        ┌────────────────┐
   │  显示控制器      │        │  视频编码器      │        │  NPU/追踪引擎   │
   │  (DRM 驱动)     │        │  (H.265 硬件)    │        │  (自有驱动)     │
   │  2寸触摸屏预览   │        │  录制到 SD 卡    │        │  主体检测+跟踪   │
   │  ~16ms/frame   │        │  ~20ms/frame    │        │  ~5ms/frame    │
   │  只读消费者      │        │  只读消费者       │        │  只读消费者      │
   └────────────────┘        └────────────────┘        └────────────────┘
```

**关键特征**：一个 DSP 输出帧被三个独立的硬件消费者同时读取。三个消费者分别属于不同内核子系统（DRM、V4L2（视频编码器 H.264/H.265）、NPU 驱动）。

（注意：本文的"编码器"指视频压缩硬件 Video Encoder，不是 DRM KMS 管线中的 `drm_encoder`——后者负责将像素流编码为 MIPI/LVDS 物理信号，是显示输出链的一环。）

### 无 Fence 方案在 Pocket 4 上的最优实现

**使用 `request_threaded_irq()` 替代 work_struct。** hard IRQ 只做 seqno 更新（~2us），threaded handler 以 SCHED_FIFO 优先级在进程上下文中直接写所有 consumer 寄存器（~15us），中间零调度跳、零队列。

```
                         DSP 硬件完成
                              │
                        DSP 硬中断触发
                              │
                              ▼
              ┌───────────────────────────────┐
              │  DSP hard IRQ (~2us)          │
              │  dsp_completed_seqno++        │
              │  smp_wmb()  // 内存屏障        │
              │  ack 中断寄存器                 │
              │  return IRQ_WAKE_THREAD       │
              └──────────────┬────────────────┘
                             │  ← 零调度延迟，内核立即切到 threaded handler
                             ▼
              ┌───────────────────────────────┐
              │  DSP threaded handler (~15us) │
              │  优先级 SCHED_FIFO 50         │  ← 高于所有 workqueue、kthread、
              │  (MAX_USER_RT_PRIO/2)         │     用户态进程
              │                               │
              │  // 直接写寄存器，无 work_struct │
              │  writel(buf_iova, VOP_ADDR)    │──→ 显示开始扫描帧N
              │  writel(buf_stride, VOP_STRIDE)│
              │                               │
              │  writel(buf_iova, ENC_ADDR)    │──→ 编码器开始读帧N
              │  writel(1, ENC_START)          │
              │                               │
              │  writel(buf_iova, NPU_ADDR)    │──→ NPU 开始分析帧N
              │  writel(1, NPU_START)          │
              └──────────────┬────────────────┘
                             │  ← 三个消费者已在 <20us 内全部启动
                             ▼

       各 consumer 完成时触发各自的硬中断：

   VBlank 硬中断               编码器 硬中断               NPU 硬中断
   vblank_seqno++             enc_seqno++               npu_seqno++
   return IRQ_WAKE_THREAD     return IRQ_WAKE_THREAD     return IRQ_WAKE_THREAD
        │                          │                          │
        ▼                          ▼                          ▼
   VBlank threaded handler     编码器 threaded handler     NPU threaded handler
   检查是否三个 consumer       检查是否三个 consumer     检查是否三个 consumer
   都处理完了帧N？              都处理完了帧N？             都处理完了帧N？
        │                          │                          │
        └──────────────────────────┼──────────────────────────┘
                                   │
                                   ▼
              ┌─────────────────────────────────┐
              │  每个 consumer threaded handler: │
              │                                 │
              │  consumer_done_mask[N] |= 自己的bit│
              │                                 │
              │  if (consumer_done_mask[N] ==    │
              │      ALL_THREE_DONE) {           │
              │      free_buffer(N);  // 帧N可复用│
              │  }                              │
              └─────────────────────────────────┘

Per-buffer 追踪结构：

  struct buffer_tracker {
      u32 dsp_seqno;            // DSP 完成该帧时的 seqno
      u32 display_seqno;        // 显示完成需要达到的 vblank seqno
      u32 encoder_seqno;        // 编码器完成需要达到的 seqno
      u32 npu_seqno;            // NPU 完成需要达到的 seqno
      atomic_t consumers_done;  // 位掩码：bit0=显示 bit1=编码器 bit2=NPU
  };

DSP→消费者启动延迟：~17us（硬中断2us + threaded handler 15us）
```

**此方案已消除了 workqueue 排队瓶颈**：threaded handler 以 SCHED_FIFO 优先级运行，不经过任何共享队列，直接从硬中断切换执行。SD 卡 I/O 无法延迟它。

### 有 Fence 方案在 Pocket 4 上的最优实现

```
                         DSP 硬件完成
                              │
                        DSP 硬中断触发
                              │
                              ▼
              ┌─────────────────────────────────┐
              │  DSP ISR (~5us)                 │
              │  dma_fence_signal(              │
              │    dsp_done_fence)              │
              │                                 │
              │  callback 链（在 ISR 上下文中执行）:│
              │  ├→ callback_1: wake sched_kthread│  ← 每个 consumer 独立唤醒
              │  ├→ callback_2: wake sched_kthread│
              │  └→ callback_3: wake sched_kthread│
              │  return from IRQ                │
              └──────────────┬──────────────────┘
                             │
              ┌──────────────┼──────────────────┐
              │              │                  │
       sched 调度延迟     sched 调度延迟      sched 调度延迟
       ~10-50us          ~10-50us           ~10-50us
              │              │                  │
              ▼              ▼                  ▼
   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
   │ 显示 kthread  │  │ 编码器 kthread│  │ NPU kthread   │
   │ SCHED_NORMAL  │  │ SCHED_NORMAL  │  │ SCHED_NORMAL   │
   │ run_job()    │  │ run_job()    │  │ run_job()     │
   │ 写VOP寄存器(~5us)│ │ 写编码器DMA(~5us)│ │ 写NPU描述符(~5us)│
   └──────────────┘  └──────────────┘  └──────────────┘

         三 consumer 可并行调度（多核 SoC 上）
         单核上调度器依次选择，总启动时间 = sum(每个 5us + 调度间隔)

   各 consumer 完成时：
          │                 │                  │
          ▼                 ▼                  ▼
   显示 fence signaled  编码 fence signaled  NPU fence signaled
          │                 │                  │
          └─────────────────┼──────────────────┘
                            │
                            ▼
              ┌─────────────────────────────────┐
              │  dma_resv 自动追踪：              │
              │  buffer->resv 上有：              │
              │    1 个 exclusive fence (DSP写)  │
              │    3 个 shared fence slots       │
              │      [0] 显示读完成 fence         │
              │      [1] 编码器读完成 fence       │
              │      [2] NPU 读完成 fence        │
              │                                 │
              │ 所有 fence signaled →            │
              │  dma_resv 通知 buffer 可释放      │
              └─────────────────────────────────┘

DSP→消费者启动延迟：~5us (fence_signal) + kthread 调度延迟 + ~5us (run_job)
  系统空闲时：  ~5 + 10 + 5 = ~20us
  系统繁忙时：  ~5 + 50 + 5 = ~60us（kthread 竞争 CPU 但不会被 workqueue 阻塞）
```

**每帧 CPU 开销 ~40us**（fence_signal + 回调 + 3 × run_job），buffer 追踪由 dma_resv 框架自动完成。

### 手持设备场景下的关键差异

#### 差异一：消费者新增的代码代价

Pocket 5 假设新增一路 WiFi 推流消费者，读取同一帧 DSP 输出：

```
无 Fence 方案需要修改的代码（threaded IRQ 版本）：
  1. 在 DSP threaded handler 中添加 writel(wifi_iova, WIFI_ADDR)
  2. 在 buffer_tracker 中新增 wifi_seqno 字段
  3. 将 ALL_THREE_DONE 改为 ALL_FOUR_DONE
  4. 为 WiFi 完成注册新的 threaded IRQ handler，含 done_mask 检查逻辑
  修改范围：~4 处代码

有 Fence 方案需要修改的代码：
  1. 新建 wifi_sched_entity，实现 run_job() callback
  2. 在 run_job() 内调用 dma_fence_add_callback(dsp_done_fence, ...)
  3. 在 wifi fence signaled 时将 wifi 的 shared fence 写入 buffer->resv
  修改范围：~1 个新文件或 1 个新 callback，不修改已有 consumer 代码
```

两者差距比 work_struct 扇出版本缩小了（threaded IRQ 已经消除了"修改扇出 worker"这一步），但 fence 方案仍然不需要修改已有 consumer 的任何代码。

#### 差异二：跨子系统消费者

Pocket 4 的三个消费者属于不同内核子系统：

| 消费者 | 所属子系统 | 是否可能由其他驱动控制 |
|--------|-----------|----------------------|
| 显示控制器 | DRM (djidrm) | 自有驱动 |
| 视频编码器 | V4L2 mem2mem 或 platform 驱动 | **通常为独立驱动** |
| NPU 追踪 | 自有驱动或 AI 框架 | 自有或第三方 |

如果视频编码器是独立的 V4L2 驱动（大多数 Linux SoC 的编码器都是独立 platform 驱动），那么编码器驱动**已经使用 dma_fence/dma_resv 进行 buffer 同步**（V4L2 框架要求）。此时：

- **有 Fence**：djidrm 的 dsp_done_fence 写入 buffer->resv 的 exclusive slot → 编码器驱动通过标准 V4L2 接口 import dma_buf → 自动等待 exclusive fence → djidrm 和编码器驱动之间无需任何私有接口
- **无 Fence**：djidrm 无法通过通用接口通知编码器驱动"DSP 已经写完"。必须设计私有同步机制（如自定义 ioctl、共享内存 seqno 等），编码器驱动需要针对 djidrm 做特殊适配

**这一条是 Pocket 4 必须引入 fence 的决定性原因，与性能无关。**

#### 差异三：散热约束下的 CPU 预算

Pocket 4 被动散热，持续录制时 CPU 必须尽量空闲以避免降频：

```
持续录制 30fps 场景（CPU 时间预算 = 33ms/frame）：

无 Fence 方案 CPU 占用（threaded IRQ 版本）：
  DSP hard IRQ:                    ~2us
  DSP threaded handler (3×MMIO):   ~15us
  各 consumer 完成路径：
    显示: hard IRQ + th_handler + done_check: ~15us
    编码器: hard IRQ + th_handler + done_check: ~15us
    NPU: hard IRQ + th_handler + done_check:   ~15us
  ─────────────────────────────────────
  合计：                              ~62us/frame = 0.19% CPU

有 Fence 方案 CPU 占用：
  DSP ISR + fence_signal:         ~5us
  显示 run_job + fence_signal:    ~10us
  编码器 run_job + fence_signal:  ~10us
  NPU run_job + fence_signal:     ~10us
  dma_resv fence 合并（框架自动）: ~5us
  ─────────────────────────────────────
  合计：                          ~40us/frame = 0.12% CPU

差异：每帧 22us。在 0.19% vs 0.12% 这个量级，两者都不会导致
散热问题。consumer 数量增长时，两者均近似线性增长。
```

#### 差异四：buffer 释放时机的最坏情况

两者的正常路径释放时机相同（所有 consumer 完成 = buffer 释放）。但异常路径不同：

```
场景：录制过程中用户切到回放模式，编码器被突然关闭

无 Fence 方案：
  编码器 threaded handler 被注销 → consumer_done_mask 中的 encoder bit
  永远不会被置位 → 帧N的 buffer 永远无法释放（引用泄漏）
  → 必须额外实现 consumer 注销时的 done_mask 清理逻辑
  → 容易遗漏，导致内存泄漏

有 Fence 方案：
  编码器驱动调用 dma_fence_put() 释放对 dsp_done_fence 的引用
  → 编码器的 shared fence slot 被清空
  → dma_resv 自动将已注销 consumer 的 slot 标记为 unused
  → buffer 释放逻辑不受影响
  → dma_fence 引用计数机制保证生命周期正确
```

#### 差异五：CPU 繁忙时谁更容易丢帧

**在拿出各自最优设计之后，两方案的丢帧风险已经非常接近。** 关键变量是"DSP 完成 → 消费者寄存器被写入"的延迟是否可控。

**两方案最优实现的延迟路径对比**：

```
无 Fence (threaded IRQ, SCHED_FIFO 50):
  DSP完成 → hard IRQ(2us) → threaded handler(15us, 直接写寄存器)
  总延迟：~17us，几乎零变异（RT 优先级，无队列等待）

有 Fence (dma_fence + kthread, SCHED_NORMAL 120):
  DSP完成 → hard IRQ(5us, fence_signal + callbacks)
         → 调度器选择 kthread(~10-50us)
         → run_job()(5us, 写寄存器)
  总延迟：~20-60us，变异来自 CFS 公平调度
```

**关键反转**：在最优实现下，无 fence 的 threaded IRQ 方案（SCHED_FIFO 50）调度优先级**高于** fence 方案的 kthread（SCHED_NORMAL 120）。CPU 繁忙时 RT 线程立即抢占普通进程，kthread 需等 CFS 时间片。但这不是无条件的优势——threaded handler 必须在一次执行中写完三个 consumer 寄存器（串行不可打断），而三个 kthread 可以在多核上并行。

**极端场景推演**：SD 卡写入尖峰 + NPU 推理同时跑，双核 SoC，T=0 DSP 完成帧N

```
无 Fence (threaded IRQ, SCHED_FIFO 50):
  T=0     DSP硬中断(CPU0, 2us)
  T≈2us   threaded handler 被调度，RT优先级 → 立即抢占CPU0上的SD写入线程
  T=2us   写VOP寄存器(5us)
  T=7us   写编码器DMA(3us)
  T=10us  写NPU描述符(2us)
  T=12us  全部consumer寄存器写入完成
  → 总延迟 12us，几乎不受系统负载影响

有 Fence (dma_fence + kthread, SCHED_NORMAL):
  T=0     DSP硬中断(CPU0, 5us, fence_signal + 3个wake_up_process)
          display/encoder/npu 三个kthread全部进入runnable
  T≈10us  调度器选encoder_kthread(CPU1, NPU推理刚好让出CPU)
          run_job() → 写编码器DMA(5us) ✓
  T≈15us  调度器选display_kthread(CPU0, 但SD写入线程正跑)
          等SD写入线程用完时间片
  T≈30us  display_kthread获CPU(CPU0)
          run_job() → 写VOP寄存器(5us) ✓
  T≈40us  调度器选npu_kthread(CPU1, encoder刚让出)
          run_job() → 写NPU描述符(5us) ✓
  T≈45us  全部consumer寄存器写入完成
  → 总延迟 45us

差值：45us vs 12us。但在33ms(30fps)尺度下都是微秒级，远不到丢帧阈值。
```

**真正的丢帧风险不在最优设计的微秒级差异，而在次优实现选择**：

| 实现选择 | DSP→消费者延迟 | 丢帧风险 |
|----------|---------------|---------|
| Threaded IRQ (SCHED_FIFO 50) | ~17us 固定 | 极低（无队列，RT 抢占） |
| 专用 WQ_HIGHPRI + per-consumer worker | ~30-100us | 低（高优队列，不受 system_wq 影响） |
| system_wq 单 worker 扇出 | 20us~12ms | **高**（排队在 SD 卡 I/O work 后面） |

```
之前文档展示的 12ms 延迟场景，根源不是"无 fence"，
而是"system_wq 单 worker 扇出"这个次优选择。
Threaded IRQ 方案不经过 workqueue，已消除此瓶颈。
```

**两方案最优实现下的丢帧风险总结**：

| 维度 | 无 Fence (threaded IRQ) | 有 Fence (fence + kthread) |
|------|------------------------|---------------------------|
| DSP→消费者启动延迟 | ~17us (固定, SCHED_FIFO) | ~20-60us (可变, SCHED_NORMAL) |
| 延迟可预测性 | 极高（RT 优先级，零队列） | 高（独立 kthread，无 workqueue 排队） |
| 受 SD 卡 I/O 影响 | 无（RT 抢占普通线程） | 轻微（kthread 公平竞争 CPU） |
| 受 NPU/用户态负载影响 | 无（RT 抢占） | 中等（同等 SCHED_NORMAL 竞争） |
| 多核并行能力 | 无（~12us 串行在单核） | 有（3 kthread 可并行在不同核） |
| 单 consumer MMIO 异常阻塞 | 连累全部 consumer（同线程串行） | 仅影响该 consumer（独立 kthread 隔离） |
| 实际丢帧阈值 | >16ms 延迟才触发 | >16ms 延迟才触发 |
| 正常路径丢帧概率 | 极低 | 极低 |

**结论**：在各自最优实现下，两方案的丢帧风险都在微秒级，远低于毫秒级丢帧阈值。**正常路径下两者都不会丢帧。** Threaded IRQ 以 RT 优先级获得更可预测的延迟；Fence 以独立 kthread 获得更好的消费者隔离和多核扩展。fence 的结构性优势在于**单 consumer 硬件异常隔离**——异常发生时独立 kthread 只阻塞该 consumer，threaded handler 会连累其他 consumer。但这是硬件 bug 场景，正常运行时不存在。

### 手持设备对比表

| 维度 | 无 Fence（threaded IRQ 最优） | 有 Fence（kthread 最优） |
|------|---------------------------|------------------------|
| **三消费者流水线吞吐量** | 30fps（threaded handler 一次性启动全部） | 30fps（独立 kthread 并行启动） |
| **CPU 每帧开销** | ~62us（6个 threaded handler 路径） | ~40us（fence_signal + 3 × run_job） |
| **DSP→消费者启动延迟** | ~17us (固定, SCHED_FIFO 50) | ~20-60us (可变, SCHED_NORMAL 120) |
| **延迟受系统负载影响** | 几乎为零（RT 优先级抢占） | 轻微（CFS 公平调度） |
| **多核并行** | 否（单线程串行执行） | 是（多 kthread 可跨核） |
| **单 consumer 异常隔离** | 否（同一线程，连累其他） | 是（独立 kthread，互不影响） |
| **CPU 开销增长（新增消费者）** | +15us/consumer + 修改 done_mask | +10us/consumer + 新增 fence callback |
| **Buffer 需求** | 2~3 个（手动 seqno 追踪） | 2 个（dma_resv 自动追踪） |
| **消费者动态注销** | 需手动清理 done_mask，容易泄漏 | dma_fence_put → dma_resv 自动清理 |
| **新增消费者代码量** | ~4 处修改 | 新增 1 个 callback，不修改已有代码 |
| **跨子系统消费者（独立驱动）** | 需私有同步协议 | dma_fence 通用接口，无需适配 |
| **与 V4L2 编码器互通** | 需在编码器驱动中添加 djidrm 特定同步代码 | 编码器驱动已支持 dma_buf + fence，零改动 |
| **正常路径丢帧概率** | 极低 | 极低 |

### 手持设备结论

**最优实现下，两方案在丢帧风险上等同。** 之前分析的毫秒级延迟差异来自 system_wq work_struct 排队——那是次优实现选择，而非"无 fence"的结构性缺陷。Threaded IRQ 方案证明了不需要 dma_fence 也能实现低延迟、高确定性的消费者启动。差异现在集中在工程维度：

1. **跨子系统消费者必须用 fence** — Pocket 4 的视频编码器(H.264/H.265 压缩硬件)几乎必然是独立的 V4L2 驱动，已经使用 dma_fence/dma_resv 进行 buffer 同步。无 fence 方案无法通过通用接口与 V4L2 视频编码器通信。**仅此一条，djidrm 就必须支持 fence。**

2. **消费者动态管理** — fence 的引用计数模型在 consumer 新增/注销、buffer 提前释放等边界情况提供更自然的处理。无 fence 方案需要手动管理 done_mask 和 seqno 生命周期。

3. **代码工程性** — 随着消费者数量增长，fence 的 callback 模型比 threaded handler 中逐步添加寄存器写入更模块化。

**结论：单设备单消费者 → 两者等同。单设备多消费者（同驱动控制）→ 丢帧风险等同，fence 在代码工程性上略胜。跨驱动消费者 → fence 是唯一可行方案。** Pocket 4 的视频编码器(H.265)在 V4L2 子系统中，这一条就是 djidrm 必须引入 fence 的充分理由，与性能无关。
