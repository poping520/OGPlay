# 01 · 目标、非目标与成功标准

## 1. 要解决的结构性问题

当前 APK 启动仍有两个耦合点阻碍通用化。

**问题 A：启动资格由 Title Profile + `.so` 哈希决定。**
APK 已经携带 package、组件、DEX 和 native library 清单，但 frontend 仍需先命中
Profile，才能知道“root `.so`”并继续启动。新增游戏因此必须先做人工作品适配，
即使其启动逻辑完全符合 Android 常规模式。

**问题 B：native 装载时机与 Java 语义倒置。**
当前路径在 DexVM 生命周期启动前装入应用 root `.so`，随后由 frontend 显式执行
`JNI_OnLoad`；而真实 Android 应用通常由 Application/Activity 的 `<clinit>`、
`onCreate()` 或更晚代码执行 `System.loadLibrary`。这会漏掉“何时加载、由哪个
ClassLoader 请求、加载失败如何反馈给 Java”等语义。

本任务把启动控制权从 Profile/root-so 交还给 APK 自身。

## 2. 目标

1. **无 Profile 也能进入通用启动路径。** Manifest、DEX、APK native inventory
   足以描述的事实不再人工重复声明。
2. **在调用 APK Application/launcher Activity 前准备好 DexVM 及 JNI 环境。**
   这里的“先”指 Java 入口执行顺序，不要求 DexVM 是整个 session 第一个对象。
3. **最小 Application 初始化进入本任务。** 自定义 `Application` 的 `<clinit>`、
   `onCreate()` 可能加载 native library 或建立后续 Activity 依赖的全局状态。
4. **应用 `.so` 按真实 Java 执行点动态加载。** 同时支持 `System.loadLibrary(name)`
   与 `System.load(path)`，不预扫描 DEX 猜测 native 依赖。
5. **一个进程只选一个 guest ABI。** ABI 来自宿主支持能力与 APK 实际内容的交集，
   不由 Profile 选 root `.so` 间接决定。
6. **Title Profile 降级为可选兼容覆盖。** `so_sha256` 不再是通用启动必需字段；
   它最多作为某条 quirk/profile 的适用性 guard。

## 3. 非目标（本任务明确不做）

| 非目标 | 说明 |
| --- | --- |
| 完整 `ActivityThread` | 只复刻本任务需要的启动语义，不搬运 framework 实现结构 |
| framework/core DEX 执行 | `android.*` / `java.*` 仍由 DexVM intrinsic/HLE 提供 |
| ContentProvider 启动排序 | Provider-before-Application 留给后续兼容需求单独立项 |
| Instrumentation | 不建立 `Instrumentation.newApplication/newActivity` 全体系；用 OGPlay 等价启动动作 |
| Service/BroadcastReceiver 生命周期 | 不属于 launcher 启动闭环 |
| 多进程组件 | `android:process`、remote service/application process 暂不实现；命中时明确失败/记账 |
| 多 ClassLoader / DexClassLoader | 继续遵守 DexVM 单一应用类命名空间边界 |
| 自动静态扫描 native load | 禁止从 DEX 字符串或调用图“猜”要预加载哪些 `.so` |
| Play 服务/支付/现代在线服务 | 项目既有非目标不因本任务改变 |

## 4. 必须保持的不变量

- **I1：APK 自描述优先。** package、launcher、Application、native inventory、ABI
  候选首先来自 APK；Profile 只能覆盖不能成为必需输入。
- **I2：DexVM ready before app Java entry。** Application/Activity 任一应用方法执行前，
  DexVM 类链接、intrinsic、JNI bridge 与 Context/Resources 基础环境已可用。
- **I3：no app `.so` preload。** process shell 阶段不得以“root module”名义预装应用库。
- **I4：one process, one ABI。** 同一 `AndroidAppProcess` 不允许混装不同 guest ABI。
- **I5：explicit load ≠ dependency load。** `DT_NEEDED` 依赖不能自动获得“Java 显式
  加载一次”的 VM 语义；尤其不得无依据对每个依赖调用 `JNI_OnLoad`。
- **I6：reentrant JNI。** `<clinit>` → loadLibrary → JNI_OnLoad → Java callback 的
  同线程嵌套必须可工作，不得因持有非重入锁死锁。
- **I7：明确失败。** 找不到 launcher、ABI 不兼容、library 不存在、JNI_OnLoad
  失败等必须返回可定位错误，不回退到 profile 猜测或静默 no-op。

## 5. 成功标准

完成本任务后，至少满足：

1. 一个不提供 Title Profile 的测试 APK 能按 Manifest 找到 Application 和
   MAIN/LAUNCHER Activity，并依次执行 Application `onCreate` 与 Activity
   `onCreate/onStart/onResume`。
2. 测试 APK 在 Application `<clinit>`、Application `onCreate`、Activity `<clinit>`、
   Activity `onCreate` 四种位置调用 `System.loadLibrary` 均能加载目标库。
3. `System.load(path)` 与 `System.loadLibrary(name)` 都有正例和失败负例。
4. 同库重复加载幂等；`JNI_OnLoad` 只按 explicit load 语义执行一次。
5. native `JNI_OnLoad` 回调 Java，再由 Java 嵌套加载第二个 library 的重入夹具通过。
6. `run-apk` 不再因“没有 exact Title Profile / 没有 so_sha256 匹配”直接退出。
7. 现有 profile title 仍可通过 compatibility override 运行，且 exact Scenario
   不因重构出现无解释回归。
