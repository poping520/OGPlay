# APS-7 · AndroidAppProcess、launcher Activity 与 frontend cutover

## 目标（一句话）

建立统一 `AndroidAppProcess` 启动 orchestrator，把 `run-apk` 切到
Manifest → process/DexVM prepare → Application → launcher Activity 的 generic path，并
移除 frontend 的 app root `.so`/JNI_OnLoad 决策。

## 依赖

- APS-1..6
- `docs/design/apk-startup/03-target-architecture.md`
- `docs/design/apk-startup/04-manifest-and-lifecycle.md`

## 设计锚点

- 03 全篇
- 04 §3、§6
- 02 §4

## 语义出处

编码前核对：

- `.local/asop/framework/base/core/java/android/app/ActivityThread.java` 的 Activity launch 顺序；
- `.local/asop/framework/base/core/java/android/app/Activity.java` 中本 WU 需要的 attach 前提。

只取语义，不复制 ActivityThread/Instrumentation 结构。

## 变更

- 建立 process-level startup state machine；
- 组合 VFS/package/ABI/native shell/loader/DexVM/Application/Activity；
- `DexActivityLifecycle` 接收已初始化 Application/process context；
- Manifest launcher 为默认入口，profile override 仅在存在时覆盖；
- `run_apk.cpp` 不再要求 exact profile 才继续；
- 移除 frontend root module 选择与显式 `InitializeJniLibrary()`。

## 验收（机器可判定）

- 无 Profile 最小 APK：Application → launcher onCreate/onStart/onResume 顺序全绿；
- Activity `<clinit>` / `onCreate` loadLibrary case 全绿；
- no launcher 明确 startup error；
- frontend 测试证明没有 app `.so` preload；
- clean stop/finalization 全绿；
- full CTest 无回归。

## 语义核对结论

- API 19 `ActivityThread.performLaunchActivity` 在 Activity 构造前取得 package Context 与
  process Application，随后由 `Activity.attach` 注入 Application/window/context，再进入
  instrumentation `callActivityOnCreate`；OGPlay 保留这些对游戏可见的前置事实和顺序，
  不复制 ActivityThread/Instrumentation/task 管理结构。
- `Activity.attach` 的本 WU 可见前提是稳定 Application identity、package Context 与已准备
  window/surface service；这些由 process context/intrinsic 提供，Activity 生命周期仍复用
  `DexActivityLifecycle`。

## 结果（机器可判定，已达成）

- 新增 session 级 `AndroidAppProcess`，Create 内部按 package→API19 system ELF shell→
  optional ABI/loader→DexVM 准备推进，再显式执行 Application 与 Manifest launcher；非法
  跨阶段调用明确失败，Stop 复用 lifecycle 的 Java threads→native finalization 反序。
- `AndroidGuestCallSession::AdoptProcess` 只包装 rootless process 供现有 DexVM/JNI 接口复用，
  不加载 app ELF；`run-apk` 已删除 legacy Start/root module/`PrepareApkProfileLaunch` 与显式
  JNI 初始化决策。
- frontend 无 exact Profile 时使用 Manifest、API 19 与受控 800×480 默认继续；命中旧
  Profile 时仅消费现有 data/surface/budget/quirk/entry override，ABI 始终由 APK inventory
  resolver 决定。
- fixture 覆盖无 Profile/无 ABI pure-Java、Application→Activity onCreate/onStart/onResume、
  no launcher、Activity `<clinit>`/`onCreate` loadLibrary、创建后零 application modules、
  clean Stop；源码门禁证明 frontend 不含 legacy root/preload API。
