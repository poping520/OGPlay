# VFS-10 · F_GETFL 与进程标准流打开状态

## 范围

依赖既有 VFS 打开对象与 syscall 诊断端点，不引入宿主标准输入或新依赖。
55/221 共用 F_GETFL，普通 FD 从 VFS 不可变状态读取访问/追加/非阻塞等 ARM 标志；
0/1/2 在原诊断端点表中保存访问模式及存活状态，stdin EOF，stdout/stderr 仍投递原 sink。
查询/写入/dup/close 共用状态，未知/已关闭 FD 返回 EBADF；其他 fcntl 命令 ENOSYS 并记账。
诊断端点的 fstat/seek/ioctl 不在范围，不改变普通 VFS 元数据与分配器。

同时接受并保存信号 action 的 SA_NOCLDSTOP，非 SIGCHLD 无效果；不接入子进程、调度、
完整信号服务或 /proc/stat，不修改 APK/Mono/strlen，不引入标题分支。

## 验收

- 既有 syscall_tests.cpp：55/221、访问模式、状态标志、dup/关闭/非法 FD；0/1/2 查询、
  stdin EOF、stdout/stderr 原 sink 与 write/writev、关闭后失败、其他 fcntl 记账。
- syscall_signal_tests.cpp：legacy/RT 注册和查询 flags=0x5，双后端 SIGINFO 投递与恢复，
  未知标志拒绝且不替换已有 disposition。
- 仅构建 Release ogplay-cli/ogplay_tests，运行上述两个测试文件；capabilities check。
- 同一 `library run com.amazon.games.tilmorningslight` 实跑一次，确认原错消失并记录下一首错；
  reached-fault 证据不等于 title gate。

## 结果

Release 只构建 ogplay-cli/ogplay_tests，成功；两个指定测试文件 **55 cases / 919 assertions**
全部通过（`ogplay_tests --source-file=*syscall_tests.cpp,*syscall_signal_tests.cpp --no-skip`）。
capabilities check、UTF-8/链接与 diff 静态检查通过。

从 build/release 执行同一条 `./ogplay-cli library run com.amazon.games.tilmorningslight` 一次，
未加载诊断探针；16.926 秒、逻辑帧 **11362**、退出 **1**。机器断言 Mono mini-posix.c:382
断言、System.Console 初始化错误和原 guest memory is unmapped 均未出现；下一停止首错为
UnityMain nativeRender 的 `JNI guest instance method is not declared: class=java/lang/ClassLoader
method=findLibrary(Ljava/lang/String;)Ljava/lang/String;`。不修复该独立缺口，未声称真实呈现或可玩。

证据目录 `.local/tml-startup-fix/`：build.log、tests.log、capabilities.log、run.log、run-exit.json、
proof.json。原分析 REPORT.md 与 throws.* 哈希保持不变，探针未纳入产品；未改 APK/Mono/strlen、
调度或 /proc/stat。诊断文本可选项未实施。
