# DVM-5 · dexasm 夹具管线与 C++ L1 回读交叉验证

## 目标（一句话）

建立"`tests/dexvm/fixtures/*.dexasm` → 构建期批量汇编 → C++ 严格 L1 解析器
回读断言"的夹具管线，形成我方汇编与我方独立解析器的交叉裁判。

## 依赖

- DVM-4（dexasm）、`loader.dex_l1`（存量 C++ 解析器）
- 设计：`docs/design/dexvm/05-verification.md` §1（golden 锁字节、回读锁结构）

## 变更

- CMake：`ogplay_dexvm_fixtures` 目标在构建期把全部 `.dexasm` 汇编进
  `build/<preset>/dexvm_fixtures/`；`ogplay_tests` 依赖它并获得
  `OGPLAY_DEXVM_FIXTURE_DIR`。
- `tests/dexvm/fixtures/core.dexasm`：两类继承 + 接口 + 静态初始值 +
  try/catch + packed/sparse switch + array-data + wide + native 方法。
- `tests/dexvm/dexasm_readback_tests.cpp`：ParseDex + ReadDexClassData 断言
  类层级、接口、字段/方法归类（direct/virtual）、code 元信息（registers/
  ins/tries/指令单元数）与 native 无 code；截断/坏 magic/坏 file_size 反例
  被严格解析器拒绝。
- 关于 07 §4 host 工具评估：`libdex`+`dexdump` 交叉裁判**降级不采用**——
  KitKat 代码在三平台 warnings-as-errors 工具链下修补成本高，且本 WU 的
  "独立 Python 写、独立 C++ 读"已构成双实现交叉；结论按设计允许记录于此。

## 验收（机器可判定）

- `ctest --preset dev -R dexasm` 3 项全过（self-test + 2 个 C++ 用例）。
