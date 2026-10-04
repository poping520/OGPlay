# 模块：guest 有界整数算术

源码编入统一 libogplay_jni.so，使用已发布 API19 libcrypto 的不透明 BIGNUM API；
不新增共享库或 JNI_OnLoad。公开 BigDecimal/BigInteger 算法仍归固定 BootDex。
仅支持加、乘、无符号 word 乘、移位、整数幂和商余数，用于十进制数值转换。

商余数返回单个 byte[]：4 字节大端商编码长度，随后商与余数编码；不依赖 JNI 数组类查询。
JNI 只接收带符号的值编码（首字节 0/1，随后大端绝对值），不接收 NativeBN 逻辑令牌。
每次调用创建临时 BIGNUM/BN_CTX，所有出口释放，无第二份持久数值 registry。
宿主保持受检 token 和 owner/GC 生命周期，先快照输入，成功解码全部结果后原子提交；
别名输入安全，失败不改旧值。除法可只提交商或余数，二者同 token 明确拒绝。

输入/结果绝对值最多 1 MiB；按 32 位 limb 保守估算，单次乘/除/幂至多 16777216 次
limb 乘积工作量。左移和幂提前校验结果预算；超限抛 UOE 并由 core 记账。
除零抛 ArithmeticException，非法编码抛 IllegalArgumentException，分配失败抛 OOME。
符号、零归一化、截断除法遵循 API19；指数符号按原版 BN_exp 忽略，word 按 uint32 解释。
未覆盖的 NativeBN 方法仍明确失败，不声明完整大整数或密码学能力。

验证入口：DVM-215 实际 guest JNI 双解释器用例、BootDex native ABI 审计、payload 校验。
