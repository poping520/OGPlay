# DVM-84 · API 19 AudioTrack 与统一 PCM backend

## 目标（一句话）

在 Android intrinsic catalog 发布 API 19 `AudioFormat`/`AudioTrack`，并让 DEX、旧
Java/JNI 与 OpenSL ES 音频入口共用进程唯一 `OpenSlesPcmMixer`。

## 依赖

- DVM-80：`android_media.cpp` family TU 与 Android ownership 已收敛。
- DVM-82：primitive array、GC side-table 与 bridge 生命周期契约已建立。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase D / AudioTrack。

## 范围

- 发布常用 PCM/channel 常量，以及 ctor、`getMinBufferSize`、state/play state、
  play/pause/stop/flush/release、byte/short write、volume 与 playback head。
- PCM8/PCM16、mono/stereo 与 STREAM/STATIC 均进入进程级唯一 PCM queue/mixer；旧
  Java/JNI AudioTrack 同步接线，不保留只计数字节的旁路。
- AudioTrack side-table 不作为 GC root；对象不可达或显式 release 时销毁 mixer player。
- 本 WU 延续阶段授权，不受通常单 WU 10 文件与 family TU 800 行限制。

## 不做

- 不建立 Java-only mixer、第二套播放时钟、Binder/AudioFlinger 或设备策略。
- 只接受 API 19 游戏所需的 STREAM_MUSIC、4–48 kHz、mono/stereo PCM8/16；其余配置
  明确返回错误或抛 Java 异常。
- 不进入 SystemClock/Looper/Handler 调度；不运行全量 CTest，全量回归留到阶段最后一个 WU。

## 验收与结果

- STREAM byte write 可从 OpenSL mixer 取得真实 PCM，左右音量生效，播放头推进且 stop 复位。
- STATIC short write 从 `STATE_NO_STATIC_DATA` 迁移到 initialized；GC 清理对应 player。
- 旧 Java/JNI handler 的 write/play 也进入同一 backend 类型并产生真实混音。
- Windows `windows-msvc` configure 与 Release `ogplay_tests` 构建通过；DVM-84、legacy
  AudioTrack、OpenSL mixer 定向回归 6/6（118 assertions）、OpenSL boundary/callback
  回归 2/2（98 assertions）及 architecture 6/6 通过；全量 CTest 按计划未运行。

状态：已完成。
