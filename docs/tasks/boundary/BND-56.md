# BND-56 · OpenSL PCM 播放器 AndroidConfiguration

状态：完成，macOS Release 原 APK 输出验证通过，用户实测正常；未做正式 title gate。日期：2026-11-05。

## 目标与依赖

补齐 PCM AudioPlayer 的显式 AndroidConfiguration 流类型配置，使原生音频库能完成
创建→配置→Realize→队列回填→宿主输出。依赖既有 BND-15 回调线程及 DVM-189 共用
mixer/SDL 输出；不新增依赖，不引入 AudioFlinger、系统路由、录音或游戏专属适配。

## 根因与方案

原命令在 `build/release` 启动 Island Castaway，关闭磁盘空间提示、跳过 CG 后到达菜单。
FMOD 选择 OpenSL 输出，Java FMODAudioDevice 在等待原生初始化，并未创建 AudioTrack。
旧实现拒绝必需的 `SL_IID_ANDROIDCONFIGURATION`，CreateAudioPlayer 返回 12，游戏继续运行
但没有音频播放器。宿主设置为 mute=false、volume=100，不是静音设置导致。

证据：`.local/island-audio-before-mcp.log`、`.local/island-audio-trace.log`；后者明确记录
`requested interface unavailable iid=SL_IID_ANDROIDCONFIGURATION required=true` 与
`$Engine.CreateAudioPlayer result=12`。

按以下边界实现并验收：

1. 加入该接口的 A32 双入口 vtable，保留 private callable，不扩大 public ELF surface。
2. 按 API19 `classes.c` 的 EXPLICIT_PREREALIZE 在请求时开放；其他接口的 Realize 门禁保留，
   GetInterface 失败清空输出。
3. 仅支持 `androidPlaybackStreamType`、默认 MEDIA、0..5；Set 仅 Realize 前允许，
   接入现有 mixer stream gain；Get 支持尺寸查询和不足缓冲。不忽略影响音量的配置。
4. 底层失败保留 method/result/thread/LR 和缺失 IID，防止调用方吞错后只表现为无声。
5. 复用 OpenSL 主题回归；原命令重达菜单和第一关，在 SDL 成功提交点采集 PCM 验证。

来源为本地 `.local/aosp/framework/wilhelm/` 的 `src/classes.c`、`src/itf/IObject.c`、
`src/itf/IAndroidConfiguration.c`、`src/android/AudioPlayer_to_android.cpp` 及
`include/SLES/OpenSLES_AndroidConfiguration.h`。
Get 的返回码采用底层 `android_audioPlayer_getConfig` 的尺寸协商语义；不复刻该版本
`IAndroidConfiguration_GetConfiguration` 包装器缺 break 导致写出结果后误报不支持的缺陷。
非法 stream 拒绝且不更改现有配置。

## 验证

- 仅构建 `build/release` 的 `ogplay-cli`、`ogplay_tests`。
- `ogplay_tests --test-case='OpenSL ES*,libOpenSLES Virtual SO*'`：6 用例、787 断言通过。
  复用主题用例扩充必需接口、Realize 前取得/配置、默认及全部六种流、空指针、未知 key、
  非法值、尺寸查询/不足缓冲、Realize 后拒绝修改、只读 vtable、未请求接口失败清空输出；
  MUSIC 静音不影响配置为 NOTIFICATION 的 PCM，而静音 NOTIFICATION 后仍消费并回调。
  日志：`.local/island-audio-final-build.log`、`.local/island-audio-final-tests.log`。
- 原命令加 `--mode diagnostic` 在 macOS 运行，关闭既有磁盘提示、跳过 CG、PLAY 到第一关。
  未再出现 OpenSL 初始化失败，运行期 `guest_fault=null`。
- 使用 `.local/island-audio-probe.c` 构建临时 dyld interposer，只复制
  `SDL_PutAudioStreamData` **成功返回**的 PCM；仍使用实际 SDL 默认播放设备，不替换 mixer、
  解码或输出。临时探针及采集产物均不入库。采样格式：48 kHz / stereo / PCM16。

  | 阶段 | 长度 | peak | RMS | 非零样本 |
  | --- | --- | --- | --- | --- |
  | 菜单 | 5 秒 | 19740 | 5424.69 | 479924 / 480000 |
  | 第一关 | 5 秒 | 14899 | 1466.71 | 479752 / 480000 |
  | 音乐 0、音效 99，点击 CONTINUE | 3.029 秒 | 5612 | 206.66 | 8916 / 290816 |

  菜单及第一关均无 100 ms 全零块。单独音效检查前暂停界面连续一秒 PCM 全零，点击
  CONTINUE 后出现短音效，排除了音乐冒充音效。截图、WAV、统计分别见
  `.local/island-audio-{menu,game}.{jpg,wav,json}`、`.local/island-audio-effects.{wav,json}`；
  运行日志 `.local/island-audio-verified.log`，最终状态 `.local/island-audio-final-state.json`。
- MCP shutdown 后进程和监听退出。游戏音乐/音效设置恢复原 91/99，Launcher 的 MCP 设置恢复
  原 false。退出仍报既有 `System.runFinalizersOnExit(Z)V` 缺失，未越界修复。
- `tools/capabilities.py check`、UTF-8、文档相对链接及 `git diff --check` 通过；未跑全量测试。

用户于 2026-11-05 确认实测正常。本次不代表全关卡或跨平台验收；未接入完整
AndroidConfiguration，不关闭 DVM-189 的其他验收项。
