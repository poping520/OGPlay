# DVM-91 · ParcelFileDescriptor 与媒体数据源区间能力栈

## 目标（一句话）

以“逻辑字节源身份 + 受检区间”系统性闭合 API 19 FileDescriptor/PFD/AFD 到
MediaPlayer/mixer/MP3 解码链，不向 guest 暴露宿主 fd。

## 依赖与边界

- 前置为 DVM-84 追加的 MediaPlayer lifecycle 窄方法与 PFD 记录式空壳；本 WU 用真实
  逻辑描述符替换空壳。
- core `java.io` 只依赖 per-VM `IoRuntime` 和注入的 `IoFileSystem`；APK/VFS 读取发生在
  integration/frontend。guest native fd 数值空间、宿主句柄均不进入 Java FD 对象。
- 不做 pipe/socketpair、dup/fcntl 数值互操作、AFD Parcelable、网络 Uri 或宿主 ffmpeg。

## 分层交付

### L1 · FileDescriptor

- core catalog 新增 `Ljava/io/FileDescriptor;` 与 `valid()`；`IoRuntime` 保存
  `{vfs_path|apk_entry, source, base_offset, closed}` 并随 owner GC sweep。
- `FileInputStream.getFD()` 返回独立于流 cursor 的逻辑路径描述符；这是与真机共享内核
  file offset 的明确偏差，reached-path 只把 FD 作为数据源 identity 传递。

### L2 · PFD/AFD

- `ParcelFileDescriptor.open(File,MODE_READ_ONLY)` 经 VFS `Stat` 校验真实文件，缺失抛
  `FileNotFoundException`，非法/未支持 mode 明确抛 `IllegalArgumentException`；close 幂等。
- `AssetFileDescriptor(PFD,J,J)`、`getFileDescriptor/getStartOffset/getLength/close` 保留
  API 19 字段语义。
- `AssetManager.openFd` 仅接受 STORED 条目，DEFLATED 抛 `FileNotFoundException`；FD
  绑定 APK entry，startOffset 为受检 ZIP local header 后的 payload 偏移。

### L4 · MP3

- 固定 vendor `minimp3` commit `ea99364f61c14656440e8d77e9c233ccf3124633`
  （CC0，原始文件与 SHA-256 见 `third_party/minimp3/README.md`）。
- 薄适配器接受最大 64 MiB 内存输入，跳过 ID3/非帧前缀，要求全流采样率/声道稳定，
  只发布 mono/stereo PCM16，最大解码 128 MiB。

### L3 · 消费端与 mixer

- `EncodedAudioSource` 统一 resid、APK entry、VFS path 与纯字节 `(offset,length)`；原 resid
  API 保留包装入口，SoundPool/AudioTrack/MediaPlayer 行为不变。
- `MediaPlayer.setDataSource(FileDescriptor)` 与三参重载解析逻辑 FD；APK 的 AOSP 绝对
  payload offset 在边界转换为 entry 内相对 offset。`prepare()` 真实取字节并解码，失败抛
  `IOException`；`start/pause/stop/release/volume/reset` 驱动同一 mixer voice。
- mixer 按 `OggS` 选择 Vorbis，其余进入 MP3 受检解码；区间切窗只处理字节，不解析格式。

## 验证结果

- Windows Debug/Release 全目标构建通过，包含 `ogplay`、`ogplay-gui` 与 `ogplay_tests`。
- FD/AFD/openFd/区间 MediaPlayer、MP3、SoundPool/AudioTrack/legacy/OpenSL 定向 16/16
  通过；core/Android catalog 2/2、architecture 6/6 通过。architecture 初跑唯一失败是修改前
  `CURRENT.md` 9659-byte 超限，收口时重写后已复验通过。
- 无 survey 的 PVZ 分层实跑：L1+L2 从 AFD ctor 阻断推进到
  `MediaPlayer.setDataSource(FileDescriptor,J,J)`；L4 独立接入后停止点不变；L3 后越过
  `LoadTask::FINISHED`，从 f≈6780 持续到 f=14081/presented=3745，无 guest fault，随后
  人工停止。`prepare()` 只有真实切窗和 MP3 解码成功才返回，`start()` 已建立循环 voice，
  SDL audio output 已启动；链路验收成立。
- 人工停止后 lifecycle 输出 `App Suspend` 但未在 10 秒内自行退出，本次按精确 PID
  终止；这是独立 teardown 观察，仅记录，不扩展本 WU。

状态：已完成，未提交。
