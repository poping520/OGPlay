# 04 · Manifest、Application 与 Activity

## 1. Manifest 是组件入口真源

APK 启动默认只依赖 Manifest 中的组件事实：

- package name；
- `<application android:name>`；
- `<activity>` / `<activity-alias>`；
- intent-filter 中 `android.intent.action.MAIN` + `android.intent.category.LAUNCHER`；
- 组件 enabled 状态与必要的类名归一化信息。

Profile 可以覆盖 launcher 作为兼容 quirk，但不得成为“没有它就不知道入口”的常规
路径。

## 2. 类名归一化

Manifest 组件类名必须统一转换为 Dex descriptor 前的 Java fully-qualified name。
实现语义对齐 KitKat `PackageParser.buildClassName`：

```text
.name      → <package>.name
Name       → <package>.Name
x.y.Name   → x.y.Name
```

归一化只做语法拼接，不猜测不存在的类。最终类必须在 DEX/intrinsic 可解析，否则
启动明确失败。

## 3. launcher 解析

默认选择满足 MAIN + LAUNCHER 的 enabled 组件。

要求：

1. 直接 `<activity>` launcher 必须支持；
2. `<activity-alias>` 必须在 Manifest model 中保留 alias 与 targetActivity 的关系；
3. 多个等价 launcher 时不得依赖 ZIP/解析器偶然顺序静默选一个；应采用与 Android
   manifest 声明顺序兼容的确定性规则，并以测试固定；
4. 没有 launcher 时返回专门的 startup error，不回退为“第一 Activity”；
5. Profile `launch_activity` 仅作为显式兼容覆盖，并验证目标类存在。

若当前 Manifest parser 尚未暴露 alias/enabled/filter 细节，先扩 loader model，再做
lifecycle；不要在 frontend 二次解析 XML。

## 4. Application 类解析

`<application android:name>`：

- 缺省：逻辑类为 `android.app.Application` intrinsic；
- 有值：按 §2 归一化并从应用 DEX 查找；
- 类不存在、构造失败或 onCreate 抛未捕获异常：启动失败。

Application 实例是 process 级对象，应保存在 `AndroidAppProcess`，并由 Activity
Context 能获得同一 application identity（具体 intrinsic API 按 DexVM 现有模型接线）。

## 5. 最小 Application startup

本任务采用以下等价语义，不实现完整 `ActivityThread.handleBindApplication`：

```text
resolve Application class
  ↓
EnsureClassInitialized(ApplicationClass)
  ↓
allocate instance
  ↓
invoke <init>()V
  ↓
attach base Context
  ↓
invoke onCreate()V
  ↓
store process Application instance
```

“attach base Context”可以由 OGPlay intrinsic/host bridge 实现等价动作；不要求执行
framework DEX 中 `Application.attach()` 方法体。

`onCreate()` 必须走真实动态分派，因此自定义 Application override 被执行。

## 6. launcher Activity startup

Application 成功后：

```text
resolve launcher target
  ↓
EnsureClassInitialized(ActivityClass)
  ↓
allocate instance
  ↓
invoke <init>()V
  ↓
attach package/application/context state required by intrinsics
  ↓
onCreate(Bundle|null)
  ↓
onStart()
  ↓
onResume()
```

现有 `DexActivityLifecycle` 已覆盖其中大部分，应优先扩展/组合而不是复制第二套 Activity
生命周期实现。

## 7. 为什么 Application 必须与本任务一起做

如果只把 DexVM 提前、随后直接 Activity：

- 自定义 Application `<clinit>` 的 `System.loadLibrary` 不会发生；
- Application `onCreate` 的 JNI 注册/全局 singleton 不会建立；
- Activity 失败后又会被迫在 frontend 补 title-specific 初始化。

这会让“Java 驱动 native load”只完成一半，并导致下一轮再次重构启动 orchestrator。
因此最小 Application startup 是本任务硬范围。

## 8. 本阶段不扩展的生命周期

以下命中时按 capability gap 处理，而不是偷偷实现部分语义：

- ContentProvider install/`onCreate` 排序；
- Instrumentation callbacks；
- `Application.onConfigurationChanged/onLowMemory/onTrimMemory`；
- component-specific process；
- activity task/back stack 完整模型；
- configuration relaunch。
