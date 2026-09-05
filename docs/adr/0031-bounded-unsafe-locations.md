# ADR-0031 · API 19 Unsafe 逻辑位置与原子性

- 状态：Accepted
- 日期：2026-09-05
- 关联：[DVM-101](../tasks/dexvm/DVM-101.md)、[ADR-0030](0030-api19-curated-boot-dex.md)

## 背景

精选 BootDex 的 AtomicInteger、AQS、LockSupport 直接调用 `sun.misc.Unsafe`。
API 19 参考是 libcore `libdvm/src/main/java/sun/misc/Unsafe.java` 与 Dalvik
`vm/native/sun_misc_Unsafe.cpp`，不是 libart 或现代 JDK 的 Unsafe。
OGPlay 的 Java 对象以强类型句柄、带标签槽和 JNI 数组存储表示，不能把 Dalvik 的
宿主内存指针运算照搬到这些存储上。

## 决定

- 类声明与 handler 归现有 `java_concurrent.cpp`；每 VM 的 `UnsafeRuntime` 负责位置解析
  和共享堆读写。它只持 `VmFieldId` 元数据，不持宿主地址、guest 对象或反射 wrapper。
- `objectFieldOffset` 返回从 `2^48` 起的稳定逻辑令牌，按首次查询登记、重复查询复用；
  令牌不是字节偏移，不支持字段地址算术。使用时校验已登记、接收者继承关系和精确字段类型。
  反射对象被回收不影响令牌；static 字段拒绝。令牌仅在所属 VM 内有效，不作跨 VM ABI。
- 数组使用逻辑 base 16 与 API 19 A32 元素 scale（1/2/4/8，引用 4）；只支持对齐的
  int/long/reference 元素操作。读写复用原 JNI primitive/object array store。
- int/long/reference 的 plain、volatile、ordered 和 CAS 全部在 `VmExecutionLock` 内。
  CAS 读写之间不分配、不回调、不停泊；锁的 acquire/release 保证跨 guest 线程可见性，
  plain/ordered 获得比最低要求更强的顺序。未来解除解释执行串行化时必须重审此决定。
- 引用比较按身份；写入保留 `SlotTag::ref` 或数组强边，GC/JNI/DEX 不建立影子状态。
  为保护唯一类型化堆，引用写入校验可赋值性，拒绝类型重解释及任意字节访问。
- `THE_ONE/theUnsafe` 为同一静态强根；`getUnsafe` 根据实际 interpreted caller 的 loader
  限制 application 调用，无 guest caller 的宿主入口按 AOSP null loader 处理。
- `park/unpark` 调用现有 Thread 方法，复用许可、monitor、interrupt、shutdown 与 Clock。
  absolute park 从注入 epoch Clock 转换为现有 Thread 的单调 deadline；缺任一时间源明确
  失败，计算溢出拒绝，不读取宿主时钟。relative nanos 保持 Thread 的向上取整与负数异常。
- `allocateInstance` 先完成真实类初始化，再分配零值字段对象，不执行实例构造器；
  primitive/array/interface/abstract 类拒绝，初始化异常保留原 guest identity。

## 边界

不提供宿主指针、任意内存分配/复制、static field offset、类型混淆、跨对象越界、
任意 class 定义或 ART/JDK 扩展。API 19 并没有这些额外方法；未发布方法继续按 VM
缺口机制记账失败。本项不代表 Executors 并行线程池或整款游戏已经验收。
