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
