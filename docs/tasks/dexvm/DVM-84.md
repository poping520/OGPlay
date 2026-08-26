# DVM-84 · API 19 AudioTrack 与统一 PCM backend

## 目标（一句话）

发布 API 19 `AudioFormat`/`AudioTrack`，让 DEX、旧 Java/JNI 与 OpenSL ES 音频入口共用
进程唯一 `OpenSlesPcmMixer`，并闭合 reached-path 所需的 MediaPlayer 窄生命周期方法。

## 依赖

- DVM-80：`android_media.cpp` family TU 与 Android ownership 已收敛。
- DVM-82：primitive array、GC side-table 与 bridge 生命周期契约已建立。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase D。

## 交付范围

- 发布常用 PCM/channel 常量，以及 ctor、`getMinBufferSize`、state/play state、
  play/pause/stop/flush/release、byte/short write、volume 与 playback head。
- PCM8/PCM16、mono/stereo、STREAM/STATIC 均进入进程级唯一 PCM queue/mixer；旧
  Java/JNI AudioTrack 同步接线，不保留只计数字节的旁路。
- AudioTrack side-table 不作为 GC root；对象不可达或显式 release 时销毁 mixer player。
- `MediaPlayer.setOnErrorListener`、`setOnPreparedListener` 与 `reset()` 按 API 19 签名
  发布：listener 保留记录但不伪造回调；`reset()` 停止播放并清除 playing 状态。
- 为当时的 reached path 发布 `ParcelFileDescriptor` 记录式前置面：`open(File,I)`、
  `getFileDescriptor()`、`close()`。它不提供宿主 fd，完整逻辑 FD/PFD/AFD 与媒体区间语义
  已由 [DVM-91](DVM-91.md) 取代。

## 边界

- 不建立 Java-only mixer、第二套播放时钟、Binder/AudioFlinger 或设备策略。
- 只支持游戏所需的 STREAM_MUSIC、4–48 kHz、mono/stereo PCM8/16；其余配置明确失败。
- listener 回调触发、SystemClock/Looper/Handler 调度，以及完整媒体数据源链不属于本 WU；
  后者归 DVM-91。
- 本 WU 延续阶段授权，不受通常单 WU 10 文件与 family TU 800 行限制；按阶段要求未运行
  全量 CTest。

## 验收与结果

- STREAM byte write 可从 OpenSL mixer 取得真实 PCM，左右音量生效，播放头推进且 stop 复位。
- STATIC short write 从 `STATE_NO_STATIC_DATA` 迁移到 initialized；GC 清理对应 player。
- 旧 Java/JNI handler 的 write/play 进入同一 backend 并产生真实混音。
- MediaPlayer 三个窄方法解析与 reset 行为测试通过；PVZ Release 越过
  `setOnErrorListener`，推进到当时尚未闭合的 PFD/AFD 数据源链。
- Windows `windows-msvc` configure 与 Release `ogplay_tests` 构建通过；DVM-84、legacy
  AudioTrack、OpenSL mixer 定向 6/6（118 assertions）、OpenSL boundary/callback 2/2
  （98 assertions）及 architecture 6/6 通过。

状态：已完成；媒体描述符后续能力见 DVM-91。
