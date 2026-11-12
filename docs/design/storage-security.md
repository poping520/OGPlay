# 存储、数据库与安全边界

存储子系统为 guest 提供唯一文件身份、描述符和每安装实例的持久沙盒。
Java IO、syscall、SQLite 与媒体读取共用 VFS；密码与 TLS 使用 guest 后端，
通过同一文件和网络边界取得来源，不继承宿主的路径或信任设置。

## VFS 的对象关系

```text
guest path / alias → File 节点 → backing
                         ↑
FD → OpenFile(offset, mode, lock)
                         ↓
                    overlay / SandboxStore
```

File 是稳定节点，path 只是可变名字。
OpenFile 保存一次打开的 offset 和访问模式，FD 表持有它的共享引用。
dup 共享打开状态；独立 open 有独立 offset。
关闭从表中移除 FD 后，在途操作仍绑定旧 OpenFile。
同一个 FD 数字复用不能把旧结果交给新文件。

文件内容版本随写入和截断变化，不随名字变化。
别名与规范路径共用节点、锁和数据库 identity。
缓存或 lease 应使用节点身份、版本和窗口，不能仅用路径字符串。
ReadAt/WriteAt 不改变顺序 offset，普通 IO 在同一打开状态串行。

## 路径与来源

路径索引按 ASCII case-fold 查找，逐目录处理点段与父目录。
guest 的父目录不会越过虚拟根。
挂载条目和宿主 backing 仍拒绝 traversal、符号链接及歧义。
相对路径从 guest 进程 cwd 解析，不使用宿主 cwd。

VFS 核心不解析 ZIP/OBB，不读取 Java 对象。
Loader 或上层提供有界 positioned reader 和寿命对象。
小窗口读取不会因为来源很大就默认整份物化。
底层 APK、OBB 与外部来源只读，写入进入可写节点或 overlay。

宿主文件首次打开绑定来源身份，闲置句柄受预算和 LRU 管理。
活动 FD、lease 与读取保活句柄，不能为回收预算关闭仍在使用的文件。
重新打开核对身份和尺寸，来源变化明确失败。
不保证宿主在 VFS 外原地修改文件时仍有快照一致性。

## 锁与读取 lease

锁序为打开状态、节点、全局元数据。
全局锁不跨 backing I/O、CRC 或解压执行。
文件内容与路径发布各有对应锁，不能为缩锁留下未保护的状态。

VfsReadLease 固定来源版本和区间。
只读来源共享 backing；可写来源在统一资源预算内复制窗口。
lease 独立于原 FD 关闭、unlink 或 rename。
取消 token 继续传到来源 reader。
媒体使用 lease，不反向查宿主路径。

文件映射的来源保护与普通媒体快照不同。
CaptureMappingSource 持有稳定节点并阻止冲突修改。
保护登记与写入拒绝必须在同一锁域排序。
这不意味着完整 POSIX mmap 或跨进程锁已经实现。

## 每安装实例沙盒

```text
sandbox/<installation-id>/
  meta.toml
  internal/
  external/
  obb/
  sdcard/
```

installation id 由安装层分配，package 仍保存真实身份。
同包副本有独立存档、设置与 ANDROID_ID。
导入可以复用已移除库条目留下的同包沙盒，否则原子占用最小空闲编号。
多实例裸启动需要显式选择，不能随便挑一个目录。

内部数据、应用 external、OBB 和其他 sdcard 路径映射到不同语义根。
专用根优先，映射集中在 SandboxStore。
文件名编码、tombstone 与配额也是同一实现的职责。
Java、native 和数据库不能各建一套沙盒目录规则。

schema 与 meta 不匹配时明确失败，不自动迁移旧布局。
四个语义根不可删除，缺失时可重建空根并警告。
根被文件替代、旧 schema 或未知混合布局继续拒绝。
Clear 原地清空内容，保留 meta 和实例身份。

close/fsync/pause/clean stop 触发对应持久化。
元数据操作按既定规则即时落盘。
unlink 后的存活句柄不应在关闭时复活旧路径。
底层挂载来源永不反写，配额不是宿主物理剩余空间保证。

