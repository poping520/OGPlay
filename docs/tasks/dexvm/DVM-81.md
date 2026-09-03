# DVM-81 · API 19 Context 类型体系

## 目标（一句话）

恢复 API 19 的 `ContextWrapper`/`ContextThemeWrapper` 与 app 组件真实继承链，使现有
Context 能力通过稳定 base Context 委托，并由 Activity 生命周期完成一次性绑定。

## 依赖

- DVM-80：intrinsic 已按 API family TU 收敛，Android class 声明仍保持独立边界。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase B。
- `src/runtime/integration/dexvm_android/MODULE.md` 与 session lifecycle 契约。

## 范围

- 新增 `ContextWrapper`，保存强引用 base Context，提供一次性 `attachBaseContext`、
  `getBaseContext`，并把当前已支持的 Context 方法虚派委托给 base。
- 新增 `ContextThemeWrapper`，保存有界 theme resource id；修正
  `Application`、`Activity`、`Service`、`IntentService` 的 API 19 superclass。
- launcher 与 Activity handoff 均在构造后、`onCreate` 前绑定进程 base Context。
- 用机器测试锁定 superclass/assignability、base identity、重复 attach 异常、委托、theme
  状态及既有 Activity lifecycle/VFS/VideoView 行为。
- 本 WU 延续阶段授权，不受通常单 WU 10 文件限制；family TU 继续适用模块行数例外。

## 不做

- 不实现 Instrumentation、ActivityManager、service process、Binder 或完整 theme/resource
  framework。
- 不扩张 Context service 集合，不增加 title-specific 分支。
- 不进入 NIO/direct-buffer；不运行全量 CTest，全量回归留到阶段最后一个 WU。

## 验收

- 类型链严格为 `Context <- ContextWrapper <- {Application, Service,
  ContextThemeWrapper <- Activity}`，`IntentService` 继承 `Service`。
- wrapper 方法经同一 base 对象虚派；空 base 明确失败，重复绑定抛
  `IllegalStateException`，guest 引用由普通实例字段进入 GC 对象图。
- Windows `windows-msvc` configure/build 通过，Context/反射/生命周期及 architecture
  定向回归全绿。
- MODULE、设计基线、任务索引、CURRENT 与 `capabilities.toml` 同步；能力保持 partial。

## 结果

- 已发布真实 API 19 Context 类型链及 `Service`/`IntentService` class shape。
- Application 与每个 Activity 均在生命周期入口绑定同一进程 base Context；现有
  package/resource/service/VFS 等 Context 行为不复制状态，只经 base 虚派委托。
- `ContextThemeWrapper` 仅保存可查询的 theme resource id，未承诺完整 theme 行为。
- Windows Debug 全目标构建及 DVM-81 定向回归通过；全量 CTest 按计划未运行。
- 2026-09-03 后续补齐 `Context.openFileInput/openFileOutput` 与
  `MODE_PRIVATE/MODE_APPEND`：文件名按 API 19 拒绝路径分隔符，流对象复用 core
  `FileInputStream/FileOutputStream` 和唯一 `IoRuntime`/VFS；`ContextWrapper` 继续只委托
  base Context。覆盖、追加、读取、缺失文件和非法名称均有双后端回归。
- 2026-09-03 对齐 AOSP 4.4.4 补齐 Context 所有的 final `getString(int)`：先在运行时
  receiver 上虚派 `getResources()`，再调用 `Resources.getString(int)`；wrapper/component
  只继承该方法。字符串与引用走唯一 ARSC resolver，缺失、环与错类型抛
  `Resources.NotFoundException`，继承归属、UTF-8、别名及失败路径均有定向回归。

状态：已完成。
