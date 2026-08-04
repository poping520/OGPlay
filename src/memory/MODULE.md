# 模块：memory

## 职责

管理 32/64 位 guest 地址空间、4 GiB 直接映射、权限、soft-MMU 调试后端与快照。

## 公共 API

- `GuestAddress`：严格 32 位 guest 地址；算术、对齐越界明确失败。
- `GuestRange`：64 位长度的半开区间，可表达完整 4 GiB 地址空间。
- `LowAddressGuard()`：`0x00000000–0x0000ffff` 默认保留区间。
- `AddressSpace`：4 GiB reservation 上的 Map/Unmap/Protect/Read/Write/Validate；
  `ValidateMapped` 只检查映射存在性，映射账本与页权限独立，因此 `PROT_NONE` guard page
  不会被误判为未映射。
- `MemoryFault`：携带 guest 地址、访问类型、失败原因和 guest 线程 ID。
- `MemoryBus`：CPU 只依赖的 8/16/32/64 位小端数据访存及 16/32 位取指接口。
- `CheckedMemoryBus`：完整权限验证和观察器钩子的 soft-MMU 调试后端。
- `MemorySnapshot`：带版本、宿主页尺寸、映射权限与内容的最小内存快照。
- `CaptureSnapshot/RestoreSnapshot`：合并连续映射并以事务式替换恢复内存状态。
- M1 后续增加直接 JIT 访问适配，并由 CPU/线程模块组合完整 guest 快照。

## 不变量

- `0x00000000–0x0000ffff` 默认未映射。
- 越界和权限错误必须产生带地址、访问类型和线程信息的 fault。
- 取指检查 execute 权限；普通读取检查 read 权限，两者不得互相替代。
- 映射状态可序列化。
- `PROT_NONE` 与 execute-only 映射快照时临时以宿主只读权限复制，恢复后必须回到原权限。
- 所有映射操作按宿主页对齐；权限账本与宿主权限必须同步更新。
- 不完整或不兼容的快照不得改变当前地址空间。
- 页索引和小端编解码必须在 32/64 位宿主及 GCC/Clang/MSVC 严格告警下保持类型安全；
  8/16 位值也必须先在足够宽的类型中移位和合并。

## 禁止

- 不得为兼容性全局扩大低地址映射。
- 不得在公共接口暴露未经验证的宿主指针。

## 测试

`tests/memory/` 的边界、权限、并发与快照测试。
