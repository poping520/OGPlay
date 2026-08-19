# DVM-53 · FastCode 预解码构建器

## 目标（一句话）

把已通过链接预检的 u2 指令流确定性派生为不改写原 DEX 的只读 FastCode，冻结
dex pc 映射、预拼装操作数、分支目标与三类 payload 边表。

## 依赖

- DVM-6..12、DVM-52
- `docs/design/dexvm/10-interpreter-threaded.md` V2-1
- `.local/asop/dalvik/vm/mterp/README.txt`

## 交付

- `FastInstruction` 保存 opcode、宽度、寄存器、literal/index、dex pc 与内部目标。
- packed/sparse switch 与 fill-array-data 只读边表保留 key/target/data 及 tick 权重。
- payload 不进入可执行 pc 映射；错位分支、错配/截断 payload 与越界目标明确失败。
- FastCode 只保存数值元数据，不含 guest 引用，不计入 guest heap，也不改写 dex u2。

## 验证

- `tests/dexvm/fast_code_tests.cpp`
- Windows/x64 `windows-msvc` 聚焦门禁：3/3 通过。

状态：完成。
