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

- `.local/asop/framework/base/core/java/android/app/LoadedApk.java`
- `.local/asop/framework/base/core/java/android/app/Application.java`
- `.local/asop/framework/base/core/java/android/app/ActivityThread.java`

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
