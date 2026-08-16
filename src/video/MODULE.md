# 模块：video

## 职责

为 guest VideoView/MediaPlayer 语义提供拉模型的视频解码播放接口
（画面 RGBA、音频 S16 PCM、seek），以及测试用的确定性合成后端。
真实解码由运行时加载的 FFmpeg 后端提供（ADR-0021）。

## 公共 API

- `VideoMetadata` / `ValidateVideoMetadata`：宽高、时长、音频采样率与声道的
  有界事实；越界在构造播放器前明确失败。
- `VideoPlayer`：拉模型接口。`TakeFrame(position_ms)` 返回该位置应显示且尚未
  交付的最新帧（无新帧返回空）；`ReadPcm` 从内部音频游标填充交错 S16 并返回
  帧数；`SeekTo` 同步移动画面与音频游标。完成是调用方事实：
  `position >= duration_ms`。
- `VideoPlayerFactory`：按宿主路径打开播放器，失败抛带原因的
  `VideoPlayerError`。
- `FakeVideoPlayer`：固定帧率、纯色帧（颜色由帧号确定，`FrameColorRgba` 可
  预测）、斜坡 PCM 的合成后端；无 I/O、无线程，供行为测试与端到端断言使用。
- `FfmpegAvailable` / `FfmpegUnavailableReason` / `OpenFfmpegVideo` /
  `MakeFfmpegVideoPlayerFactory`：运行时加载 FFmpeg 7 的真实解码后端。
  探测结果进程内稳定；不可用时打开抛带原因的 `VideoPlayerError`。共享库按
  `OGPLAY_FFMPEG_DIR` → 可执行文件目录 → 系统默认路径的顺序整组探测
  （Windows `avutil-59.dll` 等五个；Linux `libavutil.so.59` 等；macOS
  `libavutil.59.dylib` 等），版本主号不匹配即不可用。

## 不变量

- 位置时钟归调用方所有（统一 Clock）；实现绝不回调调用方，内部线程不得外泄。
- `TakeFrame` 的 position 单调不减，回退只能经 `SeekTo`；违反抛
  `VideoPlayerError`。
- 尺寸 1..4096、时长 1 ms..4 h、声道 0..2、采样率 0 或 4000..192000；
  声道与采样率必须同时存在或同时为零。
- FFmpeg 只镜像 7.x（avutil 59 / avcodec 61 / avformat 61 / swscale 8 /
  swresample 5）ABI 的前导字段，加载器拒绝其他主版本；五个库必须来自同一
  目录，缺一即整体不可用。
- FFmpeg 后端内部队列有界：视频待取帧 ≤ 64、PCM 缓冲 ≤ 8M 样本。音频拉取与
  视频共用 demux cursor，视频队列达到 48 帧时暂停音频侧继续 demux，保留 decoder
  headroom；receive 达 64 帧时施加背压等待 `TakeFrame` 消费，不丢帧、不伪造 EOF。
  PCM 上限及其他真实越界仍明确抛错。
- 每帧 RGBA 缓冲大小恒等于 `width * height * 4`；PCM span 长度必须是声道数
  的整数倍。

## 禁止

- 不引入编译期 FFmpeg 依赖；不直接调用平台解码 API。
- 不出现游戏名或按 title 分支。

## 测试

`tests/video/` 的接口语义与 Fake 后端确定性测试。
