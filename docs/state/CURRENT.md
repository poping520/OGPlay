# 当前状态

更新：2026-08-21 · DVM-63 Single ClassLoader facade

## 当前阶段

- **APK Startup**：APS-1 已发布 Manifest Application/launcher facts；APS-2 已建立
  APK native inventory、固定 v7a→armeabi 默认优先级和 selected-ABI 隔离视图；APS-3
  已抽出无 application ELF 的 `AndroidGuestProcess`；APS-4 已加入 process-lifetime ELF
  append、selected-ABI `NativeLibraryLoader`、constructors 与显式 JNI_OnLoad 状态机；
  APS-5 已把 DexVM `System.load/System.loadLibrary` 接到同一 loader，并以真实
  A JNI_OnLoad → Java callback → load B 夹具闭合同线程重入；APS-6 已在 Activity/surface
  前建立 default/custom Application process root，固定 class init、构造、attach、onCreate
  顺序与异常短路；APS-7 新增 `AndroidAppProcess` 并把 `run-apk` 切到 Manifest 驱动的
  rootless generic path，app ELF 只由 Java `System.load*` 动态追加；APS-8 新增 optional
  Profile v3 与 v1/v2 legacy applicability adapter，无 Profile、纯 Java或旧 hash 不命中
  都继续 generic startup，Profile 不再决定 root `.so` 或 process ABI；APS-9 已闭合 A–J
  fixture、rootless dynamic Bionic dependency、frontend source gate 与旧设计 superseded
  链接。Asphalt 5 exact Scenario 连续三轮为 468/468000、`f91150b4…`、无 fault 且 clean
  shutdown，实际 Java explicit load 仅 `libasphalt5.so`。
- **M9 DexVM**：DVM-1..46、48..61 已交付；DVM-47 gate 仍受阻。解释执行仍由
  `VmExecutionLock` 串行。GC-B 已实现
  全根枚举、精确非移动 STW 标记清除、句柄/存储槽复用、宿主析构以及只在安全 opcode
  发生的确定性水位触发；`gc_watermark_percent` 默认 75，0 回到 GC-A。A5 默认配置
  exact 三轮保持 `468/468000`、`f91150b4...`，16 MiB/1% 强制回收探针也通过同一
  golden 且日志确认多轮真实回收。DVM-47 仍受阻：A6 在 GC gate 前命中 APS-4
  DT_SONAME/inventory identity 通用契约，DH 无 guest fault 但固定 step 的呈现序列
  100/97 漂移，A6 长运行因此未执行；`dexvm.gc` 诚实保持 `partial`。
  APK class_def 全量注册并懒链接；未触达 SDK 层级缺口不阻断，触达后明确失败或记账。
  API-19 `Enum`/clone/java.lang interface shape 已补齐；PVZ NA 越过静态层级与引用容量，
  `isTaskRoot()` 使用通用 task root 事实，后续 title 缺口尚待确认。
  DVM-48..50 已闭合 `java.lang.Thread`、统一 Object[]/class identity、每 guest Java
  线程独立 A32/JNI/Bionic 上下文、统一 monitor/synchronized 与跨 context clinit；
  阻塞原语释放执行锁并只用统一 Clock，`dexvm.threads`/`monitors` 为 `complete`。
  DVM-51 已交付类型化 intrinsic、预绑定字段与 API-19 shape 工具；DVM-52 已交付默认
  关闭的有界 `dexvm.trace` 和停界 `dexvm.stack`，MCP/CLI 消费与异步抢占仍属后续。
  DVM-53..60 已交付 FastCode、threaded 稳态循环、双后端验证与 Profile/CLI/Scenario
  受检入口；热指令保留直达，微基准只报告、不设时序断言。title 三 gate 未验收，
  `dexvm.interpreter_threaded` 保持 `partial`，生产默认仍为 switch。
  `IntrinsicClassBuilder` 类型/方法/字段 API 已完成全仓迁移，非法声明在装配期拒绝。
  DVM-61 已把 guest identity hash 与可复用 handle/记录槽分离；Class identity 按
  descriptor 稳定派生，`System.identityHashCode` 绕过 override，`Object.toString`
  保持 libcore 虚调用及 throwable 传播语义。
  DVM-62 已建立 reflection linker metadata 底座：正式 `ClassNameCodec` 统一受检
  descriptor/name 转换；linker 区分 bootstrap/application defining-loader role，
  分离 direct 与 flattened interfaces，按 DEX/intrinsic 声明顺序保存 own direct/
  virtual methods 与 fields，并发布完整 access flags 和 direct/static/virtual/interface
  调用类别。array loader 跟随 component，primitive Class 补齐 `V`。
  DVM-63 已建立每 VM 唯一稳定的 application `PathClassLoader` 与
  `BootClassLoader` facade，固定 parent 链和 bootstrap/application delegation；linker
  分离 defining/initiating role，使 `findLoadedClass` 只读且不把已注册 DEX class 当作
  loaded，`loadClass` 受检解析 binary name、支持 array、忽略 resolve 且不触发初始化。
  动态 classpath、多 namespace 与自定义 class definition 明确不支持。wrapper 物化、
  Class core 与 reflective invoke/Field/Array 仍由 DVM-64..69 交付。
## 验证基线

- Windows/x64 `windows-msvc`：872/872 CTest（含 interpreter v2、Profile、Scenario 与文档门禁）。
- macOS/arm64 最近记录：766/766 CTest。
- Windows 预设使用原生核数并行工程；OGPlay 自有 MSVC target 启用 `/MP`。
- 浮点 `FromChars` 在 HAL：macOS `strtof_l`/`strtod_l`，Windows/Linux `std::from_chars`。

## 下一步

1. 继续 DVM-64：Reflection wrappers。
2. 通用闭合 A6 DT_SONAME identity 与 DH 当前 Activity switch/SMS-network 启动阻断后，
   复验 DVM-47 与 interpreter threaded title gate。
3. Linux M9 严格出口复验。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- DVM-47 未完成：A6/DH 三轮 exact 与 GC 长运行门禁尚未全部成立，不得把
  `dexvm.gc` 推进为 complete。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
