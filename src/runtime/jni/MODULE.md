# 子模块：runtime/jni

## 职责

实现完整 JNI/JavaVM ABI 目录及 M3 常用行为，包括类型、引用、异常、签名、Modified UTF-8、
字符串、数组、对象、类、字段、方法调用、native 注册、标准 native 导出名和线程附着。

## 依赖

只依赖 `core` 与标准库；对象身份可以表示宿主对象或未来 VM 对象，但不得依赖 framework、
Bionic、syscall、execution 或 integration。

## 不变量

- 233 个 JNIEnv 槽与 8 个 JavaVM 槽的索引稳定。
- 未绑定槽记账并 trap；引用类别、线程作用域和 pending exception gate 必须严格检查。
- 已解析方法缺少 implementation handler 时，错误必须携带规范 implementation ID，禁止
  丢失定位所需的注册表身份。
- guest handle 保持固定宽度，不暴露宿主指针。
- 默认严格执行 local reference 生命周期。仅由上层根据 Manifest `targetSdkVersion` 1..13
  显式启用 Dalvik app-bug 兼容：同一对象复用稳定 direct-style handle，失效 handle 可解析但
  不属于 GC roots；首次跨 frame 重发必须通过回调警告，不能静默兼容或取消 frame 清理。
- local frame 的 attach/push 容量是 JNI 保证值而非硬上限；可自动增长到按线程
  `local_per_thread` 总上限，超过总上限仍明确失败。
- JNI monitor 按强类型 object identity 隔离 owner guest thread、recursion 与 waiters；同线程
  可重入，非 owner exit 明确失败。thread detach 释放其全部 ownership。
- `JniEnvironment::SetMonitorHooks` 可由拥有 Java 对象模型的上层安装唯一 monitor
  后端；enter/exit/detach/interrupt/shutdown/snapshot 全量委托，回调在 hooks 锁外执行，
  因而可阻塞。未安装时继续使用本模块默认表，runtime/jni 不反向依赖 DexVM。
- monitor 的临时中断与永久关闭是两个语义，不共用 sticky boolean。`InterruptWaiters` 提升
  interrupt generation 并唤醒全部当前 waiter，被唤醒者以 `interrupted` 失败且不得取得
  ownership，之后新的 `Enter` 使用新 generation 仍可正常竞争；`Shutdown` 只在 table 即将
  永久销毁时调用，唤醒全部 waiter 并让当前与后续 `Enter` 一律以 `shut_down` 失败。
- `BuildJniNativeExportNames` 严格验证 class/method/descriptor，并按 JNI 规范同时产出
  short 与含参数 descriptor 的 long 名；Unicode 必须先转为 UTF-16 code unit 再转义。
- object descriptor、DexVM internal class name 与 JNI native signature 共用
  `IsValidJniObjectClassName`，禁止各入口漂移出不同的斜杠/分段规则。
- class registry 保存 direct interface 图；assignability 遍历父类与接口闭包。interface
  `GetMethodID` 沿 super-interface 查找，虚派仍由 receiver 实际类选择实现；static field
  可按 Dalvik 规则从 interface 查找，instance field 不跨 interface。

## 测试

对应 `tests/runtime/jni*_tests.cpp`、字段/数组/对象/JavaVM 契约测试。

DVM-105：PendingExceptionMetadata 读取 pending identity，不清除异常或新增 local ref。
ThrowNew 提供原类与 modified-UTF8 消息，Throw 保留原 identity；上层据此传播异常。

DVM-106/111：ObjectArrayStore 的类型兼容性通过显式 SetAssignability 回调交给 VM，
覆盖普通类实现接口和 synthetic 数组协变，JNI 层不反向依赖 DexVM。回调在数组锁外执行；
true/false 是最终结果，nullopt 或未安装时，host 类使用 registry 父类链、synthetic 类
保持严格身份比较。只在 guest 启动前安装，停止线程后撤销，不允许与数组读写并发修改回调。
initial/set 共用校验，不兼容写入保留原元素并携带源/目标类身份明确失败。

`JniReferenceTable::TrySnapshot` 只 try-lock 复制 local/global/weak_global 与 attached thread 数；不创建引用、不遍历 guest 对象，忙时返回 nullopt。
