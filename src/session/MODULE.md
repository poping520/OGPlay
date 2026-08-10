# 模块：session

## 职责

编排识别、Profile、VFS、生命周期、存档、输入、统一 Clock 和运行检查点。

## 公共 API

- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：M0 确定性空会话。
- `LoadTitleProfileText` / `LoadTitleProfile`：M5 将 v1 纯数据 TOML 严格转换为
  identity/runtime/native-call/data/audio/java/quirks/input 强类型模型。
- `TitleProfileCatalog::LoadDirectory/Match`：稳定加载 profile 目录，并且只在 package、
  versionCode 与 `.so` SHA-256 三项全部命中时返回 profile。
- `MatchApkTitleProfile`：把 binary Manifest 与所有 APK ARM native library 目录事实送入
  Profile 精确匹配；只有 package/version/hash/ABI 一致的唯一 library 才能成为入口候选。
- `PrepareApkProfileLaunch`：精确匹配后按 Profile API 把根库和系统库交给统一 Bionic
  依赖闭包规划，并预解析 Profile 声明的 root-library JNI native 导出地址；无匹配返回空，
  闭包或导出错误不发布部分启动计划。
- `ResolveProfileNativeCalls`：按 JNI 标准优先 short、回退 long 导出名，并保持 Profile
  调用顺序；缺失、重复或地址溢出明确失败。
- `BuildProfileNativeInvocations`：按 phase 把已解析 native target、JNIEnv、实例/jclass、
  surface 与当前 input 事实转换为 A32 JNI 参数；前四个 word 进入 r0-r3，其余以偶数
  word 的 8 字节栈帧布局输出。
- `ExecuteProfileNativeInvocations`：先完整预检一个 phase 的已装配调用批次，再按 Profile
  顺序交给统一 A32 executor；逐项保留 index/export/ticks/return，失败附带精确调用身份。
- `ProfileGuestLifecycle`：为 `gl_surface_view` 把 Profile 声明的
  startup/resume/input/frame/pause/shutdown 批次与 host-managed surface 严格编排为
  单一有状态流程；停止时 guest finalization 必须位于 shutdown 与 surface close 之间；
  运行中 suspend/resume 精确执行 pause/resume phase，挂起期间拒绝 frame 与 input；
  任一 guest phase 失败时先发布 sticky wait interruption，再进入有界清理，且清理错误不得
  覆盖原始 phase 异常；
  Java declarations 与 native-call receiver 进入调用方提供的统一
  class registry，调用帧仍只进入注入的通用 executor。
- `QuirkRegistry::Load/Validate`：严格加载 `data/quirks.toml` 的理由、风险、owner 与
  测试引用；含 quirk 的 Profile 目录必须显式通过注册表验证。
- `DescribeLifecycle` / `LifecycleFrameRunner`：把三种 Profile 生命周期映射为稳定的通用
  回调路由，并以唯一实现执行输入、生命周期、渲染、呈现、音频、调度与 Clock 计时。
- `AssembleProfileVfs`：把已导入数据与 Profile mount 精确配对，在全新 VFS 中挂载并
  校验 required mount、manifest 和 working directory，失败时不发布半成品文件系统。
- `AssembleProfileJava`：把 Profile Java 类/方法装入全新的 JNI registry 与 invocation
  engine，并只绑定显式提供且实际引用的通用 implementation handler。
- `ApplyProfileInput`：有 Profile 时精确选择其通用 input template，无声明时使用 catalog
  显式默认项；两条路径均进入相同 code-defined mapper。
- `ApplyProfileAudio`：按 source + path 精确解析 Profile 封面音乐资源，并把编码字节与
  loop 事实提交给通用 `MusicPlayer`；无声明时明确返回未启动。
- `ResolveProfileSoundPoolPath`：按纯数据 `audio.sound_pool` 的 source 与唯一 resource
  占位符，把非负 legacy resource 编号确定性格式化为规范相对路径；文件存在、解码和
  loaded 状态由下游分别验证，不在 Profile 层猜测。
- `AssembleProfileSessionPlan`：事务式汇总 lifecycle、VFS、Java、input 与 audio，返回
  唯一拥有运行期 VFS/JNI 状态和预解析资源的只读计划。
- `BootstrapProfileSession`：先执行 package + versionCode + `.so` SHA-256 精确匹配，
  再把命中 Profile 或显式 generic default 送入同一 plan 装配入口。
