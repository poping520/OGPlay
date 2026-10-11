# WU-PERF-15 · 原生图片缓存与到期 RGBA 转换

目标：避免后台为随后被丢弃的未来图片提前生成 RGBA，同时有界计量解码像素 backing。
依赖：WU-PERF-14、FFmpeg 7 / avutil 59、现有 VideoPlayer 和唯一 decoder/AVIO worker。
契约：[video MODULE](../../../src/video/MODULE.md)、[ADR-0116](../../adr/media.md#adr-0116)。

范围：可选 DecodedVideoPlayer/DecodedVideoPicture 接口、拥有型原生图片与共享转换器；
普通后端走 RGBA adapter，按 vector capacity 计量。原生平面去重计入实际 stride/padding。
worker 只转换最新到期图片，转换不持有拉取锁，EOF 后仍服务剩余图片；seek/退出取消并
join，再清理缓存与输出。FFmpeg queue/pending 合计增加像素字节预算，背压空读不冒充 EOF。
不改变媒体时钟、Java API、UI/ANGLE 合成、VFS 来源或增加 decoder worker。

状态：第二步实现与定向验证完成，待用户复测。Release 视频定向 30 项通过，覆盖
1080p 30/120 fps、两帧容量/无音轨、RGBA reserve 计量、转换阻塞下非阻塞拉取、
转换失败退役、seek 代际和原生图片在 seek/播放器退役后的有效性。
合成 4k/60 fps H.264/AAC 制品验证字节背压先于帧数水位、恢复消费后 PCM 完整及 seek 清零。
FFmpeg 7 本地头文件核对 AVBufferRef 镜像大小/offset 通过。

真实 CG：原生图片 backing 为 3139293 bytes，RGBA 为 8294400 bytes；同一 64 MiB
缓存可容纳 21 张原生图片。整段无画面消费 PCM 仍为 3706880 frames，FNV1a 为
5407212499923180033，与第一步/同步基准一致；临时转换追踪证明未推进画面请求时
仅转换首帧，请求下一帧后才发生下一次转换。单槽输出由后续拉取异步交付。

原 APK/外部数据、原存档完整副本、无 Profile/survey：点击国王村后约 2 秒切换 CG，
约 4 秒开始显示非黑画面，后续持续更新；guestFault=null，MCP shutdown 退出 0。
早先取证启动曾因相对目录复制失败、随后合并副本造成重复 guest path 而中止，保留于
证据目录 `aborted-startup/`；有效复现使用全新完整副本 `sandbox-final/`。

证据：`.local/wb-cg-staged-fix-20261011/`。只构建 ogplay/ogplay_tests 并运行视频相关检查；
未运行全量测试。完整影音同步、FFmpeg 私有工作区总内存与跨平台/title gate 未验收。
