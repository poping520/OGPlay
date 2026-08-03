# AOSP Bionic 数据目录

发行版最终在 `19/`、`22/`、`23/` 中存放来自 AOSP 构建的 armeabi-v7a/arm64-v8a 系统库，
并为每个文件记录相对路径、体积、SHA-256、Android 版本、构建指纹、来源和许可证。

设备提取物只能作为开发期 ABI 行为 oracle，禁止进入本目录和发行包。M2 在来源清单与
哈希校验器完成前，不提交任何 `.so` 或 linker。

本地 oracle 使用 `tools/import_bionic_oracles.ps1 -SourceRoot <目录>` 导入到被 Git 忽略的
`.local/bionic-oracle/`。工具只保存 API、相对路径、ELF 类型、体积和 SHA-256，不记录
提取设备、外部绝对路径或凭据；`-ValidateOnly` 可复核导入结果。