- `ProfileRuntimeCatalog`：拥有不可变的通用 Java implementation handler 与 input mapper
  目录，拒绝非法、空或重复 handler。
- `ProfileAssetBundle`：拥有已导入 VFS/audio 字节并在装配前拒绝非规范路径、大小写歧义、
  重复 mount/entry/audio 和空资产。
- plan/bootstrap 的高层重载直接消费一个 `ProfileRuntimeCatalog`，底层 span API 仅保留为
  组合原语，避免调用方错配 Java 与 input 目录。
- 最高层 plan/bootstrap 重载同时消费 `ProfileAssetBundle` 与 `ProfileRuntimeCatalog`，
  调用方不再分别拼接 VFS/audio/Java/input 容器。
- M1 将相同状态机装配 guest；M5 后续 WU 把 Profile 声明接入通用运行机制。

## 不变量

- 生命周期只使用 native_activity、gl_surface_view、custom_jni 通用模板。
- 游戏身份信息只有 Title Profile 一个来源；无 profile 也使用通用默认值。
- Profile 文件为 UTF-8 纯数据且不超过 200 行；未知字段、路径逃逸、非法或歧义身份失败。
- runtime 单次 guest call 预算省略时保持 2 亿 tick，只接受 1..10 亿的显式纯数据声明；
  不得由前端按标题猜测或无界提高。
- identity ABI 只接受强类型 `armeabi` 与 `armeabi-v7a`；不得把非 ARM ABI 或任意字符串
  带入目录匹配和后续启动选择。
- data/audio/java/quirks/input 只保存声明，不直接调用相邻模块或执行脚本。
- native-call 只允许固定 phase、Java class/method/signature、instance/static dispatch 与
  受限参数来源；参数数量/整数类型、常量值和 input phase 必须在装载时一致。
- native invocation 必须覆盖每个 call 的同序 target 与唯一 class reference；r0 固定为
  非空 guest JNIEnv，r1 按 dispatch 选择实例或 jclass。缺失 input、null receiver、
  非整数参数或部分运行期状态不得发布调用帧。
- native execution 必须在首个 guest 指令前验证整批调用身份、严格递增顺序、非空地址、
  偶数 stack words、进程栈/return trap 和 tick budget；运行失败不得丢失调用身份。
- Profile guest phase 失败必须先中断 guest 阻塞等待，再按既定逆序清理；中断和后续清理
  自身失败不得替换调用方观察到的首个运行错误。
- VFS 输入不得靠顺序或来源猜测；guest 根与 source 必须同时命中声明，额外输入明确失败。
- Java implementation id 必须命中真实 handler；类、签名或 handler 无效时不得发布
  部分 JNI registry。
- Java method 的 instance/static kind 必须由 Profile 显式声明，lookup 与 invocation
  不得用同名或默认 kind 猜测。
- input template 未注册时明确失败；无 Profile 声明只能使用 catalog 的显式默认项。
- audio 资源缺失、重复或为空时明确失败；错误 source 不得仅凭同名路径匹配。
- SoundPool path pattern 只接受唯一 `{resource}` 或 `{resource:0N}`（N 为 1..9）；差异
  必须留在 Profile 数据，不得在生产代码按游戏、厂商或资源编号分支。
- session plan 只在全部子装配成功后发布；运行期不再次猜测 input/audio 选择。
- bootstrap 只在零匹配时使用 generic default；非法身份或已选择路径装配失败不得回退。
- APK Profile 匹配不得猜 main library；无匹配返回空，ABI 自相矛盾或多个 library 同时
  精确命中必须明确失败。
- APK launch plan 必须拥有根库与依赖字节；API、系统库分类和 load bias 只来自 Profile
  及 Bionic 通用契约，不得由前端按标题拼接。
- runtime catalog 的 implementation id 必须规范且唯一，查询只接受精确 id。
- imported assets 构造后只读；VFS 键遵循统一大小写规则，audio 继续按 source + path 精确。
- 每个 enabled quirk 必须有注册定义和可定位测试；未注入注册表时不得进入匹配目录。
- 状态推进可由固定帧步进驱动，不依赖 sleep。
- 帧步骤顺序对三种生命周期完全相同；回调失败后进入可清理但不可继续步进的失败状态。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。

## 测试

`tests/session/` 的状态机、Title Profile、精确身份匹配、确定性与题库检查点测试。
