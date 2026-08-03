# 模块：loader

## 职责

解析 APK/XAPK/APKM/APKS/目录、二进制 Manifest、DEX L1、ELF、动态链接、OBB 和引擎指纹。

## 公共 API

- `ParseElf32Arm(bytes)`：从不可信字节解析 little-endian ELF32/ARM ET_EXEC/ET_DYN，
  返回入口、ARM flags、程序头和未知 tag 不丢失的动态项事实模型。
- `ReadElf32DynamicInfo(bytes, image)`：只在 file-backed `PT_LOAD` 范围内解析动态字符串表、
  `DT_NEEDED` 与 `DT_SONAME`，重复、缺失、越界和未终止字符串均明确失败。
- 后续 Work Unit 在该事实模型上增加映射、符号、重定位和链接命名空间，不重复解析字节。

## 不变量

- 输入不可信；所有偏移、长度和整数运算必须校验。
- `PT_LOAD` 必须满足 file size ≤ memory size、guest 地址不回绕、文件范围有效且对齐同余。
- `PT_DYNAMIC` 最多一个、文件范围完整、条目尺寸正确并由 `DT_NULL` 终止。
- 动态虚拟地址必须能完整翻译到单个 file-backed `PT_LOAD`，不得读取 BSS 或猜测文件偏移。
- `DT_NEEDED`、TLS、exidx、符号版本、init/fini 使用同一链接模型。
- 输出只描述事实，不猜测具体游戏身份。

## 禁止

- 不执行 guest 指令，不直接调用 HLE 或宿主图形。
- 不按游戏名、导出符号 if-else 选择启动路径。

## 测试

`tests/loader/` 的单元与畸形输入契约测试。
