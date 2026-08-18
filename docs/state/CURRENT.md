# 当前状态

更新：2026-08-18 · DexVM APK 类链接已切为首次真实使用

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
- **M9 DexVM**：DVM-1..41 已交付；解释执行仍由 `VmExecutionLock` 串行。子线程 native
  调用仍复用 root guest 栈/thread id，JNI/DexVM monitor 表尚未合一；`threads` 与
  `monitors` 保持 `partial`。APK class_def 现为全量注册、首次解析/实例化/调用时链接；
  未触达 SDK 类的缺失父类/接口不再阻断进程，触达后仍明确失败或在 survey 中记账。
  JNI class identity 仍全量发布，但 jclass global reference 改为 static native 真正出向
  时才创建。PVZ NA 已越过 222/70 类静态层级清单和全局引用容量；其首缺口
  `COPPAActivity.isTaskRoot()Z` 已实现（Manifest launcher 为进程唯一 task 根，
  startActivity 到达的 Activity 回答 false，handle 记账于 `task_root_activity`），
  PVZ NA 后续缺口待下一次命中批次确认，不等同于 title 启动成功。
- **兼容性基线**：Layout UI 已验收 complete；存档沙盒、GUI、intrinsic 声明迁移与
  MSVC 工程内/工程间并行编译已交付。能力现状以 `capabilities.toml` 为准。

## 验证基线

- Windows/x64 `windows-msvc`：807/807 CTest。
- macOS/arm64 最近记录：766/766 CTest。
- Windows 预设使用原生核数并行工程；OGPlay 自有 MSVC target 启用 `/MP`。

## 下一步

1. 按命中批次闭合 DexVM 缺口，随后推进 GC-B 与解释器 v2 threaded 分批。
2. 收口子线程 native guest 栈/thread id 与 JNI/DexVM monitor 统一。
3. Linux M9 严格出口复验。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
