# 模块：audio

## 职责

实现 guest OpenSL ES、AudioTrack、MediaPlayer/SoundPool 语义并对接 HAL 音频/视频时钟。

## 公共 API

- `MusicPlayer::Play`：接收已解析的编码音乐字节和循环事实；具体解码与输出由后续通用
  实现提供。
- `JavaSoundPoolState`：保存 Java SoundPool 的可用/已销毁生命周期；initialize/destroy
  只在真实状态迁移时递增计数，重复调用幂等；voice 按普通 pool / big 分类并以
  resource + instance 标识，stop-all 可按类别清理且可保留一个 resource；已加载资源按
  类别 + resource 独立建账，pending load request 与 loaded 状态分离，供加载、查询、播放
  与卸载共享。
- `DecodeOggVorbis`：从有界内存输入解码 Ogg Vorbis，事务发布拥有型 mono/stereo PCM16、
  采样率与帧数；使用仓库固定的 stb_vorbis 1.22（MIT 或 public-domain 双许可）。
- `DecodeMp3`：用固定 CC0 minimp3 从有界内存输入逐帧解码，允许 ID3/非帧前缀，只接受
  全流稳定的 mono/stereo PCM16；vendor commit、hash 与 fixture 来源见
  `third_party/minimp3/README.md`。
- `EncodedAudioSource`：统一 resid、APK entry、VFS path 与纯字节区间；`revision` 区分同路径
  替换，`lease` 标识已捕获的 FD 窗口。来源读取仍由上层注入，audio 模块不解析 APK/VFS。
- `EncodedAudioDataSource`：窄拥有型接口只发布 `Size` 与带 `stop_token` 的 `ReadAt`；
  decoder 按块消费，来源寿命覆盖 prepare/playback，取消只终止当前任务。
- `EncodedMusicMixer` / `EncodedAudioStream`：音乐按 OGG/MP3/WAV bitstream 分块读取，
  不缓存整首 PCM；SoundPool 仍用短音效全量缓存。音乐最多 16 实例，所持编码窗口总量
  128 MiB、单窗口 64 MiB；每 decoder PCM 块 16 KiB，Vorbis codec arena 上限 2 MiB。
  元数据准备在实例拥有的 jthread 中进行；每实例最多一个任务，不 detach，reset/release/
  重设源先取消并 join；提交验证任务身份。MP3 准备只扫描帧头，后退 seek 重置解码器并
  从头丢弃至目标帧，保持精确 PCM；不承诺常数时间 seek 或无欠载的实时性能。
  后续块解码失败停止对应音乐，通过 runtime 投递 MediaPlayer error，不停止其他音源。
- `JavaSoundPoolMixer`：用注入的编码资源 loader 按 source 去重解码；独立 PoolId 隔离
  sample/voice，`PlaySample` 使用 AOSP left/right/priority/loop/rate，maxStreams 按
  priority 然后最旧抢占；loader 不存在时保持显式 disabled，缺失/损坏资源保留可查询失败原因。
  unload/release 在最后一个池引用消失时回收 decoded cache；autoPause/autoResume 只作用于
  receiver 所属池，且不会恢复此前手动暂停的 voice。
- `OpenSlesPcmMixer`：为 Virtual `libOpenSLES.so` 保存线程安全 PCM player/queue，支持
  mono/stereo、unsigned PCM8/signed little-endian PCM16、跨 buffer 线性重采样、
  millibel volume、mute、pan、独立左右声道 gain、无符号 32 位回绕的 playback head，
  以及多 player 64-bit 累加后一次饱和。AudioTrack STREAM/STATIC 与 OpenSL buffer
  queue 使用不同 stop/clear 语义；STATIC 数据不被播放消费。
- `DecodeWav`：解码有界 RIFF/WAVE PCM8/16 mono/stereo，拒绝压缩 WAVE。
- `DecodeEncodedAudio`：按魔数分派 OGG/WAV/MP3。
- M3/M6 定义对象表、PCM 队列、回调与媒体状态机。

## 不变量

