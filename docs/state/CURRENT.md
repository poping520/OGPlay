# 当前状态

更新：2026-08-24 · BND-15 Boundary/OpenSL final audit

## 当前阶段

- **Native Boundary 重构**：BND-1..7 已闭环。API-filtered metadata-only catalog、late
  import、SVC #2/#3 dense `{fn,self}` transport、JIT 外原异常恢复、typed A32 ABI、libc
  per-export 并发安全 override 与端到端 benchmark 均已有门禁。concrete final module 只注入
  bounded services；EGL/GLES1/GLES2 共享唯一 graphics context，中央 `Impl::Invoke*` 已删除。
  BND-5 按 AOSP 4.4.4 完整实现 liblog 23 个 API，guest 日志进入 `guest.liblog` 并带
  `[guest]`。BND-8 完成 AOSP Wilhelm ABI/PCM/callback 设计；BND-9 已发布全部 51 个
  `SL_IID_*` `STT_OBJECT` pointer global 与精确只读 UUID record，data 不占 dense slot，
  focused 8/8 通过。BND-10 已实现独立线程安全 PCM mixer：PCM8/16、mono/stereo、线性
  重采样、volume/mute/pan、bounded queue、消费事件及多 player 饱和加性混音，focused
  5/5 通过。BND-11 已发布 3 个 public function 和 53 个 private callable，immutable AOSP
  vtable 直接指向 dense thunk；concrete final module 完成 Engine→OutputMix→PCM AudioPlayer、
  Object/Play/Queue/Volume 状态与离线混音，范围外 constructor 规范失败，focused 16/16
  通过。BND-12 已把 mixer 加性接入 SoundPool process output，并以专用 A32 guest
  thread/CPU/TLS/stack 执行 async Object、Play 与每-buffer callback；callback 内可正常
  SVC #2 Enqueue，失败延迟恢复。OpenSL 地址移入与 JNI lease 不冲突的 `0x718..0x71b`，
  focused 19/19 通过。BND-13 同步 A32 11-word ABI gate 与重构后的 quirk 测试路径，最终
  full CTest 923/923（含 architecture gate）通过，`runtime.opensles_virtual_so` 已晋升
  `complete`。BND-14 又以真实 A32 callback 读取专用 TLS、经 SVC #2 re-enqueue 并播放
  第二个 buffer，补齐 callback-thread 端到端证据；mute 现在只静音而继续推进队列/回调，
  focused 20/20 通过。BND-15 逐项复核目录/ownership/依赖门禁并同步 capability 中搬迁后的
  测试路径；最终 full CTest 924/924（含 architecture gate）通过。

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
