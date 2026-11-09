# 模块：guest 时区数据

从只读 guest VFS 读取固定 API 19 tzdata，按索引定位 TZif 记录，由原版 ZoneInfo
解析和查询偏移。归档按 VM 惰性读取。保留既有固定偏移时区与进程默认时区协议。
不访问宿主时区库，不提供 mmap 或外部时区更新。
