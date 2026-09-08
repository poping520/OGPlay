# Android guest 系统库数据目录

发行版按 API 目录存放来自 AOSP 源码构建的系统库。当前已纳入 Android 4.4.4/API 19
的 `aosp_arm-user` 五个 Bionic 库、AOSP OpenSSL 构建的 `libcrypto.so`，以及 ICU4C
运行库 `libicuuc.so`/`libicui18n.so` 及其 ABI/STLport 依赖
`libgabi++.so`/`libstlport.so`；
`run-apk` 根据所选 Profile 自动读取 `19/lib/`，不接受
外部系统库目录。

`19/manifest.json` 是发行 payload 的机器可读事实源，保存构建、逐库 ELF/哈希、依赖和
NOTICE 映射，以及 BootDex 的 recipe、输入和 DEX/JAR 身份；`19/source-manifest.xml` 固定
参与构建的源码 revision。Bionic-only 构建没有生成 build fingerprint，因此清单以 `null`
明确表示未知，不以推测值代替。

BootDex 的内容选择事实源是 `tools/bootdex/api19.json`；生成命令为
`python3 tools/bootdex/build_bootdex.py build`。运行时全量加载 jar 内 class_def。
DVM-107 包含 774 类：core.jar 745、framework.jar 12、Conscrypt 16，以及原版 Java
源码编译的 ArrayUtils 1。来源/哈希独立记录于 manifest；构建要求见
[工具说明](../../tools/README.md)。

提交或发布前运行：

```text
python tools/validate_android_payload.py --root data/android/19
```

校验器要求精确的五个 pinned Bionic 库、`libcrypto.so`/`libogplay_jni.so`、两个 ICU4C
运行库及其 ABI/STLport 依赖（共四个库）的闭集，复核体积、SHA-256、ELF32/little-endian/ARM/DYN 身份、
并逐库解析 SONAME/DT_NEEDED，复核目标 AOSP tag、构建目标、源码仓库 clean/tag 状态和 NOTICE；同时复核 BootDex recipe、
精确 class descriptor 集与 canonical JAR。

通常设备提取物只作为开发期 ABI oracle。DVM-105 曾按用户授权临时使用设备
libcrypto.so 与 conscrypt.jar 中的 AES 字节码；Conscrypt 的临时来源仍单列于
`boot_dex.sources`。`libcrypto.so` 已替换为 AOSP
`platform/external/openssl` 在 `android-4.4.4_r2.0.1` 的
`dd1da36b0baa39942f0aef42c4712ef0ad628a83` 源码构建产物；其 hash、许可证、
来源和 pinned manifest 的 `libraries` 条目共同记录。构建器只消费
显式准备的 `data/android/19/lib/libcrypto.so`，不从 `.local` 或设备提取目录自动复制。

JNI 桥构建：`python tools/bootdex/build_bootdex.py build-guest-jni`，然后重建 BootDex
并执行 payload 校验。固定使用 NDK r25c ARMv7 API 19 工具链；输入位置和边界见
`src/guest/crypto/MODULE.md` 与 `src/guest/icu/MODULE.md`。
DVM-106 的 Certificate/Harmony Java 从同一 pinned core.jar 选入；RSA/ECDSA 验签
复用该 JNI 桥和 manifest 的 `libraries` 条目，不另建证书库、配置或构建脚本。

本地 oracle 使用 `tools/import_bionic_oracles.ps1 -SourceRoot <目录>` 导入到被 Git 忽略的
`.local/bionic-oracle/`。工具只保存 API、相对路径、ELF 类型、体积和 SHA-256，不记录
提取设备、外部绝对路径或凭据；`-ValidateOnly` 可复核导入结果。
