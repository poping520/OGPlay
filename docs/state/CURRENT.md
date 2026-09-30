# 当前状态

更新：2026-09-30。

## 运行状态

- **Dead Trigger 1.1.0**：无 Profile 的 APK/OBB 启动已越过 [VFS-04](../tasks/vfs/VFS-04.md)
  挂载、`Resources.getAssets()`、[DVM-190](../tasks/dexvm/DVM-190.md)
  `getReceiverInfo`、[DVM-191](../tasks/dexvm/DVM-191.md) `getServiceInfo`、
  [DVM-192](../tasks/dexvm/DVM-192.md) `getPermissionInfo` 与
  [DVM-193](../tasks/dexvm/DVM-193.md) `queryBroadcastReceivers` 及
  [DVM-194](../tasks/dexvm/DVM-194.md) SQLite 系统配置资源读取，以及
  [DVM-195](../tasks/dexvm/DVM-195.md) NativeActivity 类链接与 Activity 切换。
  原版 Camera$PreviewCallback、XML Pull 工厂/完整接口与 KXml 已纳入 BootDex。
  XML DEX 闭包核对、build/check、双解释器定向 2 用例/882 断言通过；实际 APK 的
  settings.xml 经 AssetManager/InputStream 正确解析 BOM、gles_mode=2、useObb=True，
  畸形 XML 抛原版异常，原有 AXML 调用及未支持方法记账失败受检。
  [SBX-15](../tasks/sandbox/SBX-15.md) 已支持匿名 MAP_FIXED 原子替换；22 用例/165 断言通过。
  无 Profile、关闭 survey 的同路径启动实测 `0x6112c000/0x1ef54/flags=0x32` 返回原地址，
  libmono.so 段扩展错误消失。[BND-40](../tasks/boundary/BND-40.md) 已将 dlopen 普通句柄
  改为 API19 ARM32 soinfo 视图；5 用例/148 断言通过。实跑首个依赖 liblog.so 返回
  `0x79000000`，原 `0xb5` 故障消失，Mono 与 Unity 的 JNI 加载完成。已修复 GLSurfaceView
  父类及 View 初始化，并将残留 NativeActivity 资源释放提前至 JNI 解绑前；此前退出 134
  是清理异常掩盖 getHolder 解析首错。定向 11 用例/381 断言通过。现已补齐 GLSurfaceView
  set/getPreserveEGLContextOnPause 的逐实例配置（默认 false、方法可覆盖），5 用例/182 断言通过；
  [DVM-196](../tasks/dexvm/DVM-196.md) 已发布 BootDex InputDevice/MotionRange 与进程输入目录查询；
  BootDex build/check、定向 11 用例/445 断言通过。原路径完成启动并进入首次 nativeRender，
  退出 1，下一首错为 `GLES1 draw requires GL_VERTEX_ARRAY`。
  证据为 `.local/input-device-{boot-build,boot-check,build,tests,startup}.log`。
  [DVM-197](../tasks/dexvm/DVM-197.md) 已补 intrinsic renderer EGL policy 消费、真实 current/swap，
  并将首次 queueEvent 提前至 Surface 回调前。定向 6 用例/414 断言通过。
  [VFS-05](../tasks/vfs/VFS-05.md) 已按 guest 页账本提供 `/proc/self/maps` 的只读打开快照；
  Windows 构建、定向 56 用例/351 断言通过。原 APK/OBB 无 Profile/无 survey、临时沙盒加
  3 帧上限复跑，`Mono requires /proc to be mounted` 消失；当时首错为
  `Cannot set SIG_SUSPEND handler`，随后 SIGABRT（134），CLI 返回 1。
  [BND-41](../tasks/boundary/BND-41.md) 已补 ARM 进程内信号注册、投递及暂停/恢复链路；
  Windows 构建与定向 68 用例/573 断言通过。同一路径已越过该注册错误，当前停止在 Dynarmic `Should raise coproc exception here`
  断言。证据 `.local/signal-startup.log`；实际 GC 全周期尚未验收。
  [BND-42](../tasks/boundary/BND-42.md) 第一阶段已统一可配置 CPU 核数与标称频率查询，
  CLI/单游戏设置接入；Windows 构建及定向 26 用例/514 断言通过。4 核/1500 MHz 原路径
  复跑后 `/proc/cpuinfo` 的 ENOENT 消失，仍停在上述 coprocessor 断言；GC 直接读取
  `/proc/stat` 的警告保留。证据 `.local/cpu-facts-startup.log`，不代表游戏兼容验收。
  [BND-43](../tasks/boundary/BND-43.md) 第二阶段已接入默认关闭的原生 CPU 并发与总速率
  上限，CLI/单游戏设置可用；Windows 构建及定向 47 用例/683 断言通过。开启 2 核并发和
  20 百万 tick/秒复跑仍到达同一 coprocessor 断言；证据 `.local/cpu-execution-startup-verified.log`。
  [BND-44](../tasks/boundary/BND-44.md) 已补旧式 CP15 屏障及通用协处理器受控能力缺口出口，
  保留故障 PC/Thumb IT；Windows 构建及定向 31 用例/476 断言通过。用户原 APK/OBB 命令
  复跑已越过 coprocessor 断言，下一首错 `pthread_kill failed` 后 guest SIGABRT/code=134，
  CLI 以 1 退出；证据 `.local/coprocessor-fix-{build,tests,startup,startup-check}.log`。
  [BND-41](../tasks/boundary/BND-41.md) 已补 ARM tkill(238)，共用现有信号投递；
  Windows 构建与定向 17 用例/300 断言通过，含真实 API19 libc errno 与双后端暂停/恢复。
  原 APK/OBB 复跑中 `pthread_kill failed` 及随后 SIGABRT 消失；下一首错为 guest
  PC=0 执行未映射内存（LR=0x614220cc）。同时修正低 PC 诊断下溢，避免掩盖原故障。
  证据 `.local/tkill-fix-{build,tests,startup,startup-check}.log`；实际 GC 全周期尚未验收。
  [BND-45](../tasks/boundary/BND-45.md) 已接通线程 Looper 与 Java/native 关联、VFS pipe
  就绪轮询及生命周期；Windows 构建与定向 14 用例/391 断言通过。原命令复跑 `ALooper_forThread` 缺失和 PC=0 崩溃消失，进入
  `UnityPlayer.onDrawFrame`，下一首错为 `javax.crypto.SecretKeyFactory` 类缺失
  （请求 `PBEWITHSHAAND256BITAES-CBC-BC`）。证据 `.local/looper-fix-startup.log`。
  NDK callback/非 pipe fd 仍不支持，不代表游戏完整验收。
  [DVM-199](../tasks/dexvm/DVM-199.md) 已补 SecretKey 公开类型与 PKCS12 SHA-1/AES-256
  SecretKeyFactory；BootDex build/check、r25c JNI/Windows 构建和双解释器密码/BKS
  回归 3312 断言通过。原命令越过类缺失并重达 `nativeRender`，下一首错为
  `unbound JNI guest slot: FromReflectedField`。证据 `.local/skf-startup.log`；不代表游戏兼容验收。
  [DVM-200](../tasks/dexvm/DVM-200.md) 已补四个 JNI 反射转换槽；Windows 构建和定向
  5 用例/342 断言通过。原命令越过该首错，进入 `Loader.SetState DataAvailable` 后仍停留
  在加载阶段；关闭请求未结束进程，有限观察后停止测试。证据 `.local/jni-reflect-startup.log`，
  已修复加载停滞：native-only JNI detach 不再要求不存在的 VM monitor token；clone 宿主
  异常立即保存首错、退出线程组并中断等待，native 调用优先传播原异常。
  Windows Release 构建及定向 14 用例/350 断言通过；原 APK/OBB 命令越过卡点，推进至
  第 6 帧后报告 `java/util/zip/CRC32` 缺失并以 1 退出，未再永久等待。
  证据 `.local/stall-fix-{build,tests,monitor-tests,startup}.log`；不代表游戏可玩验收，
  运行中主动关闭及完整 native-attached Java 执行上下文支持仍未验收。
  [DVM-201](../tasks/dexvm/DVM-201.md) 已以原版 BootDex CRC32 + guest JNI/libz 补齐校验能力；
  BootDex build/check、载荷校验、NDK r25c 重复构建及 macOS 双解释器 292 断言通过。
  原 APK/OBB 无 Profile/无 survey、临时沙盒复跑越过 CRC32 缺失，第 6 帧退出 1；
  下一阻塞是 `Class.forName(com.prime31.EtceteraPlugin)` 抛异常后 nativeRender 在 pending
  exception 下调用 `NewGlobalRef`，清理期 `PushLocalFrame` 次生错误仍在。
  证据 `.local/crc32-{tests,startup}.log`，不代表游戏可玩验收。
  已补入原版 TextToSpeech 两个回调接口，并按 API19 放行 pending 下 PushLocalFrame；
  JNI 诊断增加有界 cause 链。BootDex build/check、载荷校验、macOS Release 构建及定向
  3 用例/68 断言通过，覆盖双解释器接口分派、缺失依赖 cause、循环原因与异常身份保留。
  原 APK/OBB 无 Profile/无 survey、临时沙盒复跑已越过上述 ClassNotFoundException/NewGlobalRef；
  下一首错为 ReflectionHelper.getMethodID 调用 getDeclaredMethods，解析插件 access$4
  返回类型时缺少 `android.speech.tts.TextToSpeech`。本轮只提供回调接口，未实现语音服务。
  证据 `.local/tts-{boot-build,boot-check,payload,build,tests,startup}.log`；仍为 reached-fault。
  随后纳入原版 TextToSpeech 类，补充 synthetic 方法返回类型的完整枚举回归；未实现语音服务。
  BootDex build/check、载荷校验、Release 测试目标构建及双解释器 1 用例/62 断言通过。
  相同 APK/OBB、无 Profile/无 survey、临时沙盒复跑已越过上述返回类型缺失，推进到 f=9；
  后续出现 IABPlugin.init 的 `only an action-only Intent is supported`，停止生命周期时
  因未捕获 VmJavaThrow 退出（134），两者因果尚未定位。
  证据 `.local/tts-type-{boot-build,boot-check,payload,build,tests,startup}.log`
  与 `.local/tts-type-startup-check.json`；未完成 120 帧或游戏兼容验收。
  LLDB 后续确认因果：IABPlugin 显式绑定 APK 内 BillingService 被 action-only 限制拒绝；
  原 UnsupportedOperationException 留在 JNI pending，nativeRender 的 GetObjectClass 被门禁拒绝。
  停止时 ReleaseGuestNativeResources 又将同一 throwable 抛成 VmJavaThrow；直接 native
  Interpreter::Call 未转换它，Stop/CLI 仅捕获 std::exception，最终 terminate，掩盖运行首错。
  证据 `.local/tts-crash-{lldb,throw,exceptions}.log`；本轮只定位，尚未修改上述路径。
  [DVM-202](../tasks/dexvm/DVM-202.md) 已修复直接 native Java 异常转换及清理失败出口，
  补齐同包同进程显式 Service 绑定。macOS Release 构建、8 用例/7106 断言通过；
  原 APK/OBB 无 Profile/无 survey、临时沙盒完成 120 presented frames，退出码 0。
  本地 onBind/onServiceConnected/onDestroy 均实际执行，外部商店缺席返回支付不可用；
  原 Intent 限制、pending GetObjectClass 和退出 134 均消失，本轮无新致命首错。
  证据 `.local/service-fix-{tests,startup}.log` 与 `.local/service-fix-startup-check.json`。
  仅为启动冒烟，不代表游戏操作、完整画面/音频或 title gate 验收。
  [DVM-203](../tasks/dexvm/DVM-203.md) 已将 intrinsic GLSurfaceView renderer 迁至独立
  Java/host GLThread，并统一 Java EGL/GLES 与 JNI 的 process TID；原点击被 NativeActivity
  worker 丢弃的问题已越过。Windows 构建、定向 10 用例/1113 断言通过；原 APK/OBB
  隔离存档复跑，第 88 帧注入点击，第 89 帧由 guest thread 16385 / context 3 的
  nativeRender 调用缺失的 `AInputEvent_getSource`，退出 1。NDK 事件查询接口仍待补齐，
  不代表按钮可用。证据 `.local/glthread-{build-final,tests-final,startup}.log`
  与 `.local/glthread-startup-check.json`。
  此前 renderer 证据 `.local/renderer-{build,test-build,tests,startup}.log`。
  EGL 暂停/恢复策略尚未接入该配置；前置 NDK 符号缺失仍待闭合。
  本轮证据为 `.local/gl-preserve-{build,tests,startup}.log`。
  本次证据为 `.local/dt-lifecycle-{build,tests,startup}.log`。此前证据为 `.local/bnd40-build.log`、
  `.local/bnd40-tests.log`、`.local/bnd40-dt-startup.log` 与 `.local/bnd40-startup-check.log`。
  全 BootDex 类链接遍历完成，但仍因既有 MediaPlayer overlay 检查与过期类数断言（1729）
  整体失败。证据为 `.local/xmlpull-*.log` 与 `.local/xmlpull-dependencies.json`；
  游戏仍只是 reached-fault，未通过验收。
- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 presented frames，
  `View.setScrollBarStyle/getScrollBarStyle` 原方法解析错未再出现，未触发新的致命首错。
  滚动条绘制和完整游戏兼容仍未验收。此前 SQLite/guest ICU、EventLog、
  `AES/CBC/ZeroBytePadding` 及 `SetIntrinsicStaticRef` 首错均已越过；证据见
  [DVM-186](../tasks/dexvm/DVM-186.md) 与相关任务单。

## 已交付范围

- **BootDex 构建**：[DVM-198](../tasks/dexvm/DVM-198.md) 从最终 DEX 自动提取 SQLite/窗口
  资源 ID，映射随 JAR 发布并按 context 加载；换 ROM 不再手改 ID 或重编译宿主。
  当前 ROM build/check、载荷校验及定向 5 用例/319 断言通过；Windows 构建临时屏蔽既有
  C4996/C4834 警告。两组 ID 的合成验证不等于多 ROM 完整兼容或游戏验收。

- **Android/DexVM**：受审 API 19 BootDex 与 intrinsic 提供游戏直接调用的能力；
  PackageManager 查询仅覆盖当前 APK。
  receiver 声明、启用状态、独立元数据与逐过滤器事实经 Manifest→session→DexVM 传递；
  `queryBroadcastReceivers` 支持 flags=0、无 data/type 的有界 action/category 查询，
  返回真实 BootDex 列表并按 API 19 去重/排序；
  service 同样传递查询字段与自身元数据。`getReceiverInfo/getServiceInfo` 支持
  0、GET_META_DATA、GET_DISABLED_COMPONENTS。VM 状态由字段/数组
  与统一对象模型持有，文件 IO 经 Libcore Posix 进入 VFS。
  `getPermissionInfo` 只查当前 APK 的权限定义，定义与请求/授权集合分离。
