# Android guest 系统库数据目录

发行版按 API 目录存放来自 AOSP 源码构建的系统库。当前已纳入 Android 4.4.4/API 19
的 `aosp_arm-user` 五库发行集；`run-apk` 根据所选 Profile 自动读取 `19/lib/`，不接受
外部系统库目录。

`19/manifest.json` 是唯一机器可读事实源，保存构建、逐库 ELF/哈希、依赖和 NOTICE
映射；`19/source-manifest.xml` 固定参与构建的源码 revision；`19/notices/` 每库一份构建
系统生成的 NOTICE。Bionic-only 构建没有生成 build fingerprint，因此清单以 `null`
明确表示未知，不以推测值代替。

提交或发布前运行：

```text
python tools/validate_android_payload.py --root data/android/19
```

校验器要求精确的五库闭集，复核体积、SHA-256、ELF32/little-endian/ARM/DYN 身份、
目标 AOSP tag、构建目标、源码仓库 clean/tag 状态和逐库 NOTICE。

设备提取物只能作为开发期 ABI 行为 oracle，禁止进入本目录和发行包。

本地 oracle 使用 `tools/import_bionic_oracles.ps1 -SourceRoot <目录>` 导入到被 Git 忽略的
`.local/bionic-oracle/`。工具只保存 API、相对路径、ELF 类型、体积和 SHA-256，不记录
提取设备、外部绝对路径或凭据；`-ValidateOnly` 可复核导入结果。
