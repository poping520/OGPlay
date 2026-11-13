# 模块：BootDex IO

API 19 core.jar 提供标准流和 PrintStream。应用进程使用 ROM RuntimeInit 重定向输出，
普通文件描述符由 DexVM 的 Posix 边界接入 guest IO。
