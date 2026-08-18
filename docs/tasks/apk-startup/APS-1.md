# APS-1 · Manifest startup facts

## 目标（一句话）

扩展 APK/Manifest model，使启动层可直接取得 package、Application 类、确定性的
MAIN/LAUNCHER Activity（含 alias）并完成 Android 类名归一化，不依赖 Title Profile。

## 依赖

- 现有 APK archive / binary AndroidManifest parser
- `docs/design/apk-startup/04-manifest-and-lifecycle.md`

## 设计锚点

- 04 §1–4
- 08 §3

## 语义出处

编码前核对并在结果段写实际函数：

- `.local/asop/framework/base/core/java/android/content/pm/PackageParser.java`
- `.local/asop/framework/base/core/java/android/app/ActivityThread.java`

## 变更

- Manifest model 增 Application `android:name`；
- 组件 model 保留 activity/activity-alias、targetActivity、enabled、intent-filter；
- 提供统一 class-name normalization；
- 提供 `ResolveLauncherComponent`/等价纯 loader API；
- no launcher / ambiguous-invalid data 返回 typed error；
- frontend 不在本 WU 切换。

## 验收（机器可判定）

- `.App`、`App`、FQCN 归一化用例全绿；
- default Application、custom Application 全绿；
- direct launcher、activity-alias launcher 全绿；
- disabled/no launcher 负例明确失败；
- 同一 fixture 多次解析选择结果稳定；
- full CTest 无回归。
