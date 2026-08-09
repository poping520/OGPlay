# ADR-0016 · 受保护的 Dynarmic 数据页表

- 状态：Accepted
- 日期：2026-08-09
- Supersedes：ADR-0011 中“CPU 只能通过 `CheckedMemoryBus` 访问数据、不得获得宿主页
  指针”的部分；固定 4 KiB guest 页及独立权限账本仍然有效。

## 背景

Dynarmic 在 callback-only 模式下会让每次普通 guest 数据访存进入 C++ 虚调用、互斥锁、
权限遍历与小端搬运。Debug exact-APK 稳态采样显示该路径占据主要 CPU 时间，而 Android
ARMv7 游戏的大多数可写数据页具备稳定的 read/write、不可执行权限。

ADR-0011 要求未来 fastmem 先定义显式受保护访问契约。本 ADR 建立该契约，不改变固定
guest 页语义，也不允许任意裸指针绕过权限账本。

## 决定

- `AddressSpace` 拥有生命周期稳定、按 4 KiB guest 页索引的 32 位直接数据页表。
- 只有已映射且权限允许 read/write、禁止 execute 的页才发布对应宿主页首地址；未映射、
  只读、可执行或其他权限组合一律发布 null 并回退 `CheckedMemoryBus`。
- Map/Protect/Unmap/RestoreSnapshot 与权限账本在同一临界区同步更新页表。
- 安装 memory observer 时 `CheckedMemoryBus` 不暴露页表，watchpoint/trace 必须继续观察
  每次访问；取指也始终走 execute 权限回调。
- Dynarmic 仅将该表用于数据访问，并对跨页的 8/16/32/64 位访问强制回调，以完整验证
  两侧页面。回调路径继续产生带地址、访问类型和线程号的 `MemoryFault`。
- callback-only 后端关闭无效且会阻止 Arm64 register get/set elimination 的逐访存 halt
  检查；边界 fault 仍由现有回调停止状态上报。

## 后果

- 常见 RW 数据访存不再进入 C++ callback，Debug 运行显著降低边界成本。
- observer、权限边界、可执行页和跨页访问保持可诊断的 soft-MMU 语义。
- 每个地址空间增加一张 1,048,576 项宿主指针表；这是 32 位 guest 空间固定、可预估的
  内存成本。
- 任何扩大直接页资格或允许并发修改页表的工作都必须另行证明权限、失效和线程同步
  契约，不得仅以性能为由放宽。
