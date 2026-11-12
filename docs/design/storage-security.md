# 存储、数据库与安全边界

存储子系统为 guest 提供统一文件身份、描述符与每安装实例的持久沙盒。
Java IO、原生 syscall、SQLite 与媒体读取共用 VFS，
密码和 TLS 通过 guest 后端使用同一文件及网络边界。
VFS 是虚拟文件系统，负责把 Android 路径语义与宿主来源隔开，
而不是把 guest 路径直接交给宿主打开。

贯穿本篇的例子是应用打开一个文件，把其中一段交给媒体，然后关闭 FD 并改名。
普通 FD 保留打开状态，媒体 lease 保留捕获版本与窗口，路径改变只影响名字索引。
将这几种身份分开，才能同时支持存活句柄、独立游标和稳定媒体来源。

## 文件、打开状态与 backing

```text
guest path / alias → File 节点 → backing
                         ↑
FD → OpenFile(offset, mode, lock)
                         ↓
                    overlay / SandboxStore
```

File 是稳定文件节点，path 是可以变化的名字，backing 是提供内容的底层存储。
OpenFile 表示一次打开，保存 offset、模式与同步状态；FD 表只持有它的引用。
dup 共享同一个打开状态，独立 open 有独立 offset，
ReadAt/WriteAt 则在指定位置操作而不改变顺序游标。

文件内容版本随写入或截断变化，rename 只是名字改变；
别名与规范路径指向同一节点、锁和数据库 identity。
关闭先从 FD 表移除入口，已取得 OpenFile 的在途操作仍绑定旧对象。

```text
旧名字 → 节点 N ← FD A 的 OpenFile
                    ↑
              lease：版本 V / 区间

rename 后：新名字 → 节点 N
同名新建：旧名字 → 节点 M
```

以开篇例子来看：文件被 rename 后，路径索引更新，但存活 FD A 仍指向节点 N。
之后在旧名字下新建的文件是另一个节点 M。关闭 A 后，同一整数可能立即分配给 M，
在途读取却仍持有原 OpenFile，因此不会向 M 提交旧游标或结果。
路径、节点、FD 数字和内容版本各有用途，这也是不能仅用字符串路径做缓存键的原因。

