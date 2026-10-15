# VID-1 · 裁剪版 FFmpeg 预编译库直接链接

目标：用独立构建的裁剪版 FFmpeg 单一共享库替换运行时加载的完整 FFmpeg 7，使有包平台
默认具备 CG 解码，未编入的编码明确失败并按名称记账。
依赖：既有 VideoPlayer/DecodedVideoPlayer 拉模型、自定义 AVIO、ANGLE 预编译 SDK 做法。
契约：[video MODULE](../../../src/video/MODULE.md)、[ADR-0136](../../adr/media.md#adr-0136)。

范围：
- OGPlay 外：OGPlay-ffmpeg（构建脚本、FFmpeg n8.1.3 submodule、组件清单、冒烟样本）与
  OGPlay-ffmpeg-prebuilt（公共头、平台包、清单）。组件为 `mov` + `h264`/`mpeg4`/`aac`
  （隐含 `h263`），构建时组件集合、导出符号、系统依赖与合成样本解码均有机器检查。
- OGPlay 内：`third_party/ffmpeg-prebuilt` 浅 submodule、`FFmpeg::FFmpeg` 导入目标与
  清单校验、运行库复制；删除 `ffmpeg_loader.cpp`/`ffmpeg_api.h` 与 `OGPLAY_FFMPEG_DIR`，
  播放器改用真实 FFmpeg 8.1 API；宿主路径入口改经自定义 AVIO；无包构建使用
  `ffmpeg_disabled.cpp` 给出原因；移除 GUI 预留的 FFmpeg 目录设置。
- 行为：视频/音频编码未编入时抛 `VideoCodecUnsupportedError`，VideoView 与 MediaPlayer
  路径记账 `android.video.codec.<name>`；不再静默丢弃无法解码的音轨。无音轨时打开即
  `PcmEnded`（自定义 AVIO 路径此前不置位，buffered 会一直等待音频）。FFmpeg 自身日志
  只保留 error 级，避免每次自定义 AVIO 打开向 stderr 打印 warning。

状态：macOS arm64 完成；Windows/Linux/macOS x64 预编译包未构建，AUTO 下这些平台构建为
无视频版本。

验证：
- 预编译包 `macos-arm64`：4.1 MB，依赖仅 `libSystem`，962 个导出含 OGPlay 全部调用；
  冒烟样本 H.264 High（B 帧）、MPEG-4 moov 在尾、3GP/H.263 各 24 帧且 RGBA/seek/PCM
  通过，HEVC 报告 unsupported。
- macOS dev：FFmpeg 相关 12 个用例/140 断言通过，含 HEVC 视频、AC-3 音频未编入、
  无音轨 PcmEnded 与 VideoView 记账 `android.video.codec.hevc` 回归；运行时只加载
  构建目录中的 `libogplay_ffmpeg.dylib`。
- 真实 CG 播放由用户在 macOS 实测正常。

缺口：Windows 包需在 Windows 主机用 OGPlay-ffmpeg 构建并发布；macOS x64 需 nasm。
`buffered 1080p video delivers due pictures under normal audio consumption` 在 Debug（-O0）
下因 5 s 期限失败，HEAD 原代码同样失败、-O2 通过，与本任务无关。未运行全量测试。
