# UTIL-3 · 同构参数化克隆收敛

目标：在不改变公开入口、锁策略、错误文本、异常类别、guest ABI 或热路径语义的前提下，
收敛同一模块内仅由类型、descriptor 或调用策略区分的参数化克隆。

依赖：UTIL-2 已完成基础字节、路径、标识符与解释器公共映射收敛。

验收：

- [x] diagnostics 的阻塞/try 栈快照只共享锁内投影，锁获取与 busy 失败边界保持独立。
- [x] loader 的 ELF virtual-address→file-offset 与唯一 dynamic tag 校验只有一份实现，
  `elf`/`lifecycle` 原错误文本保持不变。
- [x] JNI guest 方法返回与字段读取共享 A32 编码；void allow/reject 策略显式。
- [x] JNI guest Modified UTF-8/UTF-16 lease 共享 arena 空洞分配与配对释放骨架。
- [x] StringBuffer/StringBuilder 共享声明构建器，但保留独立 `Declare_*` 入口。
- [x] 简单 throwable 共享 `()V`/`(String)V` 声明；带专有字段或行为的 throwable 保持显式。
- [x] reflection 三类 member array 与 metadata slot 校验共享强类型私有模板。
- [x] int/long 与 float/double binop 分成两组类型化实现；整数/浮点规则不得混合。
- [x] NIO bulk get/put 只共享范围和 remaining 校验，数据方向保持显式。
- [x] static/instance GetFieldID 只共享 ID lookup 骨架；getter/setter 保持强类型实现。
- [x] Title Profile exact-key 校验与 runtime internal class-name predicate 各有唯一实现。
- [x] 受影响目标编译、直接相关定向测试及架构检查通过；同步更新模块契约与 CURRENT。

非目标：不统一 `RegisterMethod`/`RegisterField`、SSL/plain socket factory、GLES
Viewport/Scissor 绑定、ANGLE uniform 调用、agent/frontend 图片编码 callback，或 JNI field/
invocation 的 `Matches`（void 语义不同）；不改变任何能力声明。
