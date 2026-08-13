# DVM-1 · vendor AOSP Dalvik 固定 tag 参考基线

## 目标（一句话）

以浅 submodule 固定 `platform/dalvik` 于 tag `android-4.4.4_r2`，建立 commit 与
锚点文件的机器校验，登记 NOTICES——为 DexVM 的语义参考与机器比对提供数据源。

## 依赖

- 设计：`docs/design/dexvm/07-aosp-reference.md` §1（vendor 决定）
- 先例：ADR-0007（第三方一律 submodule）、`gles.angle_dependency` 校验精神

## 变更

- `third_party/aosp-dalvik`：浅 submodule，锁定
  `android-4.4.4_r2` = `36e356c96640775f0a3f167bd2426ea0f0093b8b`，默认不编译、
  不链接（07 §1 红线）。
- `tools/verify_aosp_dalvik.py`：校验 HEAD commit 与四个锚点文件
  （`opcode-gen/bytecode.txt`、`libdex/DexOpcodes.h`、`libdex/InstrUtils.cpp`、
  `NOTICE`）的 SHA-256；带 `--self-test`。
- CTest：`tools.aosp_dalvik_baseline_self_test`、`tools.aosp_dalvik_baseline_current`。
- `THIRD_PARTY_NOTICES.md` 增补 Apache-2.0 条目。
- `capabilities.toml` 新增 `dexvm.aosp_baseline = complete`。

## 验收（机器可判定）

- `ctest --preset dev -R tools.aosp_dalvik` 2/2 通过。
- submodule HEAD 漂移或锚点文件被改动时 `tools.aosp_dalvik_baseline_current` 失败。
