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
  帧数；`PcmEnded` 区分真实 EOF 与暂时空读，buffered 不把背压空读伪装成 EOF；
  `SeekTo` 同步移动画面与音频游标。完成是调用方事实：
  `position >= duration_ms`。
- `DecodedVideoPlayer` / `DecodedVideoPicture`：可选的分阶段接口，交付拥有型原生图片、
  PTS 与像素 backing 字节数。图片单次转换为 RGBA；decoder/图片由同一 owner 串行使用。
  FFmpeg 图片持有自己的 AVFrame buffer 引用及共享转换器，seek/播放器退役不使图片悬空。
  普通 VideoPlayer 通过拥有型 RGBA adapter 接入 buffered，不要求其他后端实现原生接口。
- `VideoDataSource` / `VideoSourcePlayerFactory`：拥有型来源只发布 `Size`/`ReadAt`；
  guest VideoView 只经该接口消费 VFS lease，不接受或反查宿主路径。
- `VideoPlayerFactory`：保留给独立宿主路径调用方的窄兼容入口；不在 guest 生产消费链中。
- `MakeBufferedVideoPlayer`：为同步后端建立内部 worker。实时拉取只消费已解数据，
  不做 I/O 或等待解码；open/seek 按图片容量预读，最多 250 ms。
  seek/析构先 join，错误由后续拉取抛出。
- `FakeVideoPlayer`：固定帧率、纯色帧（颜色由帧号确定，`FrameColorRgba` 可
  预测）、斜坡 PCM 的合成后端；无 I/O、无线程，供行为测试与端到端断言使用。
- `FfmpegAvailable` / `FfmpegUnavailableReason` / `OpenFfmpegVideo` /
  `OpenFfmpegVideoSource` / `MakeFfmpegVideoSourcePlayerFactory`：运行时加载 FFmpeg 7 的
  真实解码后端。自定义 AVIO 使用 32 KiB buffer 和来源 ReadAt/seek，不要求宿主路径。
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
- FFmpeg 后端内部队列有界：视频队列 ≤64 帧，另有一张 pending；两者像素 backing
  合计 ≤256 MiB，PCM 缓冲 ≤8M 样本。按受检软件图像平面的 AVBufferRef 去重计数，
  包含 stride/padding；实际解码尺寸也须为 1..4096。音频与视频共用 demux cursor，
  视频达到 48 帧或 192 MiB 时暂停音频侧 demux，receive 保留下一张已观察最大图片的
  空间并服从上述上限；格式变化超过预算明确失败。背压等待图片消费，不丢帧、不伪造 EOF。
  PCM 上限及其他真实越界仍明确抛错。
- buffered 后端图片队列 ≤64 帧且像素 backing ≤64 MiB，按实际最大图片字节数计算容量，
  必须至少容纳两张图片，否则打开/解码明确失败。FFmpeg 队列保留原生格式，
  仅最新到期图片由 worker 在锁外转换；待交付 RGBA 为单槽且 ≤64 MiB，转换可有一张
  临时输出。拉取不等待转换，尚未就绪时返回空、后续拉取交付。预读跨度由容量和 PTS 最小间隔计算，保留两帧余量，
  范围 10..500 ms；未知间隔按 10 ms。PCM 水位还扣除已拉取音频相对图片请求的领先量，
  最低 10 ms、最高 500 ms，最多额外一个 10 ms chunk；启动/seek 预读至跨度的一半，
  最多 250 ms。不丢 PCM，不用图片背压截断音频；压力下保留最新到期图片与最近未来图片，
  合并过期图片后才淘汰更远的未来图片。无音轨/音轨 EOF 后按同一跨度限制视频领先。
  空 PCM 也可表示临时欠载，不伪造 EOF。解码 EOF 后 worker 仍可转换剩余到期图片；
  seek/退出 join 后清理图片、待交付结果及预读统计。队列/输出预算分开计量，
  不代表 FFmpeg 私有 codec/swscale 工作区或调用方保留图片的总内存。
- custom AVIO 的来源由 player 共享拥有，关闭顺序为 format/codec → AVIO context/buffer
  → 来源；不得双重释放。seek 可回退，读取请求不得超过 32 KiB。
- 每帧 RGBA 缓冲大小恒等于 `width * height * 4`；PCM span 长度必须是声道数
  的整数倍。

## 禁止

- 不引入编译期 FFmpeg 依赖；不直接调用平台解码 API。
- 不出现游戏名或按 title 分支。

## 测试

`tests/video/` 的接口语义与 Fake 后端确定性测试。
