# 模块：memory

## 职责

管理 32/64 位 guest 地址空间、4 GiB 直接映射、权限、soft-MMU 调试后端与快照。

## 公共 API

- `GuestAddress`：严格 32 位 guest 地址；算术、对齐越界明确失败。
- `GuestRange`：64 位长度的半开区间，可表达完整 4 GiB 地址空间。
- `LowAddressGuard()`：`0x00000000–0x0000ffff` 默认保留区间。
- M1 后续定义 Map/Unmap/Protect/Read/Write/ValidateRange/Snapshot。

## 不变量

- `0x00000000–0x0000ffff` 默认未映射。
- 越界和权限错误必须产生带地址、访问类型和线程信息的 fault。
- 映射状态可序列化。

## 禁止

- 不得为兼容性全局扩大低地址映射。
- 不得在公共接口暴露未经验证的宿主指针。

## 测试

`tests/memory/` 的边界、权限、并发与快照测试。
