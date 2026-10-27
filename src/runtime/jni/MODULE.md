# 子模块：runtime/jni

JNI 反射转换由 JniEnvironment 的显式 hooks 接入 VM owner。四个 From/ToReflected
入口复用引用表及 pending exception 约束；无后端明确失败。回调在锁外调用，owner
必须在停止所有调用后清除 hooks；返回对象由 owner 发布为当前线程 local reference。

## 职责

实现完整 JNI/JavaVM ABI 目录及 M3 常用行为，包括类型、引用、异常、签名、Modified UTF-8、
字符串、数组、对象、类、字段、方法调用、native 注册、标准 native 导出名和线程附着。

## 依赖

只依赖 `core` 与标准库；对象身份可以表示宿主对象或未来 VM 对象，但不得依赖 framework、
Bionic、syscall、execution 或 integration。

## 不变量

- `JniClassRegistry::FindClass` 为被动查询；`ResolveClass` 可经 owner 的显式 resolver
  按需链接/发布类。回调在注册表锁外执行，owner 停止调用后撤销，不启动全量物化。
- `ThrowNew` 可由 owner 的 ThrowableFactory 发布真实 local Throwable，保留异常类/消息
  元数据；无 VM 时保留独立 JNI 表示。factory 在锁外执行，真实新抛出的异常可按 Dalvik 替换原 pending。
- 字段 owner 用 `JniFieldAccessPending` 表示已挂起 Java 异常；禁止据此回落零值侧表。

- DexVM 存在时 AllocObject/NewObject* 唯一经 owner 分配回调进入共享对象模型；
  registry 分配仅用于无 VM 的 HLE 环境，AllocObject 无后端仍明确失败。引用、线程作用域
  在本层校验，pending 按 ADR-0153 放行并记诊断。回调在独立锁外执行、发布当前线程 local reference；无后端明确失败，
  owner 在调用静止后撤销。本层不复制类布局或执行构造器。
- 233 个 JNIEnv 槽与 8 个 JavaVM 槽的索引稳定。
- 未绑定槽记账并 trap；引用类别和线程作用域保持严格。ADR-0153 按 Dalvik 非 CheckJNI
  默认放行 pending 下的 JNI 调用，不保留严格开关。每个槽×guest 线程×异常类第一次发布
  结构化 warn（slot/thread/class/guest LR），可查询并交显式 owner observer 记 warning ledger。
  guest transport 标记真实入槽，槽内 helper 不重复记成新调用，独立 HLE LR=0；同组合不重报。
- Push/PopLocalFrame 成功保留原 pending 身份；Throw 显式替换，ExceptionOccurred/Check
  不清除，Clear 清除；Pop 下溢在 guest ABI 清 pending 后挂 RuntimeException、仍发布 result；
  新的真实 Java 异常可替换旧 pending。特殊入口清单见 ADR-0153。
- 已解析方法缺少 implementation handler 时，错误必须携带规范 implementation ID，禁止
  丢失定位所需的注册表身份。
- guest handle 保持固定宽度，不暴露宿主指针。引用 handle 低位编码类别，表项决定存活性；
  GetObjectRefType 不解引用 handle，严格模式下已删除 local 按 Dalvik ReportJniError 致命失败。
- JniStringStore.VisitAccessRoots 在锁外枚举仍有 chars/UTF8/critical lease 的字符串身份；
  VM owner 将其纳入 GC roots，直至对应 Release，不因局部引用或线程退役提前删除。
- 默认严格执行 local reference 生命周期。仅由上层根据 Manifest `targetSdkVersion` 1..13
  显式启用 Dalvik app-bug 兼容：同一对象复用稳定 direct-style handle，失效 handle 可解析但
  不属于 GC roots；首次跨 frame 重发必须通过回调警告，不能静默兼容或取消 frame 清理。
- AttachCurrentThread/AsDaemon 的 daemon 事实随 attachment 保存，重复 attach 保持原值，
  detach 后再次 attach 可重新选择；可选 Modified UTF-8 名称随 attachment 保存，上层按码元物化 Thread（保留 NUL/代理对）。
  JavaVM detach 在状态变更前调用 owner 的显式 guard，Java 帧未退出时致命失败；
  owner 停止调用后撤销 guard，环境 teardown 不走此 JavaVM 入口检查。
- EnsureLocalCapacity/PushLocalFrame 的请求语义按 Dalvik 512（扣除当前 local 使用量），
  实际使用量仍自动增长到 `local_per_thread` 总上限；请求失败由 guest ABI 发布 OOME，
  Push 失败先清除 pending，成功保留 pending；实际分配超过总上限仍明确失败。
