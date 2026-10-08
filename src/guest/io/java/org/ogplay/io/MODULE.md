# 子模块：System 诊断输出流

LogOutputStream 是 System.out/err 的独立 UTF-8 日志端点；两个普通 PrintStream 包装
两个独立实例，正常运行类初始化与构造。原版 PrintStream 自己的目标、编码、刷新和
错误状态归 Java，不覆盖其普通方法，不让任意 OutputStream 被重定向为 logger。

pending ByteArrayOutputStream、关闭标记和同步归 Java，GC 沿字段追踪。换行/flush/
close 提交非空 UTF-8 文本，空片段不创建日志；每条提交只经 private static native emit。
write 范围/关闭受检，close 幂等；显式 flush 的不完整 UTF-8 沿现有解码替换语义。
原始任意二进制终端或逐字节日志保真不在此结构化诊断端点范围内。