[资源与沙盒决定](../adr/session.md#adr-0020)采用这套分离，
避免缓存仅以路径为键而误读替换后的文件，也避免锁外保留会失效的 FD 表元素引用。
代价是内容版本、节点寿命和打开状态分别同步，不能靠一把全局 IO 锁简化全部操作。
节点、打开状态与 backing 的精确接口见
[VFS 契约](../../src/runtime/vfs/MODULE.md)。

## 路径解析与只读来源

路径按 ASCII case-fold 查找，并逐目录处理点段和父目录。
guest 在虚拟根处继续访问父目录仍停留于根，相对路径从 guest cwd 解析。
挂载条目、宿主 backing 与沙盒编码仍拒绝逃逸、符号链接和歧义，
因此允许 guest 的常规 `..` 并不放宽宿主路径安全边界。

VFS 不解析 ZIP/OBB，也不读取 Java 对象；Loader 或上层注入受检 positioned reader，
即按区间读取且不改变共享游标的来源接口，并附带寿命对象。
APK/OBB/外部来源保持只读，写入进入可写节点或 overlay。
overlay 是覆盖底层素材的可写层，删除用 tombstone 记录遮蔽，而不反写原始资源。

按区间读取让大文件的小读不必整份物化，但压缩后退读取可能从头重放，
归档首次完整性校验也可能需要线性扫描。
“没有全量内存副本”不等于“没有全量 IO”。
这样保留归档校验边界，而不通过跳过 CRC 换取看似廉价的小读。

宿主文件首次打开绑定来源身份，闲置句柄按预算和 LRU 管理，活动 FD 与 lease 保活所需来源。
重新打开核对身份与尺寸，避免同路径换文件后沿用旧内容假设。
外部程序原地修改宿主文件不承诺快照一致性；截断和读取失败仍明确报告，
支持边界见 VFS 契约。

## lease 怎样独立于原 FD

VfsReadLease 固定节点版本与读取窗口，
只读来源共享 backing，可写来源在统一快照预算内复制所需区间。
lease 是携带保活对象的读取租约，所以原 FD 关闭、unlink 或 rename 后仍可消费捕获内容。
取消 token 向下传递，取消一份消费者任务不撤销其他消费者共享的 backing。

回到开篇例子，媒体 setDataSource 捕获起点和长度后，
调用者改变原 FD offset 不影响播放器；同路径建立新文件也不会替换已捕获内容。
可写来源的 lease 是捕获窗口的快照，普通 FD 则继续按文件操作顺序看到同节点更新。
两者是不同的读取承诺，快照成本只由需要它的消费者承担，普通打开不为不可变快照付费。
可写窗口可能占用快照预算，大窗口超限会明确失败，当前没有为此引入分页写时复制。

文件映射使用另一种来源保护。CaptureMappingSource 持有节点并阻止冲突修改，
登记保护与写入拒绝在同一锁域排序，而不是先检查后留下竞争窗口。
它保留映射所需的稳定性，但不意味着已经提供完整 POSIX mmap 与跨进程锁。
两种保护的用途分别由消费者决定。

## 缩锁怎样保留文件语义

锁序按打开状态、节点和全局元数据安排。
耗时 backing IO、CRC 与解压不跨全局索引锁执行，
先固定来源和操作身份，再读取，再按该操作提交游标或结果。
同一 OpenFile 的顺序读写保持串行，不同节点不因一个慢来源共同等待全局 IO。

缩锁要求显式保活节点并核对版本，
而不是只把 Read 中的一段代码挪到锁外。
写入、截断、路径发布和关闭都需遵守同一事实来源，
例如 unlink 后存活句柄关闭不能把已删除路径重新创建出来。
这些精确不变量留在 [VFS MODULE](../../src/runtime/vfs/MODULE.md)。

## 为什么按安装实例组织沙盒

```text
sandbox/<installation-id>/
  meta.toml
  internal/
  external/
  obb/
  sdcard/
```

installation id 是安装层分配的实例身份，package 仍保存真实 Android 包名。
同包副本可以有不同存档、设置与 ANDROID_ID；版本是元数据，不决定实例。
安装库与沙盒使用同一 id，导入优先沿用无库条目且 meta 匹配的残留沙盒，
否则原子占用两侧均空闲的最小编号。

按包名作为唯一存储键会使“再安装一份”共享存档，按版本作为键又会让升级丢失连续身份。
[安装实例决定](../adr/session.md#adr-0020)选择独立实例，
代价是多实例裸启动需要明确选择，并在导入、运行锁、设置与沙盒之间传递同一 id。

internal、external、obb 和其余 sdcard 各自对应 Android 语义根，专用根优先于通用根。
SandboxStore 集中处理正反映射、文件名编码、tombstone、配额和持久化，
Java、native 与 SQLite 不再各建一套落盘目录规则。
这些宿主目录名不会作为 guest 文件系统布局泄漏给应用。

schema 与 meta 不匹配时明确失败，不自动迁移或创建空沙盒掩盖旧数据。
语义根不可删除，缺根可重建并告警，根被文件替代或混合布局继续拒绝。
Clear 清空内容但保留 meta 与身份；close、fsync、pause 和 clean stop 按对应落点持久化。
配额是兼容层限制，不是宿主物理剩余空间保证。

## SQLite 为什么运行真实引擎

ROM SQLite Java 管理连接池、事务、语句、Cursor 和异常，
native 只适配 ABI、令牌与错误，runtime/database 提供固定真实 SQLite 引擎。
[SQLite 决定](../adr/dexvm.md#adr-0068)放弃自定义数据库格式和 C++ Java 事务影子栈，
让真实引擎承担 SQL、文件格式和损坏判断。

一次事务不仅修改主库，还可能创建、写入或删除 rollback journal。
如果主库在沙盒，journal 却落到宿主直接路径，锁、权限、配额与清理就会形成两套规则。
因此主库、rollback journal 和临时文件都经唯一 VFS，
adapter 以 VFS 实例和规范路径作为锁的键，路径别名不能获得第二把独立数据库锁；
unlink 后存活句柄与沙盒 flush 仍按普通文件规则处理。

代价是 VFS 的定位读写、同步、锁和取消行为会直接影响数据库事务，
宿主 SQLite 版本与 API19 Java 栈的接缝也需要验证，
不能只看 SQL 查询返回了一行，也不能据此宣称完整 Android SQLite 等价。

时间由 Clock 注入，随机由统一密码安全随机源（CSPRNG）提供。
CursorWindow 是保存有限行列结果的窗口，按必需行位置与是否统计全部行的协议填充，
损坏交给原版处理器。当前支持 rollback journal 与进程内并发，
WAL、ATTACH/DETACH、动态扩展、跨进程共享和断电耐久性不在声明范围。

## Java IO 与网络使用什么身份

File、stream 和 channel 的普通算法执行 ROM，Posix native 把 VFS errno 传给 ErrnoException。
Java FileDescriptor 保存逻辑描述符，而不是宿主 fd。
接口查询用描述符与普通文件 IO 分开，数字有效不表示可以对它执行任何操作。
这样文件身份与资源权限能跨 Java/native 保持一致。

NetworkRuntime 通过注入的 policy/transport 访问网络，默认离线。
URL 解析本身不触网，DNS、connect 和实际 IO 才检查策略。
离线接口视图来自统一进程拓扑，不读取宿主网卡；
能查询回环地址也不代表在线传输已授权。
原因见[离线接口决定](../adr/runtime.md#adr-0168)。

## 密码资源与 BKS 状态归属

原版 JCA、证书、KeyStore 和 JSSE API 分派到受审 Provider/SPI，
即 Java 安全 API 用来选择具体实现的服务接口。
密码 native 经私有 guest JNI 连接随附 libcrypto/libssl，
宿主不再实现另一份 AES、证书 parser 或 TLS 状态机。

[密码边界决定](../adr/dexvm.md#adr-0066)把普通协议留在 Java，真实算法放入 guest 后端。
BKS 条目、格式、KDF、MAC 与 key 保护由自有 Provider 管理，
状态保存在 Java 对象图，不建 C++ 影子条目表。
代价是项目承担 SPI、格式互操作和旧后端维护，不能借宿主 JDK 默认格式替代 API19 BKS。

调用方拥有输入输出流，持久化使用正常 Java IO/VFS；
store 写流并不为任意输出提供原子替换。
Java 保存逻辑 token，native owner 管实际资源，显式释放、GC 与 teardown 共用关闭事实。
BKS 也不代表 AndroidKeyStore 或硬件保护，敏感值不进入诊断日志。

## TLS 为什么使用内存 BIO

OGPlayJSSE 把原版 JSSE API 连到 guest libssl/libcrypto。
内存 BIO 是 SSL 库读写加密字节的缓冲接口，
它与 NetworkRuntime raw channel 交换密文，不把宿主 fd 传入 guest SSL。
这样加密和网络授权分别由唯一后端处理，也避免宿主再加密一次。

TLS 链验证、SNI 与主机名校验分别承担不同责任。
默认 TrustManager 执行真实路径验证，HttpsURLConnection 与 Apache 使用相应 HostnameVerifier，
连接恢复和 session cache 保留实际寿命；查询本身不续期。
这种拆分多了校验接缝，却能区分“信任链有效”“目标主机正确”与“网络允许连接”。

AndroidCAStore 提供来自显式包的只读版本化 CA 视图，测试 CA 包不是公开信任列表。
一次回环 TLS 成功可以验证握手、密文 transport、证书链和主机名校验的接缝，
却不能替项目选择公开 CA 来源或承担旧 OpenSSL 的补丁维护，这两项责任尚未闭合，
loopback 客户端成功只能作为本地兼容开发证据。
默认离线仍由唯一 NetworkPolicy 决定；KeyStore 能保存证书不等于该证书被加入默认信任，
AndroidCAStore 可读也不等于拥有完整 Android 密钥服务。
SSLEngine、server TLS、AndroidKeyStore 与完整 PKIX 的边界见
[已知问题](../state/KNOWN-ISSUES.md#执行与平台)。

## 契约、决定与操作

- [VFS](../../src/runtime/vfs/MODULE.md)、[database](../../src/runtime/database/MODULE.md)
- [crypto](../../src/guest/crypto/MODULE.md)、[guest Provider](../../src/guest/crypto/java/MODULE.md)
- [core native 接缝](../../src/runtime/dexvm/intrinsics/MODULE.md)
- [持久沙盒决定](../adr/session.md#adr-0020)、[离线接口事实](../adr/runtime.md#adr-0168)
- [Java 平台边界](../adr/dexvm.md#adr-0066)
- [新游戏挂载流程](../playbook/NEW-TITLE.md)、[已知问题](../state/KNOWN-ISSUES.md)
