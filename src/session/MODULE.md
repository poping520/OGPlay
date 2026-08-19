# 模块：session

## 职责

编排 APK compatibility Profile、VFS、DexVM Activity 生命周期、存档、输入、
统一 Clock 和运行检查点。

## 公共 API

- `AndroidAppProcess::Create/StartApplication/StartLauncherActivity/Stop`：拥有 Manifest、
  native inventory/ABI、rootless guest process、动态 loader、DexVM、Application root 与
  Activity lifecycle；Create 只准备 API 19 system ELF，不预载 APK `.so`。Manifest launcher
  是默认入口，显式 compatibility entry 才覆盖；无 native inventory 保持 Java-capable
  process mode。
- `LoadTitleProfileText` / `LoadTitleProfile`：严格读取 legacy v1/v2 与 optional v3
  纯数据 TOML；v1 仅适配仍有效的 compatibility 字段，不恢复 native-call/Java replay，
  v3 只要求 package/api_level，版本和 `.so` hash 是可选 applicability guard。
- `SelectApkCompatibilityProfile`：按 package 与可选 version/hash guard 选择 override；
  无 Profile、旧 hash 不命中和纯 Java APK 都返回成功的 generic selection。ABI 只在
  v1/v2 旧 applicability 中校验，不覆盖 APK process ABI resolver。
- `TitleProfileCatalog::LoadDirectory/Match`：稳定加载目录；`Match` 仅保留为 v1/v2
  package/versionCode/SHA-256 exact adapter，新启动路径使用 optional selector。
- `MatchApkTitleProfile` / `PrepareApkProfileLaunch`：以 binary Manifest 和全部 APK ARM
  native library 事实选择唯一根库，再交给统一 Bionic 依赖闭包规划；无匹配返回空，
  闭包错误不发布部分计划。
- `SummarizeApkProfileMatch` / `FindApkProfileSummary`：向上层只读发布稳定 profile
  id/name 与“是否声明 required external mount”布尔事实，调用方无需解析或遍历
  Profile 数据结构；后者供缓存 id 刷新状态。
- `ResolveProfileLaunchDescriptor`：`runtime.entry.launch_activity` 存在时优先使用，
  否则使用 Manifest launcher；两者均不存在时明确失败。
- `ApplyProfileStaticPresets`：逐项初始化真实 DEX class（包括 `<clinit>`），再按声明的
  primitive/String 类型写真实 static field；类、字段、类型、范围或初始化不一致即失败，
  每次写入记录 `reason`。
- `StartDexApplication` / `DexActivityLifecycle`：先按
  resolve→`<clinit>`→construct→attach base Context→虚派 `onCreate` 建立稳定的 process
  Application root，再实例化入口 Activity 并解释执行 onCreate/onStart/onResume、
  renderer surface/frame、输入、suspend/resume 与 surfaceDestroyed/onStop/onDestroy；
  未捕获 Java 异常携带解释器栈失败。每帧同时泵 VideoView，并按 managed view 命中规则
  分发触摸与 click。pause 在 guest `onPause` 后调用持久状态 flush 回调；clean stop
  在线程停止后、guest finalizer 前再次调用，失败向上层传播。
  guest EGL swap 在 intrinsic 内 publish，不由 lifecycle 再次 present。lifecycle
  仅在 guest-owned GLSurfaceView 路径注册 driver 线程，并在每帧尾推进条件 swap
  pacer；intrinsic-renderer 不安装 observer。driver 可运行时维持一帧一 swap，
  driver 停泊于 guest 阻塞原语时由执行锁 observer 放行 GLThread。停止在 shutdown/
  join guest Java 线程前唤醒 pacer。surface callback 前按通用 render-driver 事实分流：
  intrinsic renderer 保留打开线程 GL currency；guest-owned GLSurfaceView 显式释放后
  交给其 GLThread。
  Activity switch 在旧 `onDestroy` 后推进 UI content generation，清空旧 UiTree binding/
  listener，再构造新 Activity；旧 Activity UI 不得参与新一帧 draw/input。入口 Activity
  实例化时把自身句柄发布为 `task_root_activity`（进程唯一 task 的根，
  `Activity.isTaskRoot()` 的判定依据），switch 到达的 Activity 不是根。
