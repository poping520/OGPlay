# 子模块：runtime/process

## 职责

在游戏进程内模拟子进程：进程表、fork/exec/exit/wait/kill 语义，以及以宿主代码实现的内置命令。
不创建宿主进程，不装载 guest ELF；对 guest 而言它们是普通子进程。native 入口由 syscall 的
`BindAndroidProcessSpawnSyscalls` 接入，Java 入口（ProcessManager、Posix.waitpid/kill）由
integration 经 `CoreIntrinsicServices` 接入，两者共用同一个 `VirtualProcessKernel`。

## 依赖

依赖 `runtime/vfs`、`runtime/common` 与 `core`；不得依赖 syscall、dexvm、jni、framework 或 integration。

## 接口

- `VirtualProcessKernel`：每个 guest 进程一个，析构时停止并等待全部命令线程、释放其 FD 表；
  必须先于 VFS 析构。
  - `Fork(parent, table)` 创建 `forked` 子进程并复制 FD 表；`Exec` 只接受 `forked` 进程，按精确路径
    查找注册命令，缺失返回 ENOENT 并记账 `process.exec.missing_command`，参数超过 1024 项或 128 KiB
    返回 E2BIG；成功后关闭 cloexec 表项，在独立宿主线程上运行命令。`Exit` 结束仍在 `forked` 的进程。
  - `Spawn`/`SpawnCommand`：不经 guest 代码的 posix_spawn，`arrange` 在子表绑定下布置描述符。
  - `Wait(caller, selector, no_hang, interrupted)`：selector > 0 等指定子进程，其余等任意子进程；
    无匹配子进程 ECHILD，`interrupted` 以 10 ms 片检查并返回 EINTR。状态按 Linux 编码：
    退出为 `(code & 0xff) << 8`，信号终止为信号号。
  - `Kill`：信号 0 只检查存在；init 与应用进程 EPERM（应用信号由调用方处理）；默认忽略的信号
    （SIGCHLD/SIGCONT/SIGURG/SIGWINCH 及作业控制）无效果；其余经停止令牌协作终止命令，状态记为
    该信号。`forked` 子进程在父线程上运行 guest 代码，不能从外部终止，记账 `process.kill.forked_child`。
  - `ResolveExecutable` 按 execvp 规则解析：含 `/` 视为路径，否则沿 PATH 查注册命令。
- `SetSystemPropertySource` 注入模拟设备的系统属性（由 integration 提供，与 SystemProperties 同源）；
  替换或清除会等待进行中的读取，未注入时属性集为空。`SystemProperties()` 按键排序返回。
- `RegisterBuiltinCommands` 注册 `/system/bin/sh`、`/system/bin/ps`、`/system/bin/getprop` 与按 argv[1]
  分派 ps/getprop 的 `/system/bin/toolbox`。

## 不变量

- 进程表固定含 init(1, uid 0, `/init`) 与应用进程（配置的 pid/ppid/uid，名称由 integration 设为包名）；
  虚拟 pid 取 2000..32767 中未用且未被 `reserved_id` 占用的号。命令退出时先释放 FD 表（管道读端随即见到
  EOF），再置 zombie 并唤醒等待者；父进程先退出的子进程转给 init 并立即回收。
- 命令线程绑定自己的 FD 表，中断谓词为停止令牌；写入无读端的管道使命令以 SIGPIPE 状态结束。
- ps 输出 Android 4.4 toolbox 表头与 `%-9s %-5d %-5d %-6d %-5d %08x %08x %c %s` 行格式，只列进程表中的进程；
  USER 为 root/system/u0_aN，VSIZE/RSS 以 KiB 计（应用来自注入的地址空间统计，其余为固定值），
  WCHAN/PC 为 0，状态为 R（ps 自身）/S/Z。数字参数按 pid、其他参数按名称前缀过滤，`-` 选项接受但
  不生效并记账 `process.ps.option`。
- getprop 按 Android 4.4 toolbox 行为：无参数按键序输出全部 `[key]: [value]`；`getprop KEY [DEFAULT]`
  输出该值，属性未定义或为空时输出 DEFAULT（缺省为空行），退出码 0。
- sh 支持 `sh -c SCRIPT [NAME ARGS...]`、`sh FILE` 与读 stdin：单/双引号、反斜杠、`$NAME`/`${NAME}`/
  `$?`/`$$`/`$#`/`$0-9`、变量赋值、`< > >> >| n> n>&m n<&m`、管道 `|`、`;`/`&&`/`||`/换行与 `!`；
  内建 exit/cd/echo/true/false/`:`。找不到的命令输出 `sh: NAME: not found` 并以 127 退出，重定向失败
  以 1 退出。复合命令、子 shell、命令替换、here-document 报语法错误（退出码 2，记账 `process.sh.syntax`），
  `&` 按顺序执行并记账，不做通配展开与字段拆分。

## 支持边界

- 只运行注册命令；未注册的可执行文件（含 su 等）与未 root 手机一样返回 ENOENT。getprop 只读，
  不提供 setprop/watchprops；native `__system_property_*` 读取 integration 在 VM 建立时写入的同源快照。
  `/system/bin` 下不发布对应的 VFS 文件节点，`/proc/<pid>` 不列出虚拟进程。
- SIGCHLD 不投递给应用；作业控制、进程组与会话不建模。
