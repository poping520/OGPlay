# 模块：session

## 职责

编排 APK 精确识别、Title Profile v2、VFS、DexVM Activity 生命周期、存档、输入、
统一 Clock 和运行检查点。

## 公共 API

- `LoadTitleProfileText` / `LoadTitleProfile`：把 v2 纯数据 TOML 严格转换为
  identity/runtime/data/audio/quirks/input 强类型模型；唯一运行模板是
  `dex_activity`。
- `TitleProfileCatalog::LoadDirectory/Match`：稳定加载目录，并且只在 package、
  versionCode、`.so` SHA-256 与 ABI 全部命中时返回唯一 Profile。
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
- `DexActivityLifecycle`：实例化入口 Activity，解释执行 onCreate/onStart/onResume、
  renderer surface/frame、输入、suspend/resume 与 surfaceDestroyed/onStop/onDestroy；
  未捕获 Java 异常携带解释器栈失败。每帧同时泵 VideoView，并按 managed view 命中规则
  分发触摸与 click。pause 在 guest `onPause` 后调用持久状态 flush 回调；clean stop
  在线程停止后、guest finalizer 前再次调用，失败向上层传播。
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

- Title Profile 只接受 schema 2 和 `dex_activity`；未知 root/runtime 字段在加载期失败。
- 游戏身份信息只有 Title Profile 一个来源；`src/` 不出现标题、厂商或包名分支。
- Profile 文件为 UTF-8 纯数据且不超过 200 行；未知字段、路径逃逸、非法或歧义身份失败。
- runtime 单次 guest call 预算省略时保持受检默认值，只接受 1..100 亿；DexVM heap、
  frame 与 tick 预算均有上限。
- `runtime.entry` / `runtime.presets` 只有在 required data manifest 非空时才允许，避免把
  未提供数据误报为已安装。
- static preset 必须在 class 初始化完成后写入，并保留字段真实类型；失败不得发布部分
  启动成功。preset 类型白名单先于值解码校验，未知引用类型不能被值类型错误掩盖。
- identity ABI 只接受 `armeabi` 与 `armeabi-v7a`；APK 匹配不得猜 main library。
- VFS 输入必须按 guest 根与 source 双重命中；额外输入和 required 文件缺失明确失败。
- SoundPool pattern 只接受一个 `{resource}` 或 `{resource:0N}`（N 为 1..9）。
- enabled quirk 必须有注册定义和可定位测试；源码/CI 在打包前验证测试文件与用例存在，
  packaged runtime 至少严格保留其引用形状；未注入注册表时不得进入匹配目录。
- 生命周期清理顺序固定，状态推进由固定帧步进驱动，不依赖 sleep。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。
- 不恢复 Profile 声明的 JNI 调用序列或 Java handler 映射。

## 测试

`tests/session/` 覆盖 v2 schema、入口/预置、精确身份、VFS、生命周期、输入与确定性；
`tools/validate_title_profiles.py` 提供独立目录门禁。