- ExceptionDescribe 经 owner 的显式 describer 调用 Java printStackTrace，暂清 pending；
  原 throwable 在调用期间作为 GC 强根，忽略打印的 Java 异常并恢复原身份。hook 在锁外调用，
  owner 停止调用后撤销；宿主致命故障恢复原 pending 后继续上报。独立 JNI 保留结构化诊断。
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
CopyWithin 只移动同一 typed array 的现有元素：在数组锁内检查完整范围、处理双向重叠，
保留 null/object/class 身份；不引入外来值、不重新进入 VM assignability 回调。

`JniReferenceTable::TrySnapshot` 只 try-lock 复制 local/global/weak_global 与 attached thread 数；不创建引用、不遍历 guest 对象，忙时返回 nullopt。

## 稳定 payload 接口（ADR-0152）

`JavaPayloadProvider` 只定义强类型 block/borrow、typed accessor 与统计接口，不依赖
AddressSpace 实现、DexVM 或 OS。integration 显式注入 owner；句柄含 generation 和 owner
身份，typed view 一次验证所有权/字节范围/对齐，每个元素只检查视图边界并访问稳定 backing。
view 保活 block/provider，活跃 view 或 pin 阻止 Release/reuse；不公开裸宿主指针或 span，
owner 串行化 view 构造和释放。copy/move view 不撤销保活，pin 计量唯一字节和借用次数。
跨线程遵循普通 guest 内存同步，未承诺额外内存序。

primitive store 必须显式注入 provider：生产使用 integration 的 guest provider，独立夹具和
headless 显式选择 `HostCopyJavaPayloadProvider`（Address 明确失败）。entry 只保存 kind、
length、handle 和长期 typed view；guest block 是唯一权威字节，Region/clone/arraycopy/fill
从同一 view 批量搬运，先校验完整区间，CopyWithin 用 memmove 处理重叠。
Address 只查询 payload guest 地址、不 pin，调用方须维持数组可达；host-copy 明确失败。
Elements/Critical 直接返回 payload guest 地址、isCopy=FALSE；零长也返回有效地址。
pin 计数归 store，以数组身份计数、两类入口共用；重复获取同址并加一，不绑定线程，detach 不清除。
Release 忽略指针和 access kind，COMMIT 无操作，其余 mode 仅减一，不回滚写入；无配对返回诊断结果，
guest owner 记 warn/ledger 而不中止。每数组首次超过 10 次 pin 告警；停止/join 线程和 CPU 后统一
退役残留 pin 并诊断数组数/字节数。host-copy provider 无 guest 地址，直接借用明确失败。
VisitAccessRoots 在锁外枚举非零 pin 的数组身份，owner 在 mark/weak 处理前纳入强根。
删除前退役所有 view，再 Release；外部 view 或 borrow 尚存时明确失败，不能吞掉。

显式 allocation context 在 store 锁外进入 VM 执行锁并收费；Java/JNI New 共用一次 Reserve，
导入只补未收费的 entry。失败撤销费用、目标增长和 emergency 消耗，再退役 block；删除
归还同一费用。provider 本身只管理 backing 提交/VAS，不持第二逻辑堆目标。上下文只在
调用静止时装配/撤销。分配时统计 kind/长度桶、峰值 live 和失败次数；诊断只返回聚合快照。
StringStore 同样显式注入 provider，entry 只持 UTF-16 handle/长期 typed view，所有读取从同一
不可变 backing 生成值；无长期 vector 或影子 String.value。公共 char[]、builder 与 substring
构造继续复制为新 block。通过 const JNI 指针写入 String 是非法使用，不提供写保护或别名验收。
Chars/Critical 同址直接借用、FALSE，以 String 身份共用跨线程 pin；空串有独立有效地址。
Release 忽略指针与 Chars/Critical kind，只解除一次 pin；无配对由 guest owner warn/ledger，
首次 pin>10 告警一次。pin 和 UTF8 lease 分开计数，任一非零时保活 String 及 backing，
比 Dalvik 仅 pin char[] 更保守；detach 不释放，quiesce/join 后统一退役并诊断残留 pin。
MUTF-8 编码仍复制、TRUE，token/指针配对与预算不变，UTF-16 不占用 copy arena/预算。

String 创建沿用共享 JavaPayloadAllocationContext；默认收对象头 32 B + payload，绑定已收费
实例时 Create(include_header=false) 只收 payload。JNI 导入只补尚未收费项；Delete 返回实际
退还费用，VM sweep 仅扣除仍归实例的原对象头/字段费用，避免重复扣账。provider 失败回滚
同一 VM 费用/target/emergency，所有读取、hash/equals/intern 键都来自同一 UTF-16 内容。

VM 装配通过 ConfigurePayloadHeap 传 maximum/growth 到 provider 的 backing 策略，
allocation context 另提供显式 emergency 状态查询；New 在已进入上下文/收费后传给
provider.Allocate(bytes, emergency)。host-copy 默认忽略 backing 策略，仍只收 VM 一份费用。
provider 的应急统计与普通 committed/slab_free 分开表示，应急池耗尽明确失败。
