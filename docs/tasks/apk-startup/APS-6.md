# APS-6 · Minimal Application startup

## 目标（一句话）

在 launcher Activity 前执行最小 Android Application 初始化：resolve → `<clinit>` →
construct → attach base Context → `onCreate()`，并把实例保存为 process 级 Java root。

## 依赖

- APS-1、APS-5
- `docs/design/apk-startup/04-manifest-and-lifecycle.md`

## 设计锚点

- 04 §4–5、§7–8
- 07 §6–7
- 08 §2 Case A/B/J

## 语义出处

编码前核对并记录：

- `.local/aosp/framework/base/core/java/android/app/LoadedApk.java`
- `.local/aosp/framework/base/core/java/android/app/Application.java`
- `.local/aosp/framework/base/core/java/android/app/ActivityThread.java`

重点固定 `makeApplication`/attach/onCreate 的可观察顺序；不引入完整 Instrumentation。

## 变更

- 新增 Application lifecycle/coordinator 或扩现有 session lifecycle；
- 缺省 Application 使用 `android.app.Application` intrinsic；
- custom Application 走 DEX 类初始化/构造/虚分派 onCreate；
- Context/Application identity 对 Activity 可见；
- 失败后禁止继续 Activity。

## 验收（机器可判定）

- default/custom Application fixture 全绿；
- Application `<clinit>` loadLibrary 可成功；
- Application `onCreate` loadLibrary/JNI callback 可成功；
- Application 抛异常时 launcher Activity 未实例化；
- Application instance 在 process 内 identity 稳定；
- full CTest 无回归。

## 语义核对结论

- API 19 `LoadedApk.makeApplication` 先复用已创建实例，否则选择 Manifest Application
  或 `android.app.Application`，通过 `newApplication` 完成构造/attach，并只在成功后发布。
- `Application.attach(Context)` 的可观察核心是先调用虚方法 `attachBaseContext`，再保存
  package context；OGPlay 直接保留这段顺序，不引入 Instrumentation。
- `ActivityThread.handleBindApplication` 在 `makeApplication` 成功后才调用 Application
  `onCreate`，异常未被 Instrumentation 消费时终止启动；OGPlay 没有异常消费层，因此直接
  fail closed，禁止 Activity 与 surface 副作用。

## 结果（机器可判定，已达成）

- 新增 API 19 bounded `android.app.Application` intrinsic，并在 append-only catalog tail
  注册；Application、base Context 与成功 descriptor 成为 `DexVmAndroidContext` 的
  process-lifetime root，`Context.getApplicationContext`/`Activity.getApplication` 共享身份。
- `StartDexApplication` 固定执行 resolve→`<clinit>`→默认构造→虚派
  `attachBaseContext`→虚派 `onCreate`；同 descriptor 幂等，换 descriptor 明确失败，异常时
  清除临时 root。
- `DexActivityLifecycle::Start` 在打开 surface 和解析 launcher 之前启动 Application；
  throwing fixture 证明 Activity 构造计数保持 0，surface callback 未触发。
- `application.dexasm` 覆盖 default/custom、四阶段顺序、稳定 identity、`<clinit>` 与
  `onCreate` 的真实 `System.loadLibrary`/JNI_OnLoad，以及失败短路。
