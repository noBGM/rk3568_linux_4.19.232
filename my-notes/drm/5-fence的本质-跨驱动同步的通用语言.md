# Fence 的本质：跨驱动同步的通用语言

## 核心命题

**"知道 GPU 第 N 次提交是否完成"这件事，不需要 `dma_fence`。中断 + seqno + wait_queue 完全够用。**

MSM 自己就是这样做的：

```c
// msm_fence.c — 核心判断逻辑，跟 dma_fence 框架无关：
static bool fence_completed(struct msm_fence_context *fctx, uint32_t fence)
{
    return (int32_t)(fctx->completed_fence - fence) >= 0;
}
```

`completed_fence` 的值来自 GPU 硬件写回共享内存 + IRQ。驱动在 ISR 中读到后更新变量，然后 `wake_up_all(&fctx->event)` 唤醒等待者。dma_fence 框架没有参与"知道完成"本身。

**那么 MSM 和 Etnaviv 为什么还要把自家的 seqno + event 包装成 `dma_fence`？** 因为完成信息需要跨越三个边界。

---

## 边界一：驱动 A → 驱动 B（内核内部跨驱动）

```
GPU 驱动 (MSM) 渲染完一帧 → buffer 通过 dma_buf 传给显示驱动 (djidrm)

MSM 内部：
  fctx->completed_fence = 7   ← 知道第7次提交完成了
  wake_up(&fctx->event)       ← MSM 自己的线程可以醒来

djidrm import 这个 buffer：
  请问 fctx 变量在哪？怎么读？fctx 是 struct msm_fence_context *，
  定义在 drivers/gpu/drm/msm/msm_fence.h，外部驱动根本看不到。
```

**无 fence 的情况下**，两个驱动之间要知道"buffer 的操作完成了"，只有三条路：

| 方案 | 可行性 |
|------|--------|
| djidrm `#include "msm_fence.h"`，直接读 MSM 的私有 seqno | 编译时绑定死 MSM，换一个 GPU 驱动就失效 |
| dma_buf 的 priv 字段放一个自定义结构体，两边约定格式 | 每对驱动之间需要单独约定，N 个驱动 = N² 套协议 |
| dma_buf->resv 上挂 `dma_fence` | **fence 方案：任何驱动都会看 resv，零适配** |

**`dma_fence` 的价值**：把"第 N 次提交是否完成"包装成一个所有内核驱动都认识的标准对象。生产者调用 `dma_fence_signal(fence)`，消费者调用 `dma_fence_wait(fence)`。双方不需要知道对方的任何私有数据结构。

```
有 fence：
  MSM → dma_fence_signal(fence) → fence 挂在 dma_buf->resv 上
  djidrm → dma_buf_get(dmabuf) → 读 dmabuf->resv → dma_fence_wait(fence)
  → 通用路径，不依赖任何私有头文件

无 fence：
  MSM 的 completed_fence 是私有变量
  djidrm 根本无法感知这个变量的存在
  → 只能硬等超时或依赖用户态协调
```

---

## 边界二：内核 → 用户态

```
Vulkan 应用：
  vkQueueSubmit(queue, submits, fence)
  → 需要内核返回一个"GPU 完成"的信号
  → 应用可以 poll 这个信号，或传给 vkWaitForFences()

EGL 应用：
  eglCreateSyncKHR(EGL_SYNC_FENCE_KHR, ...)
  → 同样需要一个可 poll 的完成信号
```

Linux 内核只有一个标准方式把"完成信号"暴露为文件描述符：`sync_file`。而 `sync_file` 封装的就是 `dma_fence`。

```
无 fence：
  MSM 的 fctx->event 是 wait_queue_head_t，在内核地址空间
  用户态没有标准方式把一个 wait_queue 转换为可 poll 的 fd
  → 只能 ioctl 轮询 seqno，或 sleep+超时

有 fence：
  dma_fence → sync_file_create(fence) → fd
  用户态 poll(fd) → 精确到微秒级
  Vulkan/EGL/Wayland 原生支持 sync_file fd
```

用户态本身可以用 `DRM_IOCTL_WAIT_VBLANK` 感知显示完成（IRQ 驱动，精确），但**无法用通用方式感知 GPU 完成**。GPU 完成的 seqno 是 GPU 驱动的私有数据，没有 `dma_fence` 就没有 `sync_file`，没有 `sync_file` 就没有标准的 fd 出口。

---

## 边界三：跨子系统（DRM ↔ V4L2）

Pocket 4 的真实场景：

```
ISP (V4L2 驱动, drivers/media/platform/xxx_isp.c)
    ↓ raw_frame (dma_buf)
DSP (自有驱动, drivers/gpu/drm/djidrm/)
    ↓ processed_frame (dma_buf)
编码器 (V4L2 mem2mem 驱动, drivers/media/platform/xxx_enc.c)
```

ISP 和编码器都是 V4L2 驱动，**V4L2 框架要求所有 buffer 操作通过 dma_buf->resv 上的 dma_fence 同步**。编码器 import dma_buf 后，调用 `dma_resv_wait_timeout_rcu(resv, ...)` 等待 exclusive fence。

如果 djidrm（DSP 驱动）不把 DSP 完成的标记输出为 `dma_fence` 并挂到 `dma_buf->resv` 上：

