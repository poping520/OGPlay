# DVM-54 · Threaded 全 bridge 后端骨架

## 目标（一句话）

在不复制异常展开与 Java 语义的前提下接入可显式选择的 FastCode dispatcher，
先让全部 handler bridge 回既有 switch 单步实现并证明结果、异常、tick 与 trace 等价。

## 依赖

- DVM-53
- `docs/design/dexvm/10-interpreter-threaded.md` V2-2
- `.local/aosp/dalvik/vm/mterp/README.txt`

## 交付

- `InterpreterBackend::{switch_dispatch,threaded}`；默认仍为 switch。
- GCC/Clang 使用 computed-goto handler 表，MSVC 使用同枚举顺序的 dense switch。
- `Run` 的异常、catch、帧展开保持唯一实现；threaded 当前全部 bridge 到 `Step`。
- stats 明确报告后端、FastCode 构建次数与宿主字节数。

## 验证

- `tests/dexvm/interpreter_tests.cpp` 双后端同输入比较返回、异常、指令数与 trace。
- Windows/x64 `windows-msvc` 聚焦门禁：1/1 通过。

状态：完成。
