# SBX-13 · 大尺寸文件 IO 有界传输

## 目标

解除 read/write/pread64/pwrite64 的任意 16 MiB 请求限制，保持受检传输和真实返回值。

## 依赖

- SBX-4、SBX-9；runtime/syscall、memory 与 VFS 现有契约。

## 交付

- 四个入口复用 64 KiB 临时缓冲，不按请求尺寸分配宿主临时 vector。
- 请求截到 `0x7ffff000`；短读/短写停止，后续块失败返回已完成字节。
- 首块坏指针返回 `-EFAULT`，VFS 错误保留 errno；定位 IO 恢复原 offset。
- getdents64 的独立容量限制不变；不修改 guest memcpy 来掩盖空指针。

## 验证

- Release `ogplay`、`ogplay_tests` 构建。
- 定向覆盖普通文件、pipe、定位 IO、进展分类和 17 MiB+123 字节往返，
  EOF、首块/中途坏指针、非法 offset、无效 fd 与游标保持：5 用例、69 断言通过。
- exact PvZ 命令探索实跑（非场景验收）：临时诊断确认
  `read(count=40531351) = 40531351`，不再返回旧 `-EINVAL`。
  原 `memcpy(src=0,count=2048)` 调用点 ELF `0x52b1d8` 已不再是首错。
- 新首错为 ELF `0x52b1b8` 的 `memcpy(dst=0,src=0x50013378,count=45047808)`，
  LR `0x316db1bc`；该目标来自 `0x52b18c` 调用游戏内 `_Znaj`（`0x8b41cc`），
  分配失败原因未确定。不得把大文件读成功解读为游戏已启动成功。
- 临时 syscall/边界诊断已移除，不提交 BootDex。

## 验证注意

首次错误地以裸位置参数传入用例名，doctest 未过滤，触发了额外用例；
其中现有 BootDex 的 SystemProperties native 绑定等失败并终止。
后续均显式使用 `--test-case`，不把该次误跑视作全量验收。

## 后续内存故障诊断（2026-09-09）

- SBX-13 已提交 `67f40177`，不含 BootDex。
- exact 实跑 syscall 追踪：45,051,904 字节 mmap2 一次成功、随后 ENOMEM；
  brk 扩到 1,387,307,008 仍成功；释放先前 40,534,016 与 45,051,904 字节映射后，
  同尺寸 mmap2 仍连续 ENOMEM。
- 临时保留 Map 原始异常，确认失败区间 `[0x68025000,0x6ab1c000)`，
  原因 `guest memory range overlaps an existing mapping`，不是宿主提交内存失败。
  API19 固定 TLS/thread-info/preinit/environment 在 `0x6a000000` 起，落在该区间。
- 根因在 `BindAndroidMemorySyscalls`：从 `0x60000000` 单调推进 next_mapping，
  不搜索空闲区、不避开固定映射；Map 成功前就推进游标，munmap 也不回收选择空间。
  游戏私有 allocator 的 `0xa500f0` 调 mmap，`0xa50100..0xa5010c`
  将 MAP_FAILED 转为 null，最终 `new[]` 的调用者没有检查 null 而 memcpy。
- 修复方向：memory 层提供在同一把锁下查找空闲区并映射的原子接口，以真实映射账本
  （含 PROT_NONE）避让；syscall 只选择地址策略，失败不消费游标，释放区可复用。
  同时覆盖并发分配、固定映射冲突、释放重用和真实耗尽；不要靠迁移 TLS 或调大预算掩盖。
- 本轮仅分析，临时 syscall/异常诊断已撤回并重新构建 ogplay。
  提前抛异常取证的进程在 teardown 未退出，已终止本轮启动的该进程。