- guest buffer 所有权和回调线程明确。
- 音频时钟接入统一 Clock；对象状态可快照。
- AudioTrack 诊断快照按 player 报告累计写入/消费帧、当前队列字节、欠载次数与欠载输出帧，
  并同时报告 periodic callback 的生成、投递和延期数量；统计不得改变混音或回调时序。
- 编码资源的来源与路径解析发生在上层；音乐播放器只接收拥有型
  `EncodedAudioDataSource`，不得在接入后再复制完整编码源。OGG/MP3 输入请求块不超过
  64 KiB，Vorbis header arena 不超过 2 MiB；WAV 用定位读取。短 SoundPool 音效才允许
  上层显式、有总量上限的 ReadAll。
- Ogg/MP3/WAV 输入最大 64 MiB、解码 PCM 最大 128 MiB，只接受 1/2 声道和正采样率；空、损坏、
  超限或不支持流必须在发布 PCM 前明确失败。WAV 容器不得当作裸 PCM。
- SoundPool resource 只有在上游完成真实加载后才能 `MarkLoaded`；未接入加载、解码和
  输出前，load/play 只能留下可查询 pending request，查询必须返回目录事实，不得发布
  loaded/playing 成功；play 只有在 loaded 后才能以 resource + instance 创建 voice。
- SoundPool 音量必须是有限的 `[0, 1]` 值；同一 voice 的重复 play 更新状态而不复制身份。
- SoundPool voice control 以类别 + resource + instance 精确寻址；pause/resume 只迁移已有
  voice，stop 删除 voice，pitch 只接受 `[0.5, 2]`，reset 记录可查询的重置事实。
- 批量 pause/resume 只迁移指定类别的现有 voice，已处于目标状态的 voice 保持幂等；
  state 与 mixer 必须在同一受检 handler 下同步提交。
- looping 是每次 play 的显式 voice 状态；mixer 到 PCM 尾部只回绕 looping voice，普通
  voice 仍自然结束，pause/resume/reset 不得丢失 loop 事实。
- SoundPool 状态必须可由不同 guest JNI 线程安全访问；destroy 同时清空所有 voice，
  与 loaded resource；initialize 不得恢复已销毁的状态。stop 只影响 voice，不得隐式卸载。
- mixer 控制和 render 共用内部锁；mono 复制到双声道，mono/stereo 通过有界线性重采样
  消费 position，跨 buffer 保留分数相位；多个 voice 以 64-bit accumulator 累加，
  仅在最终设备格式上饱和。
- OpenSL mute 只把当前 player 的输出 gain 置零，不暂停 source position、queue 消费或
  consumed-buffer callback；pause/stopped 才停止 mixer 时间推进。
- AudioTrack/MediaPlayer 保存 stream type，SoundPool 按池保存；stream gain 与实例 gain
  相乘，静音仍消费 PCM。默认 OpenSL/legacy 音源归 STREAM_MUSIC；不映射系统设备路由。
- AudioTrack stereo gain 与 OpenSL millibel/mute/pan 在同一 player 上相乘；播放头按已消费
  source frame 计数并无符号 32 位回绕。STREAM stop 复位 head 但保留队列；STATIC stop
  回到缓冲起点且保留样本；pause 仅冻结。flush 仅在非 playing 时丢弃 STREAM 队列且不改 head。
- AudioTrack MODE_STREAM 使用 mixer 的可中断 blocking enqueue：未消费字节（首 buffer 已播放
  frame 除外）与本次 write 之和不得超过构造 buffer budget；播放、clear、destroy 唤醒
  writer，process teardown 粘性中断。position callback 若同步回填 PCM，同一次 lifecycle pump
  不继续补发过期 periodic callback，但剩余跨越周期必须保留到后续安全点，避免丢回填事件；
  queue item capacity 只作内存护栏，不定义正常延迟。

## 禁止

- 不直接调用 CoreAudio 等平台 API。
- 不为某个资源编号硬编码封面音乐。

## 测试

`tests/audio/` 的 ABI、状态机和无设备离线混音测试。
