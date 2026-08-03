# 模块：loader

## 职责

解析 APK/XAPK/APKM/APKS/目录、二进制 Manifest、DEX L1、ELF、动态链接、OBB 和引擎指纹。

## 公共 API

M2/M3 定义包识别、ELF 映射与链接命名空间接口。

## 不变量

- 输入不可信；所有偏移、长度和整数运算必须校验。
- `DT_NEEDED`、TLS、exidx、符号版本、init/fini 使用同一链接模型。
- 输出只描述事实，不猜测具体游戏身份。

## 禁止

- 不执行 guest 指令，不直接调用 HLE 或宿主图形。
- 不按游戏名、导出符号 if-else 选择启动路径。

## 测试

`tests/loader/` 的单元与畸形输入契约测试。

