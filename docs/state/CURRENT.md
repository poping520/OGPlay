# 当前状态

更新：2026-10-04。

本页只保留最新运行结果与未闭合边界。能力状态见 [capabilities.toml](../../capabilities.toml)，
实现契约见各模块 MODULE；历史过程见 [任务总览](../tasks/README.md) 与
[DexVM 任务索引](../tasks/dexvm/README.md)。

## 运行状态

- **Tales From Deep Space 1.0.0**：VideoView 与 packed depth/stencil 已闭合，macOS
  800×480/1280×720 菜单仍裁剪；BND-29 终止断言失败。
  [DVM-217](../tasks/dexvm/DVM-217.md) AtomicFile/清理异常隔离回归通过。
  1920×1080 原 APK 越过缺类/pending 首错，f=4077 首次读存档 ENOENT 后遇到
  `GetArrayLength requires a valid reference`，退出 1；进程全关闭。证据 `.local/atomic-file-fix/`。
  存档交互、Windows/Linux 未验收。

- **PVZ（com.popcap.pvz_na）**：[DVM-205](../tasks/dexvm/DVM-205.md) 已修复 DEX
  注解预算误拒绝，使用共享解析缓存和分层资源预算。macOS Release 定向回归通过；
  原 APK 无 Profile/无 survey、隔离沙盒完成首帧，进入 TermsActivity 并退出 0。
  prepare 首错消失；后续帧、交互及完整游戏兼容未验收。
  证据：`.local/annotation-budget-analysis/`。

- **Dead Trigger 1.1.0**：
  - [DVM-206](../tasks/dexvm/DVM-206.md) 已补齐 BootDex KeyCharacterMap 与通用虚拟键盘
    native 查询，Java/JNI 共用 API19 映射。Release 构建、BootDex build/check、数据校验及
    双解释器 1662 断言通过。原 APK/OBB、隔离空沙盒进入第一关，W/A/Shift+D/空格
    越过缺类首错，f=3954 遇到下述 JNI 字符串构造错；默认存档 f=6 另触发
    `Dialog(Context)` 评分弹窗缺口。证据：`.local/key-character-map-fix/`。
  - macOS 第 0 帧停滞已修复：GLThread 用空回调交换领取任务，消除
    `std::function` 移动后源对象仍可调用的标准库差异。Release 构建通过；
    现有双解释器 renderer 回归旧版失败、修复后 990 断言通过。
    原 APK/OBB、默认存档、无 Profile/无 survey 推进到 120 帧；退出仍停滞，
    FMOD stop/join 等待 AudioTrack 队列写入，GLThread 已空闲并释放 VM 锁。
    取证后强制结束；本次修复未在 Windows/Linux 实跑。证据：`.local/glthread-portable-fix/`。
  - Windows 已进入第一关并持续出帧；最新隔离沙盒回放在 f=4740 遇到
    `string constructor receiver is not an unbound string instance` 后退出。
    [DVM-204](../tasks/dexvm/DVM-204.md) 的保存/重载已验证：强制结束后同一沙盒
    返回任务地图、开场剧情不再重现、加密进度哈希一致。
    证据：`.local/concurrency-audit/replay.stderr.log`、`replay-gameplay.png`，
    `.local/jni-string-{tests.log,game.stderr.log,reload.png}`。
  - 启动类/native 缺口、同进程 Service、独立 GLThread、Java/NDK 输入与混合 GLES2
    绘制已越过；随机源及 JNI 字符串长度问题已修复。cacheflush 失效与等待期间宿主
    消息泵见 [WU-PERF-08](../tasks/optimization/WU-PERF-08.md)；STREX 已改为原子 CAS，
    旧实现交错回归失败，证据 `.local/accept-stall/`；映射失效、clone 失败收敛及
    kuser ABI 见 [BND-48](../tasks/boundary/BND-48.md)。
  - 当前阻塞为上述评分弹窗/字符串构造首错与退出停滞；完整关卡、画面/音频及 title gate 未验收。

- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 呈现帧，滚动条方法解析错
  消失，未触发新致命首错。滚动条绘制及完整游戏兼容未验收；历史修复见
  [DVM-186](../tasks/dexvm/DVM-186.md)。

## 已交付范围

- **BootDex**：[DVM-198](../tasks/dexvm/DVM-198.md) 从最终 DEX 提取 SQLite/窗口
  资源 ID，映射随 JAR 发布。当前 ROM build/check、载荷校验与定向回归通过；
  多 ROM 尚未完整验收，Windows 构建曾临时屏蔽 C4996/C4834 警告。
- **Android/DexVM**：受审 API 19 BootDex 与 intrinsic 支持游戏直接调用；
  PackageManager 仅查询当前 APK，Manifest 元数据经统一链路传递，文件 IO 经 Libcore Posix/VFS。
- **VFS**：[VFS-01..04](../design/vfs/README.md) 的资源 backing、定位 IO、预算、
  APK/OBB range、媒体 lease、安装实例沙盒与无 Profile external/OBB 挂载已接入并定向验证。
  多实例选择要求显式 `--installation-id`。
- **Windows GUI/Dashboard**：[GUI/DASH](../tasks/gui/README.md) 的库、导入、设置、
  移除、只读诊断与独立窗口已定向验证；机型预设仍为纯数据，未接入运行时。
- **音频**：[DVM-189](../tasks/dexvm/DVM-189.md) 的 worker、MediaPlayer、增量解码与
  AudioTrack 通知已定向验证；Angry Birds 首界面背景音乐已获用户确认。
- **WebView**：[DVM-187](../tasks/dexvm/DVM-187.md) 的视图/设置基础行为与
  [DVM-188](../tasks/dexvm/DVM-188.md) 的默认禁用网页策略已定向验证。

## 其余未闭合边界

- GLSurfaceView EGL 暂停/恢复、context-loss 尚未闭合；NDK Looper callback/非 pipe fd
  不支持，native-attached 参数/反射长尾未验收。首次 GLES1 vertex-array 首错尚未重达复验。
- GC 全周期、普通直接写入的 ABA 历史跟踪、直接访存卸载 quiescence 与退出顺序风险未验收。
- 全 BootDex 类链接检查曾因 MediaPlayer overlay 与过期类数断言失败，尚未整体复验；
  `architecture.dexvm_intrinsic_layout` 因媒体源码未列入清单仍失败，见
  [DVM-190](../tasks/dexvm/DVM-190.md)。
- GUI 的导入→设置→启动→Dashboard→退出→移除全链路验收按用户安排延后；
  GUI 的 Linux/macOS 宿主未完成。移动接口仅预留，暂停握手与真机验收留待后续移植。
- 音频 raw-resource 完整链、手动步进写入闭环、custom AVIO 真实解码与完整游戏音频未验收。
  WebView 页面/JavaScript、TextToSpeech 语音服务未实现。
- TLS-03、完整 mmap/lock、系统 CA 与 Java 长尾未验收；不实现完整 Android framework、
  Binder/system_server、外部包数据库、广播投递或 Play 服务。
