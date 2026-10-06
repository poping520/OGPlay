# WU-PERF-11 · 实时视频供给与覆盖呈现

目标：解决实时 VideoView 解码占用主帧/音频锁、积压画面截断 PCM 及固定时钟慢放。
范围：FFmpeg 上有界后台 wrapper、显式实时 Clock 补时、全屏不透明视频覆盖时省读回、封存 STORED 媒体一次 CRC 校验。
依赖：FFmpeg 7/VFS lease、VideoView 代际、统一 Clock、UiTree、ANGLE/SDL3。
设计：[ADR-0099](../../adr/media.md#adr-0099)。不扩展系统媒体能力，不改游戏核心逻辑。

验收：阻塞解码时拉取不等待，滞后画面不截断 PCM，seek 回收旧队列，时钟补时有界且
手动步进不变；覆盖证书反例及恢复底图回归。Release 同启动路径对照两段视频，记录真实
成功 present、播放耗时及缺口。仅定向验证，不宣称完整游戏/title gate。

状态：本播放路径修复完成。Release 定向 57 项/986 断言通过，UI compositor 复验通过。
用户复测确认两段载入动画的画面和背景音乐卡顿均已解决（2026-10-06）。
原 2560×1600、默认 1 核、同 APK/Profile/MCP 路径的成功 present 采样为 Amazon
32.15 FPS、Frontier 24.51 FPS；片源为 30/24 FPS。纯方案初版 Amazon 仍约 10 FPS，
宿主采样定位到每次 AVIO 读取都全量 CRC；一次校验后消除此供给瓶颈。
两段原 APK 区间无画面消费的实时 PCM 探针全量逐样本一致，均无空读。
手动步进仍用同步后端；测试进程已关闭，退出保留既有 0x315f0001 读取故障。
完整影音同步、Windows/Linux 与 title gate 未验收。构建因既有符号转换警告在本地
关闭 warnings-as-errors，未扩大为全量测试。
证据 `.local/video-playback-20261006/`。
