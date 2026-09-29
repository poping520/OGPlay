# VFS-05 · 进程映射的只读 proc 视图

状态：完成（有界 maps 视图）。决策：[ADR-0078](../../adr/runtime.md#adr-0078)。

## 范围

- memory 从唯一页账本导出有界元数据，包含 PROT_NONE、权限变化和映射空洞，不复制内存。
- VFS 注入式只读生成文件，每次 open 一致快照；预算先预留，回调不持 VFS 锁。
  注册句柄销毁同步撤销回调，已有 FD/lease 仍存活；stat 大小为 0。
- integration 安装 `/proc/self/maps`；本机 guest 页都是私有匿名 backing，包括复制入内存的
  ELF 段，因此输出真实范围/权限和匿名字段，不猜测文件偏移/inode/路径。
- 不实现完整 procfs、PID 别名、self/stat、文件 mmap 或宿主进程信息。

## 验证

- Windows `windows-msvc` Release `ogplay_tests` / `ogplay` 构建通过；沿用本机验证的
  进程级 `_CL_=/wd4996 /wd4834`，仅屏蔽已有测试文件告警，不修改相关文件。
- `VFS-05*,VFS *,Android access*,Android guest proc*`：56 用例/351 断言通过。
  覆盖 PROT_NONE/空洞/替换/4 GiB 末页、按打开快照、短读/seek、预算失败、回调撤销等待、
  旧 FD 存活、ARM access/open/read 和真实 guest 进程 maps。首次断言漏加 ELF p_vaddr，
  已按实际 PT_LOAD 修正为 0x10010000，并完成上述通过的复验。
- 同一 Dead Trigger 1.1.0 APK/OBB，无 Profile、无 survey、临时沙盒、3 帧上限实跑：
  `Mono requires /proc to be mounted` 消失；下一首错 `Cannot set SIG_SUSPEND handler`，
  guest 触发 SIGABRT（134），CLI 返回 1。属于 reached-fault，不是游戏兼容验收。
- 证据：`.local/proc-maps-{build,test-build,tests,startup}.log`。
- 本次不继续修复信号投递、self/stat 或历史绘制错误。