- **VFS**：[VFS-01..04](../design/vfs/README.md) 的资源 backing、定位 IO、预算、
  APK/OBB range、媒体 lease、安装实例沙盒和无 Profile external/OBB 挂载已接入。
  受影响目标及定向回归通过；多实例选择要求显式 `--installation-id`。
- **Windows GUI/Dashboard**：[GUI-1..8](../tasks/gui/README.md) 与
  [DASH-01..04](../tasks/gui/README.md) 的库、导入、设置、移除、只读诊断和独立窗口
  已完成定向验证。[GUI-7](../tasks/gui/GUI-7.md) 机型预设仍是纯数据，未接入运行时。
- **音频**：[DVM-189](../tasks/dexvm/DVM-189.md) 的 worker、MediaPlayer、增量解码和
  AudioTrack 通知修复完成定向验证；Angry Birds 首界面背景音乐已获用户确认。
- **WebView**：[DVM-187](../tasks/dexvm/DVM-187.md) 的视图/设置基础行为与
  [DVM-188](../tasks/dexvm/DVM-188.md) 的默认禁用网页策略已完成定向验证。

## 未闭合边界

- Dead Trigger 已完成无 Profile 的 120 呈现帧启动冒烟并正常退出；本次无新致命首错，
  完整画面、交互及游戏兼容验收仍未完成。独立 GLThread 与输入接管已修正；点击路径
  当前首错为缺失的 `AInputEvent_getSource`。同进程 Service 绑定与失败清理链已修复。
  `getDeclaredMethods` 解析 TextToSpeech 返回类型缺失已越过；此前插件回调接口缺失
  导致的 ClassNotFoundException/NewGlobalRef、SecretKeyFactory、CRC32 缺失，
  Looper 缺导出导致的 PC=0、Mono 信号注册、tkill SIGABRT 和 coprocessor 断言已越过。首次 nativeRender 的
  `GLES1 draw requires GL_VERTEX_ARRAY` 尚未重达复验；
  不运行 Binder、
  system_server、外部包数据库、广播投递或 Play 服务。
- GUI 的真实 APK 导入→设置→启动→Dashboard→退出→移除全链路验收按用户安排延后；
  GUI 的 Linux/macOS 宿主未完成。WebView 页面/JavaScript 执行仍不支持。
- [DVM-189](../tasks/dexvm/DVM-189.md) 的 raw-resource 音频完整链、手动步进写入闭环、
  游戏音频验收未完成；本机缺 FFmpeg 7 DLL，custom AVIO 真实解码未验收。
- TLS-03、完整 mmap/lock、系统 CA、Java 长尾及完整 Android framework 不在已验收范围。
  `architecture.dexvm_intrinsic_layout` 仍因既有媒体源码未列入检查清单而失败，
  详见 [DVM-190](../tasks/dexvm/DVM-190.md)。
