# 图形、音频与视频

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3](#adr-0003)
- [ADR-0019 · 桌面呈现管线与零拷贝方向](#adr-0019)
- [ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载](#adr-0021)
- [ADR-0027 · AudioTrack stream 按构造缓冲字节回压](#adr-0027)

<a id="adr-0003"></a>

## ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3

- 状态：Accepted
- 日期：2026-08-03

### 背景

DEMO 的手写 GLES→桌面 GL 转译产生能力上报、固定管线和跨平台语义问题。

### 决定

GLES 语义交给 ANGLE；guest 边界调用由 IDL 生成。窗口、键鼠和手柄统一使用 SDL3。

### 后果

Windows/Linux/macOS 共用实现，CI 可接软件后端；禁止迁移 DEMO 的 `gl_bridge.cpp`。

<a id="adr-0019"></a>

## ADR-0019 · 桌面呈现管线与零拷贝方向

- 状态：Accepted
- 日期：2026-08-11

### 背景

当前每帧路径是 ANGLE pbuffer 渲染 → `glReadPixels` 全帧回读 CPU → 窗口上传显示。
WU-PERF-04..06 之后的稳定期采样（Dungeon Hunter，macOS Release）显示主线程约 50%
耗在 `glReadPixels`（ANGLE Metal `waitUntilCompleted` + `getBytes` CPU 拷贝），
呈现上传约 10%，guest 执行仅 15%。回读是结构性瓶颈：它强制 CPU/GPU 每帧串行，
且随超采样倍率平方放大。

同时，MCP `frame_capture`、golden SHA-256 断言与 Scenario 证据链都依赖 CPU 像素，
任何零拷贝方案必须保留机器可判定的回读路径。

### 决定

1. 桌面窗口呈现统一经 SDL_Renderer 流式纹理上传与 GPU 缩放合成（WU-PERF-06 已
   落地）；禁止回到 CPU surface blit。
2. 长期方向：ANGLE EGL window surface 直接渲染，普通帧不再经过 CPU；回读收敛为
   按需路径（MCP capture、golden 断言、`--exit-after-frames` 证据），保持机器可
   判定测试不变。该工作量为里程碑级，需解决 SDL3 窗口与 EGL surface 的生命周期
   绑定、超采样 blit 与 resize，单独立项排期。
3. 在 2 落地前允许的中间优化：PBO/fence 异步回读（一帧延迟换取解除每帧
   `waitUntilCompleted` 串行）、按 present 需要降频回读。任何中间优化不得改变
   capture/golden 语义。

### 后果

- 呈现语义（黑边、等比缩放、present 计数）由 hal 契约固定，与实现路径解耦。
- 回读成为显式的证据路径而不是呈现路径的一部分，为窗口 surface 迁移铺平接口。
- 在 2 未完成期间，回读仍是帧率上限的主要因素，性能基准应分别记录
  「含回读」与未来「直接呈现」两组数据。

<a id="adr-0021"></a>

## ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载

- 状态：Accepted
- 日期：2026-08-12

### 背景

多款目标游戏在启动或过场时通过 `android.widget.VideoView` 播放本地视频文件
（已核实样本为 MPEG-4 Part 2 + AAC 的 mp4 容器）。当前 `android.videoview.*`
intrinsic 是记账桩：`setVideoPath` 丢弃路径，`start()` 立即回调 `onCompletion`，
表现为一段黑屏后直接进入下一个 Activity。VideoView 是游戏进程直接调用的能力，
落在 OGPlay 范围内（ADR-0001）；伪造完成违反“未实现能力必须诚实失败”的纪律，
但对该能力的正确前进方向是真实解码，而不是继续记账。

约束：

1. 编解码需覆盖 mp4v/H.264 + AAC，且跨 Windows/Linux/macOS 三平台。
2. 构建与 CI 不得引入新的编译期依赖；无解码器的机器上测试必须全绿。
3. 播放位置必须由统一 Clock 推进（ADR-0004），manual-step 与 Scenario 下可复现。
4. 线程模型保持一个 guest 线程对应一个宿主线程；解码内部线程不得回调 guest。

### 决定

1. 新建同层模块 `src/video`（与 audio/input 同层），定义拉模型 `VideoPlayer`
   接口：调用方持有位置时钟并轮询取帧/取 PCM，实现内部可以有工作线程但绝不
   向外回调。`onCompletion` 等 guest 回调由 guest 循环在轮询到结束事实后自行
   触发。
2. 真实解码后端选 **FFmpeg**（avformat/avcodec/swscale/swresample）,以
   **运行时动态加载**（`LoadLibrary`/`dlopen`）接入：构建期零依赖，符号在首次
   使用时解析，可用性可查询。LGPL 动态链接合规；共享库随发行版放在可执行文件
   旁，分发方式仿照 ANGLE 预编译产物（ADR-0014）。
3. 回退语义：FFmpeg 缺失、文件缺失或解码失败时，videoview 走结构化 warn +
   capability 记账 + 立即 `onCompletion`（即改造前行为）。不伪造播放进度，
   不静默吞错误。
4. 测试基线用确定性的 `FakeVideoPlayer`（合成帧与 PCM）承担全部行为测试；
   FFmpeg 后端只保留可用性探测测试与检测到共享库才运行的可选 smoke 测试。

### 后果

- 视频画面经 boundary 的软件帧发布进入既有帧存储与呈现管线（ADR-0019 的回读
  与 golden 语义不变），音频 PCM 混入既有音频输出;两条路径都不新增平台 API
  调用点。
- CI 与开发机不装 FFmpeg 也能验证全部行为逻辑；真实解码质量依赖本地放置的
  共享库,属于发行装配问题而不是构建问题。
- FFmpeg ABI 随大版本变化,加载器按平台常见的库名/版本号列表探测；不在探测
  列表内的版本表现为“不可用”并走回退,不做半兼容。

<a id="adr-0027"></a>

## ADR-0027 · AudioTrack stream 按构造缓冲字节回压

- 状态：Accepted
- 日期：2026-08-29
- 关联：[ADR-0023](diagnostics.md#adr-0023)、
  [ADR-0025](diagnostics.md#adr-0025)、
  [DVM-93](../tasks/dexvm/DVM-93.md)

### 背景

API 19 `AudioTrack.write` 明确规定 MODE_STREAM 会阻塞直到数据全部写入 audio sink；JNI
`writeToTrack` 同样调用 native `AudioTrack::write`。OGPlay 的 legacy 路径却把 mixer 的
255 项队列当作 Android 缓冲：满时抛宿主 C++ 异常；DexVM 路径则返回 0。前者可让异常越过
Java 边界并终结 native audio worker，后者也没有 Android 的阻塞语义。以 4096-byte write
为例，255 项还会积累约 1 MiB，而 guest 构造时请求的 35280-byte buffer 只有约 0.2 秒。

### 决定

共享 `OpenSlesPcmMixer` 提供可中断的 blocking enqueue，并以每个 AudioTrack 构造时的
`buffer_size` 作为未消费 PCM 字节上限；首 buffer 已播放的 frame 不再计入积压。原有 item
capacity 只保留为有界内存护栏，不再定义正常回压点。播放推进、clear 和 player destroy 都
唤醒 writer；不使用墙钟 sleep、轮询步进或固定重试预算。

legacy Java/JNI 与 DexVM MODE_STREAM 统一使用该原语。Legacy 在等待期间不持 media state
mutex；DexVM 在等待前释放全部 `VmExecutionLock` 深度，唤醒后恢复并重新验证 track/player
identity。MODE_STATIC 保持一次复制语义。release/销毁或 teardown 中断返回
`ERROR_INVALID_OPERATION`，队列饱和不再抛宿主异常或返回 0。

`BeginTeardown()` 与 process Stop 对 audio wait 发布粘性中断，再进行既有 futex/monitor
唤醒。阻塞发生在 JNI/HLE 边界内部，不消耗 guest tick；完成的 JNI 重入仍沿 ADR-0023 作为
既有 advanced 进展，不新增按轮询次数续期的类别。

### 后果

producer 由真实 mixer 消费速率节流，稳态未消费字节不超过构造 buffer，短音效不再排在数秒
PCM 后面；队满也不会杀死 guest audio worker。位置回调中的重入 write 仍可工作，但和真机
一样只有在播放已释放足够字节时才返回；测试不得依赖无限队列。共享 backend 暴露 queued
bytes 与 blocking-writer 数，legacy snapshot 另记录成功 write 次数、非零 write、sample peak
和当前积压，供机器验收与后续受控诊断投影复用。
