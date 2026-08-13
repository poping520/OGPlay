# DVM-6 · loader.dex_code 指令流/try-catch/静态初始值受检读取

## 目标（一句话）

在 DEX L1 之上补齐 L2 受检读取：方法指令 code-unit 流、try/catch 处理器表与
类静态初始值 encoded_array，全部越界/错位/非法索引在读取期明确失败。

## 依赖

- `loader.dex_l1`（存量）、DVM-5（dexasm 夹具管线）
- AOSP 参考（07 §2 模式 B）：`libdex/DexFile.h`（code_item 布局）、
  `DexCatch.h`（encoded_catch_handler 迭代）、`Leb128.h`（sleb128）、
  `DexSwapVerify.cpp`（反例规则面）

## 变更

- `include/ogplay/loader/dex_code.h` + `src/loader/dex_code.cpp`：
  - `ReadDexMethodCode`：指令流受检复制；tries 非空、有序、不重叠、落在
    指令流内；handler_off 必须命中 encoded handler 列表真实条目偏移；
    handler type 索引与地址受检。分支目标/payload 指令级校验按模块边界
    留给 dexvm 链接预检。
  - `ReadDexStaticValues`：encoded_array 解码，支持 11 种静态值形态，
    float/double 右零扩展、int 族符号扩展语义对照 dex-format；annotation/
    array/enum 等不支持形态明确失败。
- `tests/dexvm/dex_code_tests.cpp`：正例（divide try 块、switch payload、
  静态值 41/"fixture"）+ 反例（offset 越界、header 谎报指令数、字节尺寸
  失配、handler 地址越界）。
- `src/loader/MODULE.md` 同步；`capabilities.toml` 新增 `loader.dex_code`。

## 验收（机器可判定）

- `ctest --preset dev -R "dex_code"` 全过；full CTest 无回归。
