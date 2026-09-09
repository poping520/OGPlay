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
