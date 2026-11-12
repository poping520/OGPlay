# Android guest 系统库数据目录

发行版按 API 目录存放来自 AOSP 源码构建的系统库。当前已纳入 Android 4.4.4/API 19
的 `aosp_arm-user` 五个 Bionic 库、AOSP OpenSSL 构建的 `libcrypto.so`/`libssl.so`，以及 ICU4C
运行库 `libicuuc.so`/`libicui18n.so` 及其 ABI/STLport 依赖
`libgabi++.so`/`libstlport.so`；
`run-apk` 根据所选 Profile 自动读取 `19/lib/`，不接受
外部系统库目录。

`19/manifest.json` 是发行 payload 的机器可读事实源，保存构建、逐库 ELF/哈希、依赖和
NOTICE 映射，以及启动类路径各 jar 的来源与哈希；`19/source-manifest.xml` 固定
参与构建的源码 revision。Bionic-only 构建没有生成 build fingerprint，因此清单以 `null`
明确表示未知，不以推测值代替。

`19/framework/` 是 API 19 启动类路径：`core.jar`、`conscrypt.jar`、`ext.jar`、
`framework.jar`、`framework2.jar`、`telephony-common.jar`、`android.policy.jar` 原样取自 AOSP 构建，`ogplay.jar`
放 OGPlay 自己的 guest Java 与进程内服务实现，并排在最前；资源 native 读取本地
`framework-res.apk` 与应用 APK，共用唯一资源核心。生成命令为
`python3 tools/bootdex/build_bootdex.py build`，运行时按顺序加载全部 jar；该目录不入库。
构建要求见 [工具说明](../../tools/README.md)。

提交或发布前运行：

```text
python tools/validate_android_payload.py --root data/android/19
```

校验器要求精确的五个 pinned Bionic 库、`libcrypto.so`/`libssl.so`/`libogplay_jni.so`、两个 ICU4C
运行库及其 ABI/STLport 依赖（共四个库）的闭集，复核体积、SHA-256、ELF32/little-endian/ARM/DYN 身份、
并逐库解析 SONAME/DT_NEEDED，复核目标 AOSP tag、构建目标、源码仓库 clean/tag 状态和 NOTICE；同时复核
启动类路径顺序、ROM jar 固定哈希与 ogplay.jar 布局。

通常设备提取物只作为开发期 ABI oracle。`libcrypto.so` 已替换为 AOSP
`platform/external/openssl` 在 `android-4.4.4_r2.0.1` 的
`dd1da36b0baa39942f0aef42c4712ef0ad628a83` 源码构建产物；其 hash、许可证（`libcrypto.so` 与
`libssl.so` 共用 OpenSSL NOTICE）、
来源和 pinned manifest 的 `libraries` 条目共同记录。构建器只消费
显式准备的 `data/android/19/lib/libcrypto.so`/`libssl.so`，不从 `.local` 或设备提取目录自动复制。

JNI 桥构建：`python tools/bootdex/build_bootdex.py build-guest-jni`，然后执行 payload 校验。固定使用 NDK r25c ARMv7 API 19 工具链；输入位置和边界见
`src/guest/crypto/MODULE.md` 与 `src/guest/icu/MODULE.md`。
DVM-106 的 Certificate/Harmony Java 来自 pinned core.jar；RSA/ECDSA 验签
复用该 JNI 桥和 manifest 的 `libraries` 条目，不另建证书库、配置或构建脚本。

本地 oracle 使用 `tools/import_bionic_oracles.ps1 -SourceRoot <目录>` 导入到被 Git 忽略的
`.local/bionic-oracle/`。工具只保存 API、相对路径、ELF 类型、体积和 SHA-256，不记录
提取设备、外部绝对路径或凭据；`-ValidateOnly` 可复核导入结果。
