# DVM-88 · NetworkRuntime、VFS SQLite 与 API 19 能力栈收口

## 目标（一句话）

以默认离线、显式注入的 `NetworkRuntime` 发布 socket/datagram/TLS 核心面，并让
ContentValues/Cursor/SQLite 的有界数据语义只持久化到 guest VFS，完成 API 19 能力栈回归收口。

## 依赖

- DVM-80：intrinsic family TU、core ownership 与 catalog/layout gate。
- DVM-81..87：Context、NIO、Java GLES、AudioTrack、调度、值服务与 Java P1 核心。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase F。

## 范围

- core `NetworkRuntime` 保存 endpoint/socket/stream/datagram 的 per-VM 状态，只消费装配方
  注入的 `NetworkTransport`；默认 policy 为离线，host allowlist、TLS 与 datagram 均需显式开启。
- 发布 InetAddress/InetSocketAddress、Socket 输入输出流、Socket、DatagramPacket/
  DatagramSocket、SocketFactory/SSLSocketFactory/SSLSocket 的常用 API 19 可链接面。
- 发布 ContentValues typed value、Cursor 游标与 SQLiteDatabase/SQLiteOpenHelper 的
  open/create、CREATE/DROP、insert/update/delete/query 有界面；数据库采用确定性内部格式，
  只经会话 VFS 读写 `/data/data/<package>/databases/`。
- 为 NetworkRuntime 和 Android database side-table 登记 GC trace/sweep；关闭、回收和会话
  teardown 不泄漏 guest identity 或宿主 transport handle。
- 增加阶段集成夹具，验证既有 NIO、Java GLES、AudioTrack 与新增网络/数据库 catalog 可链接，
  并按 `windows-msvc` 执行完整 configure、build、CTest。
- 按用户授权，本 WU 不受通常 10 文件与 800 行限制。

## 不做

- 默认不访问宿主网络；不提供 DNS 缓存、代理、cookie、证书库、HostnameVerifier 或完整 TLS。
- 不模拟 Android Connectivity/Binder 服务；网络开放只能来自显式 policy 与 transport 注入。
- 不承诺 SQLite 文件格式、SQL parser、join/transaction/index/trigger/pragma/并发锁完整兼容；
  超出登记语法和 selection 子集的调用明确失败。
- 不引入宿主数据库路径、system_server、ContentProvider 或跨进程 Cursor。

## 验收与结果

- 默认离线与 allowlist/TLS/datagram policy 有机器测试；未注入 transport 或未授权操作明确失败。
- `SocketFactory.getDefault/createSocket` 常用面真实进入 policy-gated `NetworkRuntime`；未显式
  close 的 live channel 在 VM teardown 统一关闭。
- ContentValues→insert→query→Cursor 与关闭后从 guest VFS 重新载入有机器测试。
- SQLiteOpenHelper 持久化 schema version，首次打开虚派 `onCreate`、版本增长虚派
  `onUpgrade`；只有 VFS `Stat` 的 ENOENT 被视作新库，其余 I/O 错误明确抛 `SQLException`。
- 阶段 catalog 夹具覆盖 NIO、GLES20、AudioTrack、Socket 和 SQLiteDatabase 链接。
- Windows `windows-msvc` 完整 configure 与 Debug build 通过；验收补强定向 20/20、
  architecture 6/6 通过，全量 CTest 991/991。既有 liblog guest tag 求值顺序问题已随本次
  验收补强修复，未新开 WU。

状态：已完成。
