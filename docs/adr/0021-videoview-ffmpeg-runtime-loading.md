# ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载

- 状态：Accepted
- 日期：2026-08-12

## 背景

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

## 决定

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

## 后果

- 视频画面经 boundary 的软件帧发布进入既有帧存储与呈现管线（ADR-0019 的回读
  与 golden 语义不变），音频 PCM 混入既有音频输出;两条路径都不新增平台 API
  调用点。
- CI 与开发机不装 FFmpeg 也能验证全部行为逻辑；真实解码质量依赖本地放置的
  共享库,属于发行装配问题而不是构建问题。
- FFmpeg ABI 随大版本变化,加载器按平台常见的库名/版本号列表探测；不在探测
  列表内的版本表现为“不可用”并走回退,不做半兼容。
