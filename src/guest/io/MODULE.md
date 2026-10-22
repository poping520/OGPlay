# 模块：BootDex IO 平台端点

只拥有 IO 到宿主事实的 Java 适配器；标准流算法/PrintStream 来自固定 API19 core.jar。
[日志流](java/org/ogplay/io/MODULE.md) 的缓冲/monitor/关闭归普通 Java 对象，唯一 native
边界提交文本、行尾与 out/err 通道事实，交给已注入的 logger/console。不接入宿主文件、终端 FD、编码或时钟。
