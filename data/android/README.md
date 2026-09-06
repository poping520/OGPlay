# Android guest 系统库数据目录

发行版按 API 目录存放来自 AOSP 源码构建的系统库。当前已纳入 Android 4.4.4/API 19
的 `aosp_arm-user` 五库发行集；`run-apk` 根据所选 Profile 自动读取 `19/lib/`，不接受
外部系统库目录。

`19/manifest.json` 是发行 payload 的机器可读事实源，保存构建、逐库 ELF/哈希、依赖和
NOTICE 映射，以及 BootDex 的 recipe、输入和 DEX/JAR 身份；`19/source-manifest.xml` 固定
参与构建的源码 revision。Bionic-only 构建没有生成 build fingerprint，因此清单以 `null`
明确表示未知，不以推测值代替。

BootDex 的内容选择事实源是 `tools/bootdex/api19.json`；生成命令为
`python3 tools/bootdex/build_bootdex.py build`。运行时全量加载 jar 内 class_def。

提交或发布前运行：

```text
python tools/validate_android_payload.py --root data/android/19
```

校验器要求精确的五个 pinned 库及两个 Cipher 库闭集，复核体积、SHA-256、ELF32/little-endian/ARM/DYN 身份、
目标 AOSP tag、构建目标、源码仓库 clean/tag 状态和逐库 NOTICE；同时复核 BootDex recipe、
精确 class descriptor 集与 canonical JAR。

通常设备提取物只作为开发期 ABI oracle。DVM-105 曾按用户授权临时使用设备
libcrypto.so 与 conscrypt.jar 中的 AES 字节码；历史来源单列在
manifest.cipher_native / boot_dex.sources，不冒充原 AOSP clean build。
2026-09-07 用户撤回 ROM 版 libcrypto.so，当前源码 checkout 的运行 payload 缺少该库。
后续自行构建后更新来源、哈希和来源校验约束并重新验收；完整 payload 校验保持严格，
不以缺库状态冒充可发行制品。构建器仅消费显式准备的 data/android/19/lib/libcrypto.so，
不从 .local 手机提取目录自动复制。

JNI 桥构建：`python3 tools/bootdex/build_bootdex.py build-cipher`，然后重建 BootDex
并执行 payload 校验。输入位置、工具链要求见 src/guest/crypto/MODULE.md。
DVM-106 的 Certificate/Harmony Java 从同一 pinned core.jar 选入；RSA/ECDSA 验签
复用该 JNI 桥和 manifest.cipher_native，不另建证书库、配置或构建脚本。

本地 oracle 使用 `tools/import_bionic_oracles.ps1 -SourceRoot <目录>` 导入到被 Git 忽略的
`.local/bionic-oracle/`。工具只保存 API、相对路径、ELF 类型、体积和 SHA-256，不记录
提取设备、外部绝对路径或凭据；`-ValidateOnly` 可复核导入结果。