```
djidrm DSP完成 → 更新私有 seqno → wake_up 私有 waitq
                              ↓
编码器 import 同一个 dma_buf → 读 dma_buf->resv → 空的，没有 fence
                              ↓
编码器立即开始读取 → DSP 可能还在写 → 读到半成品帧
```

**这不是性能问题，是正确性问题。** 且 djidrm 无法绕过——因为编码器是 V4L2 框架的驱动，它只会通过 `dma_buf->resv` 上的 `dma_fence` 来做同步。djidrm 要么输出 `dma_fence`，要么无法与任何 V4L2 驱动交互。

---

## 总结

```
驱动内部知道完成：
  IRQ + seqno + wait_queue     ← 不需要 dma_fence
  （MSM、Etnaviv 都先用这个感知完成，再套一层 fence 暴露出去）

把完成信息传给另一个内核驱动：
  dma_fence 挂在 dma_buf->resv  ← 需要 dma_fence（跨驱动通用令牌）

把完成信息传给用户态：
  dma_fence → sync_file fd      ← 需要 dma_fence（用户态唯一标准接口）

把完成信息传给其他内核子系统：
  dma_fence 挂在 dma_buf->resv  ← 需要 dma_fence（V4L2/DRM 框架的共同语言）
```

**`dma_fence` 不解决"怎么知道完成了"——中断硬件已经解决了。`dma_fence` 解决的是"我知道完成了，怎么让不知道我内部结构的其他人也知道"。**

| 问题 | 需要 dma_fence 吗？ | 替代方案 |
|------|-------------------|---------|
| 驱动内部感知 GPU 完成 | 不需要 | IRQ + seqno + wait_queue |
| 跨内核驱动传递完成信息 | **需要** | 无通用替代；只能逐对约定私有协议 |
| 用户态感知 GPU 完成 | **需要** | 无标准替代；只能 ioctl 轮询或设超时 |
| 跨子系统 (DRM↔V4L2) 传递 | **需要** | 无；对方框架强制要求 dma_fence |
| Buffer 随 dma_buf 流转时携带完成信息 | **需要** | 无；dma_resv 只接受 dma_fence |

---

## 对大疆 djidrm 的建议

djidrm 上不上 fence，本质是一个工程权衡。私有协议**完全可以工作**——DJI 控制整个 SoC 的软件栈，视频编码器(H.265)也是自己写的驱动，不存在"改不了别人的驱动"的问题。

### 路径 A：私有协议

DSP 驱动和视频编码器(H.265)驱动约定一个共享结构体，挂在 dma_buf 的 priv 上：

```c
// djidrm 和视频编码器驱动共同 include 的头文件
struct dji_sync_token {
    u32 dsp_seqno;          // DSP 完成到第几帧了
    wait_queue_head_t wq;   // 视频编码器驱动可以在这上面等
};
```

- DSP 完成一帧 → 更新 `token->dsp_seqno` → `wake_up(&token->wq)`
- 视频编码器 import dma_buf → 从 priv 取出 token → `wait_event(token->wq, token->dsp_seqno >= needed_seqno)`
- 两方驱动各自 include 同一个头文件，约定好格式

**优点**：
- 实现简单，不需要理解 `dma_fence` 的状态机、memory ordering、callback 生命周期
- 完全可控，协议字段可以按需增减
- 不引入 `drm_sched` 框架的依赖

**缺点**：
- 每新增一个消费者（如未来加 WiFi 推流），需要在私有协议中新增对应字段或让新消费者也认识 `dji_sync_token`
- 用户态无法用标准方式（`poll(fd)`）等待完成——需要自定义 ioctl 或依赖 VBlank event
- 如果某天 DJI 用了第三方 IP（如授权自其他厂商的视频编码器 IP 带标准 V4L2 驱动），私有协议就不通了
- 新工程师接手时，需要额外学习这套私有同步机制

### 路径 B：dma_fence 标准接口

DSP 完成时调用 `dma_fence_signal()`，fence 挂在 `dma_buf->resv` 上。编码器通过标准 V4L2 接口 import 时自动等待。

**优点**：
- 任何认识 dma_buf 的驱动都能自动同步，不需要改消费者代码
- 用户态通过 `sync_file` fd 可以 poll 完成时间
- 后续新增消费者零适配成本
- 符合内核主流模式，代码可参考 etnaviv/v3d

**缺点**：
- 需要理解 `dma_fence` 框架（`dma_fence_ops`、`dma_fence_signal` 语义、callback 管理、memory ordering 保证）
- 如果配合 `drm_sched` 使用，需要实现四个 backend callback（`run_job`、`dependency`、`timedout_job`、`free_job`）
- 引入框架依赖，增加内核模块的复杂度

### 决策建议

| 条件 | 建议 |
|------|------|
| 仅 2 个驱动互通，未来不会增加消费者 | 私有协议更简单，不引入 fence 框架的认知负担 |
| 消费者 ≥3 个，或未来可能增加 | 私有协议的扇出复杂度开始超过 fence 的一次性学习成本 |
| 需要用户态通过标准 fd poll 等待 DSP 完成 | 只能选 fence（sync_file 是唯一的 fd 出口） |
| 可能引入第三方 IP 的视频编码器/ISP 驱动 | 只能选 fence（第三方驱动不会认识私有协议） |
| 团队有充足时间学习 dma_fence 框架 | 选 fence，一次学习长期受益 |

**两种路径都是对的，取决于 DJI 当前的工程约束和未来的演进预期。** 不存"必须上 fence"这一说。
