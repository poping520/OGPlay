# WU-PERF-14 · 视频容量感知预读与到期帧保留

目标：修复有音轨的高分辨率视频因预读淘汰到期帧而持续保留开头黑帧。
依赖：现有 VideoPlayer 拉模型、FFmpeg 7、统一 Clock 与 WU-PERF-11 后台 wrapper。
契约：[video MODULE](../../../src/video/MODULE.md)、[ADR-0116](../../adr/media.md#adr-0116)。

范围：仅第一步缓冲策略；不改媒体时钟、公共接口、demux/codec 所有权或 RGBA 转换阶段。
64 MiB/64 帧预算不增加，按尺寸计算容量、观察 PTS 间隔调整预读，并扣除音频拉取领先；
open/seek 使用同一策略。至少容纳两张图片，否则明确失败。压力下保护最新到期帧与
最近未来帧，合并过期帧后淘汰更远未来帧；呈现停滞不截断 PCM，暂时空读仍不是 EOF。
wrapper 图片预算不等于 FFmpeg 与呈现端总内存，内部原生帧缓存与完整影音同步留待后续。

验收：1080p 30/120 fps 正常 PCM 消费时持续交付到期帧；呈现停滞时 PCM 全量保序、
当前/下一帧可取；无音轨、两帧容量、seek、EOF、阻塞解码下非阻塞拉取保持。
真实 CG 同步与 buffered 无图片消费的整段 PCM 数量和哈希必须一致；复现原游戏触发路径。

状态：第一步完成，用户确认国王村 CG 画面很快出现（2026-10-11）。
Release 视频定向 24 项/1608 断言通过；新增到期帧保留回归在旧实现明确失败。
真实 CG 同步与 buffered 全程不取画面的 PCM 均为 3706880 frames，FNV1a 均为
5407212499923180033；初始帧与下一帧仍可取，FFmpeg 内部背压未截断音频。
原 APK/外部数据、沙盒副本、无 Profile/survey：点击国王村后约 2 秒切换视频，
约 4 秒首次非黑画面，符合约 2 秒原片黑场；后续持续更新，guestFault=null。
MCP shutdown 正常退出 0，listener 已退出。完整影音同步及跨平台/title gate 未验收。
证据：`.local/wb-cg-buffer-fix-20261011/`；初始原因分析
`.local/wb-cg-black-analysis-20261011/analysis.md`。仅 reached-fault，不是 title gate。