- frontend 取回最终 present frame 后必须交 session `ComposePresentedFrame` 与 cached UI
  overlay 做整数 source-over；screenshot/window 只读取返回结果，video 不依赖 UI。
- touch DOWN 的 gesture ownership 与 click eligibility 独立：touch-only false 立即回退
  Activity，touch-only true 保持 capture；带 click listener 时未消费的 UP-inside 才 onClick，
  touch 消费则禁止 click。隐藏/删除/UP-outside 取消 click，hit-test 仍只读 UiTree frame。
  无 listener target 时先虚派发给 Activity 的 content view；只有消费 DOWN 才建立
  MOVE/UP capture，未消费事件再回退 `Activity.onTouchEvent`。Activity switch 清空两类
  capture，禁止旧 View 跨 generation 收事件。
- `AssembleProfileVfs`：把已导入数据与 Profile mount 精确配对，在全新 VFS 中挂载并
  校验 required mount、manifest 和 working directory；
  `FlushProfileVfsAtLifecycleBoundary` 是 pause/clean stop 共用的 `FlushAll` 适配点。
- `ApplyProfileInput` / `ApplyProfileAudio` / `ResolveProfileSoundPoolPath`：只消费 Profile
  的通用 input id、source/path 与资源占位符，不按标题猜测。
- `QuirkRegistry::Load/LoadPackaged/Validate`：严格加载 `data/quirks.toml` 并交叉验证
  Profile 引用；源码模式额外验证测试文件/用例存在，发行模式保留测试引用形状验证。
- `ProfileAssetBundle`：拥有已导入 VFS/audio 字节并拒绝非规范路径、大小写歧义、重复项
  和空资产。
- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：确定性会话原语。

## 不变量

- Title Profile 接受 legacy schema 1/2 与 optional schema 3，统一归一到
  `dex_activity` 架构；未知 root/runtime 字段在加载期失败。
- v1/v2 `runtime.dexvm.interpreter` 只接受 `switch`/`threaded`；省略时固定归一为
  `switch`，Profile 只表达通用 backend 选择，不承载 title 专属解释器行为。
- 游戏身份信息只有 Title Profile 一个来源；`src/` 不出现标题、厂商或包名分支。
- Profile 文件为 UTF-8 纯数据且不超过 200 行；未知字段、路径逃逸、非法或歧义身份失败。
- runtime 单次 guest call 预算省略时保持受检默认值，只接受 1..100 亿；DexVM heap、
  frame 与 tick 预算均有上限。
- `runtime.entry` / `runtime.presets` 只有在 required data manifest 非空时才允许，避免把
  未提供数据误报为已安装。
- static preset 必须在 class 初始化完成后写入，并保留字段真实类型；失败不得发布部分
  启动成功。preset 类型白名单先于值解码校验，未知引用类型不能被值类型错误掩盖。
- v1/v2 identity ABI 只接受 `armeabi` 与 `armeabi-v7a` 且仅用于旧 applicability；
  v3 禁止 ABI/root-library 字段，APK 匹配不得猜 main library。
- VFS 输入必须按 guest 根与 source 双重命中；额外输入和 required 文件缺失明确失败。
- SoundPool pattern 只接受一个 `{resource}` 或 `{resource:0N}`（N 为 1..9）。
- enabled quirk 必须有注册定义和可定位测试；源码/CI 在打包前验证测试文件与用例存在，
  packaged runtime 至少严格保留其引用形状；未注入注册表时不得进入匹配目录。
- 生命周期清理顺序固定，状态推进由固定帧步进驱动，不依赖 sleep。
- app process 状态只允许 DexVmReady→ApplicationStarted→ActivityResumed→Stopped；
  frontend 不得跳过 Application 或直接选择/初始化 APK ELF root。
- Application 初始化失败必须先清除临时 root，且不得打开 surface、初始化或构造 Activity。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。
- 不恢复 Profile 声明的 JNI 调用序列或 Java handler 映射。

## 测试

`tests/session/` 覆盖 v1 adapter、v2 exact、v3 optional、入口/预置、VFS、生命周期、
输入与确定性；
`ui_compositor_tests.cpp` 锁定透明 overlay 基线不变和局部合成 exact pixels；
`tests/dexvm/widget_click_tests.cpp` 锁定 touch/click ownership 四组合、content view
capture/fallback 及取消路径；
`tools/validate_title_profiles.py` 提供独立目录门禁。