## SQLite

ROM SQLite Java 管理连接池、事务、Cursor 与异常协议。
native 仅处理 ABI、资源 token、错误码和受审配置。
runtime/database 拥有固定真实 SQLite 引擎和 VFS adapter。
数据库格式、页校验和损坏判断由真实引擎执行。

主库、rollback journal 和临时文件全部走唯一 VFS。
锁以规范文件 identity 为键，别名不能绕过互斥。
时间由 Clock 注入，随机由统一 CSPRNG 提供。
内存库不创建文件，ATTACH/DETACH 和 WAL 明确拒绝。

CursorWindow 有界，填充按 requiredPos/countAllRows 协议执行。
损坏交给原版处理器，不自建旧格式识别和迁移器。
teardown 取消与关闭资源，不能吞掉仍在使用的 statement。
跨进程共享与断电耐久性不在当前声明范围。

## Java IO 与网络边界

File、stream 和 channel 的普通算法来自 ROM。
Posix native 把真实 VFS errno 传给 ErrnoException。
逻辑 FileDescriptor 不包含宿主 fd。
接口查询描述符与普通文件描述符分开，不因数字合法就接受任意 IO。

NetworkRuntime 只通过注入 policy/transport 访问网络。
默认离线，不读取宿主 DNS、代理、网卡或证书。
只读回环接口事实来自统一进程拓扑，不代表在线传输授权。
URL 解析不触网，实际 DNS/connect/I/O 再检查策略。

## KeyStore 与密码资源

原版 JCA 与 KeyStore API 分派到自有 Provider/SPI。
BKS 条目和 codec 状态保存在 guest Java 对象图，C++ 不建影子条目表。
流所有权仍归调用者；持久化依赖正常 Java IO/VFS。
store 写流不等同于任意输出上的原子替换。

BKS 的版本、KDF、MAC 和 key 保护按已声明格式处理。
不能以现代 JDK 默认格式替代 API19 BKS。
证书和密钥编解码使用真实密码原语，失败区分口令、损坏和未支持能力。
日志不能输出密钥、密码或敏感临时内容。

私有 guest JNI 连接随附 libcrypto。
Java 保存逻辑 token，native owner 管实际资源。
GC、显式释放与 teardown 共用关闭事实。
AndroidKeyStore 不由 BKS 冒充，也不宣称硬件密钥保护。

## TLS 与信任

OGPlayJSSE 连接原版 JSSE API 和 guest libssl/libcrypto。
TLS 通过内存 BIO 与 NetworkRuntime raw channel 交换密文。
不把宿主 fd 传给 guest SSL，也不在宿主再加密一次。
权限、取消和 deadline 仍由唯一 transport 协议处理。

链验证、SNI 与主机名校验是不同环节。
默认 TrustManager 使用真实路径验证，不能通过接收所有证书绕过。
HttpsURLConnection 与 Apache 使用对应 HostnameVerifier。
session cache 保存真实连接状态，超时不会因查询或恢复而续期。

AndroidCAStore 是只读版本化 CA 视图，证书从显式包来源加载。
测试 CA 包不等于公开信任列表。
当前旧版 OpenSSL 的维护和发行 CA 责任仍是在线发布缺口。
客户端 loopback 成功不能证明公开互联网 TLS 已可发布。
SSLEngine、server TLS 和完整 PKIX 另有支持边界。

## 契约、决定与操作

- [VFS](../../src/runtime/vfs/MODULE.md)、[database](../../src/runtime/database/MODULE.md)
- [crypto](../../src/guest/crypto/MODULE.md)、[guest Provider](../../src/guest/crypto/java/MODULE.md)
- [core native 接缝](../../src/runtime/dexvm/intrinsics/MODULE.md)
- [持久沙盒决定](../adr/session.md#adr-0020)、[离线接口事实](../adr/runtime.md#adr-0168)
- [Java 平台边界](../adr/dexvm.md#adr-0066)
- [新游戏挂载流程](../playbook/NEW-TITLE.md)、[已知问题](../state/KNOWN-ISSUES.md)
