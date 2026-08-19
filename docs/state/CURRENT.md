# 当前状态

更新：2026-08-20 · DVM-49 Java 对象与 intrinsic 状态底座完成，DVM-47 gate 仍受阻

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
- **M9 DexVM**：DVM-1..46、48、49 已交付；DVM-47 gate 仍受阻。解释执行仍由
  `VmExecutionLock` 串行。GC-B 已实现
  全根枚举、精确非移动 STW 标记清除、句柄/存储槽复用、宿主析构以及只在安全 opcode
  发生的确定性水位触发；`gc_watermark_percent` 默认 75，0 回到 GC-A。A5 默认配置
  exact 三轮保持 `468/468000`、`f91150b4...`，16 MiB/1% 强制回收探针也通过同一
  golden 且日志确认多轮真实回收。DVM-47 仍受阻：A6 在 GC gate 前命中 APS-4
  DT_SONAME/inventory identity 通用契约，DH 无 guest fault 但固定 step 的呈现序列
  100/97 漂移，A6 长运行因此未执行；`dexvm.gc` 诚实保持 `partial`。
  子线程 native
  调用仍复用 root guest 栈/thread id，JNI/DexVM monitor 表尚未合一；`threads` 与
  `monitors` 保持 `partial`。APK class_def 现为全量注册、首次解析/实例化/调用时链接；
  未触达 SDK 类的缺失父类/接口不再阻断进程，触达后仍明确失败或在 survey 中记账。
  JNI class identity 仍全量发布，但 jclass global reference 改为 static native 真正出向
  时才创建。`java.lang.Enum` intrinsic 已按 pinned libcore Enum.java 迁入 core 目录并交付
  全表面（valueOf 直读 enum static 常量不走反射；enum `values()` 经数组
  `Object.clone()` 浅拷贝）。`Object.clone()` 为 overridable virtual
  intrinsic：`Cloneable` 检查对照 libcore Object.java，payload 浅拷贝对照
  AOSP `dvmCloneObject`；数组可赋给 `Cloneable`/`Serializable`。
  pinned libcore `java.lang` 顶层 8 个 interface 已全部进入
  `java_lang_interfaces.cpp`（含 `Appendable`/`AutoCloseable`/`Iterable`/
  `Readable`）；`Readable.read` 依赖尚未交付的 `java.nio.CharBuffer`，与其余
  未实现接口方法一样显式失败。PVZ NA 已越过 222/70 类静态层级清单和全局引用容量；其首缺口
  `COPPAActivity.isTaskRoot()Z` 已实现（Manifest launcher 为进程唯一 task 根，
  startActivity 到达的 Activity 回答 false，handle 记账于 `task_root_activity`），
  PVZ NA 后续缺口待下一次命中批次确认，不等同于 title 启动成功。
  DVM-48 已把 `java.lang.Thread` 从 Android 侧表迁入 dexvm core：root/child
  stable identity、构造期 per-VM ID、virtual `this.run()`、Runnable 转发、
  start-once、name/priority/isAlive、interrupt、join/timed join、sleep/yield/
  holdsLock 与 active GC roots 均闭合；`Object.wait(JI)`/join/sleep 共用注入的
  monotonic Clock，停泊释放 execution lock。priority 不映射 host scheduler，
  daemon 不驱动 session 退出；既有 native 栈/monitor 边界使总能力仍为 partial。
  DVM-49 统一 session `Object[]` identity/store 与 class 映射；intrinsic 状态改走
  trace/sweep/clone 注册契约。
  `IntrinsicClassBuilder` 已重构为工厂式 API：`Class/RootClass/Interface` 一次
  声明类型头（普通类默认父类 Object，仅 java.lang.Object 显式无父类），
  方法 `Constructor/StaticMethod/VirtualMethod/FinalMethod`、字段
  `InstanceField/StaticField`、未实现 `Unimplemented*` 各自语义化命名，空
  handler、保留名、构造器返回值与整型常量范围在声明/装配期拒绝；core
  intrinsics、dexvm_android、集成与测试共 206 个源文件及 gap-report 源解析
  已迁移，VM/linker 语义不变。
- **兼容性基线**：Layout UI 已验收 complete；存档沙盒、GUI、intrinsic 声明迁移与
  MSVC 工程内/工程间并行编译已交付。能力现状以 `capabilities.toml` 为准。

## 验证基线

- Windows/x64 `windows-msvc`：832/832 CTest（含 DVM-49、DVM-48、GC-B、Profile、Scenario 与文档门禁）。
- macOS/arm64 最近记录：766/766 CTest。
- Windows 预设使用原生核数并行工程；OGPlay 自有 MSVC target 启用 `/MP`。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 呈现确定性后复验 DVM-47 长运行 gate。
2. 收口子线程 native guest 栈/thread id 与 JNI/DexVM monitor 统一。
3. Linux M9 严格出口复验。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- DVM-47 未完成：A6/DH 三轮 exact 与 GC 长运行门禁尚未全部成立，不得把
  `dexvm.gc` 推进为 complete。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
