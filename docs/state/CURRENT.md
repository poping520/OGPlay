# 当前状态

更新：2026-08-25 · DVM-83 Java GLES façade 与 NIO/Bitmap 编组

## 当前阶段

- **EGL/GLES API19 补齐**：BND-16 冻结 AOSP/KTU84P/PVZ ABI；BND-17 已实现 EGL 13 项
  基础 API，BND-18..20 已闭合 GLES1 Bounds 与全部 62 个缺口，145 core 均有 handler。
  BND-21/22 闭合 GLES2 state/object 与 transfer/query 41 项；BND-23 完成最后 26 项
  shader/uniform/vertex API；142 个 GLES2 core 现均有 concrete handler。BND-24 复验 PVZ
  exact ELF 导入 `eglGetProcAddress + 142/142 GLES2 core`；关闭 survey 的 bounded run
  已越过 native load/JNI_OnLoad/OpenGL，新的首 fault 为 DexVM
  `Window.setSoftInputMode(I)V` 缺口，不属于 OpenGL 闭集。

- **Native Boundary 重构**：BND-1..15 已闭环 metadata catalog、dense `{fn,self}` transport、
  typed A32 ABI、final module、liblog 与 AOSP Wilhelm/OpenSL ES；EGL/GLES1/GLES2 共享唯一
  graphics context，PCM/SoundPool 共用 mixer，真实 callback/TLS/re-enqueue 已有门禁。
  `runtime.opensles_virtual_so` 为 `complete`，阶段 full CTest 924/924。

- **APK Startup**：APS-1..9 已闭合 Manifest/ABI facts、rootless `AndroidGuestProcess`、
  process-lifetime loader/JNI_OnLoad、Application root 与 Profile legacy adapter；app ELF 只由
  Java `System.load*` 追加。A5 exact Scenario 三轮确定、无 fault 且 clean shutdown。
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
  DVM-78/79 已用统一 runtime 发布常用集合及 I/O/ZIP；DVM-80 把 core/Android intrinsic
  收敛为 12/18 个 family TU，并将 22 个非 Android Java 声明迁回 core。DVM-81 恢复 API 19
  ContextWrapper/ContextThemeWrapper 与 app 组件继承链，生命周期一次性绑定 base Context，
  既有 Context 能力只经 base 委托。DVM-82 新增统一 `NioRuntime`，发布 API 19 Buffer/
  ByteBuffer/ByteOrder 与六类 typed buffer；Java/JNI direct-buffer 共用 identity、guest
  address 和 capacity，backing 只经 session `AddressSpace`。Java/Android core 仍为
  partial。DVM-83 已从固定 AOSP 源发布 GLES10/10Ext/11/11Ext/20、GLUtils、GLU 的
  API 19 可链接面；可确定的 scalar/String/array/NIO Buffer 调用复用 sealed native GLES
  binding 与唯一 `GuestGlContext`，direct buffer 不复制、heap/view 输出 copy-back 不移动
  cursor，GLUtils 复用既有 Bitmap 像素。无法确定 native 映射的 Java wrapper 继续记账并
  明确失败，因此 `dexvm.java_gles` 保持 partial。后续顺序与边界见
  [`12-api19-capability-stack.md`](../design/dexvm/12-api19-capability-stack.md)。
## 验证基线

- Windows VS 18.8 / MSVC 14.51 `windows-msvc` Debug 全目标构建通过；受影响的 DEX、
  Android/EGL/GLES2 focused 27/27（9645 assertions）与 architecture 5/5 通过。
- DVM-80/81 Windows Debug 全目标构建通过。DVM-82 `windows-msvc` configure 与 Debug
  全目标构建通过；NIO/JNI direct 6/6（28 assertions）、core catalog/side-table/JNI
  aggregate/superclass 4/4（462 assertions）、architecture 5/5 定向回归通过。全量 CTest
  按计划留到阶段最后一个 WU。
- DVM-83 Windows Debug 受影响目标构建通过；Java GLES/NIO/EGL/catalog/JNI superclass/
  Java-native texture state 定向 10/10（2513 assertions）、生成面 stale check 与
  architecture 6/6 通过；全量 CTest 按计划留到阶段最后一个 WU。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前 Activity switch/SMS-network 启动阻断后，
   复验 DVM-47 与 interpreter threaded title gate。
2. Linux M9 严格出口复验。
3. 进入 DVM-84：让 API 19 AudioTrack 与现有 OpenSL ES 共用唯一 PCM queue/mixer backend。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- DVM-47 未完成：A6/DH 三轮 exact 与 GC 长运行门禁尚未全部成立，不得把
  `dexvm.gc` 推进为 complete。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
