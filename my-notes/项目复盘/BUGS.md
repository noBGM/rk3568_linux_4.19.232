QM001

1. 充电时启动黑屏、休眠时报错：应用和驱动不兼容，新的spi屏驱动是使用的新的simple pipeline框架，在一次atomic commit里，plane和crtc的状态需要一致

2. 快速logo：LK阶段识别到屏幕，并将信息传递给

   1. 花屏问题：毛刺信号。没有听供应商的，先清零一次使得信号处于一个可预测的状态。
   2. OQ102冷启动不出图：LK阶段enable函数里面耦合了对屏幕信息的第一次读取，并存储到cmdline里。

3. vblank超时问题

   1. commit tail里的vblank等待200ms超时，但是spi屏幕驱动可以等待2s的时间。不对称。
      1. 是否可以去掉tail的vblank等待？
         1. 去掉后是否会影响weston的节流？
            1. weston节流有多个机制：
               1. 它自己实现了commit tail里的vblank监听机制，有1s的超时时间。
               2. commit时drm框架会wait_for_dependencies，等待上一次的hw_done完成。
         2. 去掉后会影响正在显示的buffer丢失或者被覆盖吗？
            1. commit plane的逻辑是先begin（plane排序和叠加），再update（写影子寄存器），最后flush（使影子寄存器生效）。在该帧完成显示后，调plane的clean_fb函数（如有，把fb回收。对应prepare_fb的反操作）

4. hw_done阶段，crtc_newstate->event残留会触发warning

   1. [drm_mode_atomic_ioctl](https://elixir.bootlin.com/linux/v5.19.17/C/ident/drm_mode_atomic_ioctl)调用 [prepare_signaling](https://elixir.bootlin.com/linux/v5.19.17/C/ident/prepare_signaling) 拷贝用户态传下来的事件到内核。如果没有传用户态事件，则在 [drm_atomic_helper_setup_commit](https://elixir.bootlin.com/linux/v5.19.17/C/ident/drm_atomic_helper_setup_commit)会主动创建一个内核事件。

   2. 在什么场景下，会有crtc_newstate->event残留？

      1. 在simple pipeline里，我们在plane的helper函数update会消费此event。
      2. tail rpm的commit有[DRM_PLANE_COMMIT_ACTIVE_ONLY](https://elixir.bootlin.com/linux/v5.19.17/C/ident/DRM_PLANE_COMMIT_ACTIVE_ONLY)标志，只有active的crtc对应的plane才会被处理，调用其diable或update函数。

      如果用户态只关crtc，plane保持使能，就会跳过对该plane的处理，包括同一个函数里后续的atomic_flush调用也会被跳过。

5. 静电检测问题：

   1. 供应商说静电检测只有通过TE这一手段，但是在花屏时TE是正常的。方案只能是持续软复位。那持续软复位的代价是什么？

DT501

1. 音频：对数据手册和协议的理解不充分导致没有元数据发送给sink端，所以后者是靠默认的采样率来解析音频数据，恰好蒙对。当链路不稳定时，会导致可能解析到错误的元数据。
2. 视频：发现一点不对就说是你的问题，对可能造成影响的参数并不知情。色彩不对，量化范围没有正确传输。打通链路。
3. I/P切换：前向问题为了解决phy状态问题新增的reset逻辑所导致。
4. 时序问题：时序没有和数据手册一致，导致存在风险。
5. 二供屏：使用相同GPIO的TP，如何避免资源冲突。
6. 手动重启时会有commit报错：SPI驱动check问题。
7. HDMI验证：pattern叠加。
8. 粉红屏幕：丢给驱动。诊断是硬件问题。因为让其烧录不同版本都是粉红色，排除软件问题。
9. 4P死黑问题：DP有问题，而HDMI没有。发现源就是错的，排除DT501问题。

CE2514

OQ102

WA030

OC180

OQ103