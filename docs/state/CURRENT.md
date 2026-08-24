# 当前状态

更新：2026-08-25 · DVM-80 intrinsic family/ownership 收敛

## 当前阶段

- **EGL/GLES API19 补齐**：BND-16 冻结 AOSP/KTU84P/PVZ ABI；BND-17 已实现 EGL 13 项
  基础 API，BND-18..20 已闭合 GLES1 Bounds 与全部 62 个缺口，145 core 均有 handler。
  BND-21/22 闭合 GLES2 state/object 与 transfer/query 41 项；BND-23 完成最后 26 项
  shader/uniform/vertex API；142 个 GLES2 core 现均有 concrete handler。BND-24 复验 PVZ
  exact ELF 导入 `eglGetProcAddress + 142/142 GLES2 core`；关闭 survey 的 bounded run
  已越过 native load/JNI_OnLoad/OpenGL，新的首 fault 为 DexVM
  `Window.setSoftInputMode(I)V` 缺口，不属于 OpenGL 闭集。

- **Native Boundary 重构**：BND-1..7 已闭环。API-filtered metadata-only catalog、late
  import、SVC #2/#3 dense `{fn,self}` transport、JIT 外原异常恢复、typed A32 ABI、libc
  per-export 并发安全 override 与端到端 benchmark 均已有门禁。concrete final module 只注入
  bounded services；EGL/GLES1/GLES2 共享唯一 graphics context，中央 `Impl::Invoke*` 已删除。
  BND-5 按 AOSP 4.4.4 完整实现 liblog 23 个 API，guest 日志进入 `guest.liblog` 并带
  `[guest]`。BND-8..15 已闭合 AOSP Wilhelm ABI、51 个 IID global、PCM mixer、完整
  Engine→OutputMix→AudioPlayer、SoundPool 混音与真实 A32 callback/TLS/re-enqueue；
  mute 继续推进队列，地址与 JNI lease 隔离，目录/ownership/capability 已复核。
  `runtime.opensles_virtual_so` 为 `complete`，最终 full CTest 924/924。

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
  exact/强制回收 golden 已稳定；PVZ NA 的 liblog/OpenGL boundary 阻断已闭合。
  DVM-70..74 闭合 Window/config、JNI identity、目录/asset、GLSurfaceView policy 与
  IntentFilter；DVM-75..77 交付 String.format、有界致命栈与 PackageManager P0。
  DVM-78 已用 CollectionRuntime 发布常用集合；DVM-79 把 I/O/ZIP 收敛到 core，以
  IoRuntime/ZipRuntime 统一状态、GC 与 VFS。DVM-80 已把 core/Android intrinsic 物理目录
  从 45/152 个 `.cpp` 收敛为 12/18 个 family TU，并将 22 个非 Android Java 声明迁回
  core；平台事实只经窄 services 注入，未改变能力状态。后续顺序与边界见
  [`12-api19-capability-stack.md`](../design/dexvm/12-api19-capability-stack.md)。
## 验证基线

- Windows VS 18.8 / MSVC 14.51 `windows-msvc` Debug 全目标构建通过；受影响的 DEX、
  Android/EGL/GLES2 focused 27/27（9645 assertions）与 architecture 5/5 通过。
- DVM-76 focused 6/6；Release PVZ 关闭 survey 复现通过，首 fault 明确失败并附带
  context、guest thread、fault opcode/method/dex pc 与 6 层 Java 栈。
- DVM-77 PackageManager/Manifest focused 13/13；Release PVZ 越过旧 fault并固定
  `LinkedHashMap`，无残留进程。
- DVM-80 Windows Debug 全目标构建与 core/Android catalog、GC、JNI、I/O/VFS/ZIP、
  EGL/UI、architecture 定向回归通过；新增 layout gate 锁定 12/18 TU 与 ownership。
  按计划未运行全量 CTest，留到本阶段最后一个 WU。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前 Activity switch/SMS-network 启动阻断后，
   复验 DVM-47 与 interpreter threaded title gate。
2. Linux M9 严格出口复验。
3. 进入 DVM-81：补齐 API 19 Context hierarchy，再推进 NIO/direct-buffer。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- DVM-47 未完成：A6/DH 三轮 exact 与 GC 长运行门禁尚未全部成立，不得把
  `dexvm.gc` 推进为 complete。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
