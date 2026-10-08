# 模块：BootDex IO 平台端点

只拥有 IO 到宿主事实的 Java 适配器；标准流算法/PrintStream 来自固定 API19 core.jar。
[日志流](java/org/ogplay/io/MODULE.md) 的缓冲/monitor/关闭归普通 Java 对象，唯一 native
边界将文本写入已注入的结构化 logger。不接入宿主文件、终端 FD、编码或时钟。
