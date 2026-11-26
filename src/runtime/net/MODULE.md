# 模块：runtime/net

## 职责

guest 进程唯一的 socket 栈。ARM socket syscall 与 `libcore.io.Posix` socket native 共用同一组
socket 对象与描述符号；AF_UNIX 与回环在进程内实现，其他目的地只在联网模式经宿主出口；
提供 netd `dnsproxyd` 进程内端点，使原版 Bionic 的代理式解析不改一行即可工作。

## 依赖

`runtime/vfs`（描述符对象、FD 表、就绪计数）与 `hal`（`HostNetwork` 出口、`core::Logger`）。
不依赖 syscall、dexvm、JNI 或 integration；上层只经 `GuestNetworkStack` 调用。

## 公共 API

- `GuestNetworkStack(vfs, config)`：会话持有，生命周期短于 VFS；`Stop()` 使所有阻塞与后续操作
  失败（EBADF），会话关闭时调用。
- `Socket/SocketPair/Bind/Connect/Listen/Accept/LocalName/PeerName/Send/Receive/ShutdownSocket/
  SetOption/GetOption`：Linux ARM 语义，返回值为非负结果或负 Linux errno；描述符是 VFS 对象，
  dup/fork/close-on-exec/O_NONBLOCK/poll 与文件同表。
- `Resolve/ReverseLookup`：数字地址、API19 hosts（localhost、ip6-localhost），联网模式再查宿主解析器。
- `DecodeSockaddr/EncodeSockaddr/ParseNumericAddress/FormatNumericAddress/IsLoopback`：sockaddr_un/in/in6
  编解码与地址工具。
- `NetworkMode`：`disabled`（启动器 `network_policy=disabled`，默认）、`loopback`、`online`（`allow`）。

## 不变量

- 设备状态唯一：离线时如同 Wi-Fi 开启未连接的手机——AF_UNIX、127/8、::1 可用，其他目的地
  connect/sendto 返回 ENETUNREACH，名字解析失败；联网时 Wi-Fi 已连接，设备地址为
  `device_ipv4`（默认 192.168.1.100），只有 IPv4。连接性服务、接口表与 `AI_ADDRCONFIG` 取同一事实。
- 回环与 AF_UNIX 永不经过宿主：TCP 监听/连接、UDP 投递与 socketpair 在进程内按端口/路径登记表配对，
  IPv4 与 IPv6 共用端口空间（IPv6 socket 经 IPv4 映射地址访问 IPv4 端点）；临时端口 32768–60999。
  进程内队列以 212992 字节为界，满时阻塞或 EAGAIN；不监听宿主端口，局域网不可达本进程。
- 外部流与数据报只在联网模式经 `HostNetwork` 的非阻塞宿主 socket；getsockname 报设备地址与宿主
  分配的端口，不暴露宿主网卡地址。宿主 socket 的就绪无通知，poll/阻塞按 10 ms 片复查。
- 阻塞操作不持栈锁访问宿主；按 SO_RCVTIMEO/SO_SNDTIMEO 超时返回 EAGAIN（connect 为 EINPROGRESS），
  以 VFS 线程绑定的中断谓词返回 EINTR。关闭（最后一个打开状态退役）立即唤醒等待方并使其 EBADF；
  对端读端已关闭时写返回 EPIPE，不产生 SIGPIPE。
- 选项：SO_TYPE/DOMAIN/PROTOCOL/ACCEPTCONN/ERROR/REUSEADDR/KEEPALIVE/BROADCAST/RCVBUF/SNDBUF（读回为
  请求值两倍）/LINGER/RCVTIMEO/SNDTIMEO/PEERCRED（进程 pid/uid/gid）、TCP_NODELAY、IP_TTL/TOS 与
  组播参数、IPV6_V6ONLY 等；宿主相关项转发给宿主 socket，其余只保存。组播成员、SO_BINDTODEVICE
  不支持。
- dnsproxyd：连接 `/dev/socket/dnsproxyd` 得到一次性会话，按 API19 netd `DnsProxyListener` 协议回答
  getaddrinfo/gethostbyname/gethostbyaddr（"222"+记录，失败 "401"）；addrinfo 为 32 位 ARM 布局，
  socktype 为 0 时按 bionic explore 顺序展开 DGRAM/STREAM/RAW。解析失败与 netd 一样由 Bionic 报
  EAI_NODATA。
- 可选 `logger` 记录名字查询与出口 connect（`runtime.net`），不记录载荷。

## 支持边界

不支持：辅助数据（SCM_RIGHTS）、OOB、raw/packet/netlink socket、AF_UNIX 自动绑定与文件系统节点、
宿主端口监听与局域网入站、组播成员、对外 IPv6（设备无 IPv6 地址）、宿主代理设置（TUN 模式代理
对宿主出口透明）。
