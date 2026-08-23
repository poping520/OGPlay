# 当前状态

更新：2026-08-23 · BND-7 Boundary ownership extraction

## 当前阶段

- **Native Boundary 重构**：BND-1..4 已闭环。API-sealed catalog 是 6 个 Virtual SO 的
  metadata 来源，支持 API filtering、metadata-only preflight 与 late import。SVC #2/#3
  使用 live-register dense transport；每个 export seal 为 `{fn,module*}`，成功路径继续
  JIT，不读取 SONAME/local id。fast fault 在 JIT 外恢复原异常；JNI/RegisterNatives/
  JNI_OnLoad 语义不变。libc 五个 override 独立 direct-bind，并有跨 guest thread 并发
  回归。各 SO 实现归属 concrete final module，中央 dispatch/`Impl::Invoke*` 已删除；
  module 仅构造注入 bounded call/Android/graphics services。EGL/GLES1/GLES2 共享唯一
  `GraphicsBoundaryContext`、`GuestGlContext` 与 ANGLE state。architecture gate 扫描全部
  boundary module source，并单独约束 `TryFastCall()` direct router；A32 typed layer 与
  end-to-end ABI benchmark 已覆盖。新增 `libOpenSLES.so` export-less loader scaffold；仅
  满足 `DT_NEEDED`，不分配 thunk，所有 OpenSL ES 函数仍明确未实现。BND-5 另按 AOSP
  4.4.4 target liblog 完整发布 23 个 writer/logprint/event-tag-map API；A32 variadic/
  `va_list`、text/binary wire buffer、filter/format 与 guest-VFS event tags 已接入，guest
  日志统一进入 `guest.liblog` structured category 并带 `[guest]` 前缀。
  BND-6 在零行为变化下建立 `core/services/modules/facade` 目录，Android/EGL/log export
  metadata 跟随 module，built-in registration 与 generic catalog 分离；boundary tests 同步
  迁入 ownership 目录。15 项 focused integration、168 assertions 与 architecture gate
  已通过。BND-7 已将 direct binding/thunk/router/fault 迁入 core，Android memory、共享
  graphics context、frame/stat/trace 迁入 services，Android/EGL/GLES1/GLES2 concrete
  implementation 迁入各自 module；libc override ownership 回到 bionic。core/service/module
  依赖方向与 direct `{fn,self}` router 由 architecture gate 递归验证，20/20 focused
  CTest 通过。下一阶段按本会话目标先完成 AOSP Wilhelm 设计，再实现 OpenSL ES module。

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
- **M9 DexVM**：DVM-1..46、48..69 已交付；解释仍由 `VmExecutionLock` 串行。GC-B 已闭合
  全根、精确非移动 STW mark-sweep、复用与安全点水位触发；线程/monitor、FastCode/threaded
  双后端、类型化 intrinsic、诊断、稳定 identity、ClassLoader 与 bounded reflection
  foundation 均已交付。DVM-66..69 完整覆盖 invoke/construct/field/array、nested/enclosing/
  throws system metadata、`Class.forName` failure identity 与 encoded-value fail-closed；generic、
  annotation proxy、多 ClassLoader 和动态 definition 仍明确不实现。
  DVM-47 gate 仍受 A6 DT_SONAME identity 与 DH 固定 step 100/97 漂移阻断，A6 长运行未执行；
  `dexvm.gc` 与 `dexvm.interpreter_threaded` 保持 `partial`，threaded 生产默认仍关闭。A5 GC
  exact/强制回收 golden 已稳定；PVZ NA 的 `__android_log_vprint` boundary import 阻断已由
  BND-5 关闭，title 后续行为仍待按既有 playbook 复验。
## 验证基线

- Windows/x64 `windows-msvc`：872/872 CTest（含 interpreter v2、Profile、Scenario 与文档门禁）。
- macOS/arm64 BND-5 liblog Virtual SO 后：917/917 CTest（102.26 秒）；新增 liblog
  focused 为 6/6、75 assertions，Bionic 为 12/12、102 assertions，preflight/late import/
  rootless loader/capability/documentation/hot-path gate 均通过。
- Windows 预设使用原生核数并行工程；OGPlay 自有 MSVC target 启用 `/MP`。
- 浮点 `FromChars` 在 HAL：macOS `strtof_l`/`strtod_l`，Windows/Linux `std::from_chars`。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前 Activity switch/SMS-network 启动阻断后，
   复验 DVM-47 与 interpreter threaded title gate。
2. Linux M9 严格出口复验。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- DVM-47 未完成：A6/DH 三轮 exact 与 GC 长运行门禁尚未全部成立，不得把
  `dexvm.gc` 推进为 complete。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
