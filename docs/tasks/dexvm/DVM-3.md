# DVM-3 · 声明式 opcode 目录 + 生成器 + AOSP 机器比对

## 目标（一句话）

建立 `data/dexvm/dalvik_opcodes.json`（dex 035 全部 218 个已定义 opcode +
显式拒绝集）与确定性 C++ 解码表生成器，目录内容与 AOSP 基线三锚点机器比对。

## 依赖

- DVM-1（AOSP 基线）
- 设计：`docs/design/dexvm/02-architecture.md` §4、`07-aosp-reference.md` §2 模式 A

## 变更

- `data/dexvm/dalvik_opcodes.json`：由 `--bootstrap` 从 `opcode-gen/bytecode.txt`
  机器派生（非人抄）；218 个标准 opcode（opcode/name/format/has_result/
  index_type/flags），拒绝集 = unused 空洞（`0x3e-0x43`、`0x73`、`0x79-0x7a`）
  + odex 专用区（`0xe3-0xff`），并集恰好覆盖 0x00..0xff。
- `tools/generate_dexvm_opcodes.py`：
  - `--verify-aosp`：与 `bytecode.txt` 逐项等价、`DexOpcodes.h` 枚举名/值
    二次核对、`InstrUtils.cpp` 宽度表与 format 首位派生宽度核对；
  - `--output [--check]`：生成 `opcode_table.h`（DexOpcode 枚举、format/
    index/flags/width 的 256 项 constexpr 表，rejected 项 `defined=false`）；
  - `--self-test`。
- CMake：`ogplay_dexvm_codegen` 生成目标 +
  `tools.dexvm_opcode_generator_self_test` / `tools.dexvm_opcode_catalog_current`。
- `capabilities.toml` 新增 `dexvm.opcode_catalog`。

## 验收（机器可判定）

- `ctest --preset dev -R tools.dexvm_opcode` 2/2 通过。
- 目录与三锚点任何分歧、生成表过期均为 CTest 失败。
