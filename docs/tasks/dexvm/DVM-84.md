# DVM-84 · API 19 AudioTrack 与统一 PCM backend

## 目标（一句话）

在 Android intrinsic catalog 发布 API 19 `AudioFormat`/`AudioTrack`，并让 DEX、旧
Java/JNI 与 OpenSL ES 音频入口共用进程唯一 `OpenSlesPcmMixer`。追加：补齐
`MediaPlayer` 的 `setOnErrorListener`/`setOnPreparedListener`/`reset()` 窄方法面，
移除 pvz 主菜单音效路径的方法解析阻断。

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

## 追加：MediaPlayer listener/lifecycle 窄方法

- 症状：pvz 到达 `LoadTask::FINISHED` 后，`LoaderThread.audioPlay` 首行
  `MediaPlayer.setOnErrorListener(this)` 以 "method cannot be resolved" 终止；同一
  路径每次播放先调 `reset()`。接口类 `OnErrorListener`/`OnPreparedListener` 已在
  catalog，缺的是 `MediaPlayer` 实例方法。
- 修复（对照 AOSP API 19 `MediaPlayer.java:1369/2359/2608`，均为 `public void`）：
  `setOnErrorListener`/`setOnPreparedListener` 按 `setOnCompletionListener` 同款
  接受即返回、回调保持记录 gap（offline mixer 无错误面、prepare 同步完成）；
  `reset()` 停止 mixer 播放并清 playing 标志，保留 `create()` 的 resid 绑定供后续
  `start()` 重放。
- 明确不做：FD 系数据源管道（`ParcelFileDescriptor.open`/`FileInputStream.getFD`/
  `AssetFileDescriptor`/`setDataSource(FileDescriptor,J,J)`）与 listener 回调触发，
  均为后续独立缺口。
- 回归：`tests/dexvm/videoview_tests.cpp` 新增
  `MediaPlayer accepts error and prepared listeners and resets`（方法解析即契约，
  旧代码下 `FindVtableIndex` 失败即测试失败）；音频/media 定向 29 用例 378
  assertions 无回归。pvz Release 实跑越过 `setOnErrorListener` 解析点，推进至
  `f≈7101`，下一阻断确认为 `Landroid/os/ParcelFileDescriptor;` 类缺失（上述
  FD 管道缺口的首块）。

## 验收与结果

- STREAM byte write 可从 OpenSL mixer 取得真实 PCM，左右音量生效，播放头推进且 stop 复位。
- STATIC short write 从 `STATE_NO_STATIC_DATA` 迁移到 initialized；GC 清理对应 player。
- 旧 Java/JNI handler 的 write/play 也进入同一 backend 类型并产生真实混音。
- Windows `windows-msvc` configure 与 Release `ogplay_tests` 构建通过；DVM-84、legacy
  AudioTrack、OpenSL mixer 定向回归 6/6（118 assertions）、OpenSL boundary/callback
  回归 2/2（98 assertions）及 architecture 6/6 通过；全量 CTest 按计划未运行。

状态：已完成。
