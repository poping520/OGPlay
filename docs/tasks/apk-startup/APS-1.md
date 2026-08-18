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

- `.local/asop/framework/base/core/java/android/content/pm/PackageParser.java`
  - `buildClassName`：`.Name` 拼接 package，无点短名补 `package.`，完整类名必须以
    小写 ASCII 字母开头；空名与其它完整名明确失败。
  - `parseApplication` / `parsePackageItemInfo`：Application、Activity 与 alias 的
    `android:name` 均走同一类名归一化；组件名缺失属于 malformed manifest。
  - `parseActivityAlias`：`targetActivity` 必填、按同一规则归一化，且只能指向此前已
    声明的 Activity；alias 作为独立 ActivityInfo 保留自己的 enabled/filter 与 target，
    alias 的 enabled 不继承 target Activity 的 enabled。
- `.local/asop/framework/base/core/java/android/app/ActivityThread.java`
  - `handleBindApplication`：`LoadedApk.makeApplication` 建立 Application，随后在启动
    Activity 前调用 Application `onCreate`；APS-1 只发布对应类事实，不执行生命周期。

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

## 结果（机器可判定，已达成）

- `AndroidManifestFacts` 现显式发布默认/自定义 Application 类，以及按文档顺序保留的
  Activity/activity-alias、enabled、alias target 和逐个 intent-filter 事实；旧
  `launcher_activity` 兼容字段由同一 resolver 派生，不再单独解析。
- `NormalizeAndroidManifestClassName` 对齐 KitKat `PackageParser.buildClassName`；
  `ResolveLauncherComponent` 只接受同一 filter 内同时出现 MAIN + LAUNCHER 的 enabled
  组件，多个候选固定取 Manifest 声明顺序第一个，alias 返回独立组件名与真实 target 类。
- malformed class/component/alias/enabled 与 no-launcher 通过
  `AndroidManifestStartupErrorReason` 明确分类；alias target 必须在 alias 前声明。
- Windows MSVC：`cmake --preset windows-msvc`、`cmake --build --preset windows-msvc`
  通过；Manifest/launcher 定向 19/19，完整 `ctest --preset windows-msvc` 782/782。
- frontend 未切换；native inventory 与 ABI resolver 仍归 APS-2。
