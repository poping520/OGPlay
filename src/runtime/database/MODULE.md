# 子模块：runtime/database

拥有固定 SQLite amalgamation 的 connection/statement 资源及 SQLite VFS adapter。主库、
rollback journal 和临时文件只经唯一 guest VFS 读写；不依赖 DexVM/JNI/integration，不取得
宿主路径。POD 文件句柄使用逻辑 FD，主库、journal 与锁使用同一 VFS 规范文件身份；打开
模式落实只读、读写及按需创建。SQLite 错误保留原始错误码供 Android 层映射。时间由调用方
注入统一 Clock，随机字节由生产装配注入 OS CSPRNG。`:memory:` 不创建 VFS 文件；
ATTACH/DETACH 与 WAL 由 authorizer 明确拒绝。数据库格式只由 SQLite 引擎判断；
损坏错误保留原码交给 Android 默认损坏处理器。
不提供旧格式识别、迁移或回退。跨进程共享和断电耐久性未支持。
