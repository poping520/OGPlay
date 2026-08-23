# 当前状态

更新：2026-08-23 · BND-4 Native Boundary closure

## 当前阶段

- **Native Boundary 重构**：BND-1 已建立 API-sealed `BoundaryCatalog`，以 5 个实际有
  handler 的 Virtual SO 作为 SONAME/export 唯一事实来源；Bionic Profile 的 9 项平行
  boundary 列表已删除。synthetic module 从完整 provider exports 建 dynsym，late import
  可直接解析既有 Virtual SO。BND-2 已增加 CPU generic HostCallHook、live-register call
  frame、多页 sealed thunk arena 与 dense hot table；正常 SVC #2 在 Dynarmic callback 内
  完成并继续 JIT，observer session 保持 slow path，nested/clone/thread CPU 安装同一 hook。
  BND-3 已让 JNI SVC #3 复用同一 live-register transport，fast/slow entry 共用唯一
  receiver/thread/slot/return-width dispatch；RegisterNatives/JNI_OnLoad 与 JNI 语义未改。
  五个真实 guest libc memory intercept 已从 Virtual SO metadata 分离为独立
  `GuestSymbolOverrideDescriptor`。BND-4 已让现有 export 在 seal 时绑定 concrete final
  module invoke pointer，descriptor 只保留 module-local id/签名；`HleRoute`、全局
  fallback id、平行参数表和 import-driven synthetic builder 已删除。EGL/GLES1/GLES2
  继续共享唯一 `GuestGlContext`，fast/slow transport 共用同一 module binding。
  BND-1 闭环已补齐跨 API active module/export 过滤与 metadata-only link preflight；
  preflight 不再构造 ANGLE/surface runtime，module-local id 也不再要求与目录序号一致。
  BND-3 闭环已让 Boundary/JNI fast fault 按 thread/PC 保存并在退出 JIT 后恢复原始
  exception identity；unbound slot、invalid receiver 与 guest memory fault 已有等价测试。
  BND-2 闭环已把 hot table 收紧为 export-specific function pointer 与 concrete module
  instance；成功路径不再持有 descriptor/FastBinding，也不读取 SONAME/local id。

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
  exact/强制回收 golden 已稳定；PVZ NA 当前下一阻断为 `__android_log_vprint` boundary import。
## 验证基线

- Windows/x64 `windows-msvc`：872/872 CTest（含 interpreter v2、Profile、Scenario 与文档门禁）。
- macOS/arm64 最近记录：896/896 CTest。
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
