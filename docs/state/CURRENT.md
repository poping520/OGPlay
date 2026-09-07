# 当前状态

更新（2026-09-07）：[DVM-107](../tasks/dexvm/DVM-107.md) 完成 Activity 组件查询与
framework 值类迁移并交付。此前 DVM-105 为 `a2bb87b2`、DVM-106 为 `480d2a54`。

## 当前能力

- **运行与发行**：按 exact Profile API 选择 bundled data；API 19 内置 pinned AOSP
  五库、774 类 BootDex 与 ICU4C 51.1。ROM libcrypto 已按用户再次授权恢复到本地临时使用，
  未纳入 Git，哈希与清单一致。JNI 桥源码为 src/guest/crypto/crypto_jni.c；构建器仍不自动恢复设备库。
  自行构建替换后须更新来源/哈希并复验；API 22/23 尚未纳入。制品身份见
  [payload manifest](../../data/android/19/manifest.json)，bootdex.jar 继续不提交。
- **Cipher**：AES 128/192/256；ECB/CBC NoPadding/PKCS5Padding、CTR/NoPadding，
  裸 AES 默认 ECB/PKCS5Padding。Java 算法归 BootDex，11 个 native 进入 guest OpenSSL；
  分段/原位/异常、IV reset、OS 随机源、真实线程、GC/teardown 受检。
- **Certificate**：Certificate/X509Certificate/CertificateFactory、Harmony ASN.1/X.509、
  javax 旧 API 和 PkiPath/PKCS7 编解码执行原版 Java，普通 verify 无 overlay。
  精简 SignatureSpi 经 ARM EVP 验证 RSA PKCS#1 v1.5/ECDSA 与 SHA1/224/256/384/512
  的 10 个组合，消息最多 1 MiB。DER/PEM、长序列号、名字/有效期/扩展、公钥编码、
  证书集合、链签名/错误公钥/篡改/未知算法和 GC 后复用受检。公钥保留 Harmony 编码型
  fallback；不代表 RSA/EC KeyFactory、签名生成、PKIX 信任、系统 CA/撤销或 TLS。
- **集合与流**：List/Collection/Map 家族、普通 atomic/AQS、工具/事件/beans、内存/包装
  IO、Reader/Writer、X500 和 key spec 来自 API 19 DEX；普通字段/数组为唯一状态。
  JNI 嵌套数组使用同一 VM 类型关系，拒绝不兼容写入。弱 referent 清空并入队；对象流
  保持源身份和共享 handle，Externalizable 协议 2 支持显式 UID/公共构造/真实回调。
- **日期与值边界**：Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、
  Date/Calendar/TimeZone 执行 BootDex。固定 ICU formatter 管理逻辑 token，标准六字符集
  与 Locale 大小写复用 ICU。BigInt/NativeBN 扩展至 17 个值原语，支持长整数编码转换，
  单次输入最多 1 MiB；其余 18 个 native 明确失败。
- **VM 与平台**：Unsafe 逻辑位置、CAS、GC 强边、park/unpark 和统一 Clock 已建立；
  一个 guest 线程对应一个宿主线程，解释执行由 VmExecutionLock 串行，threaded 默认关闭。
  文件/VFS、资源 XML、Locale、URL/form codec、Intent/Context 与平台 enum 保持。
- **framework 值类**：Pair、五种 SparseArray、ComponentName/CREATOR、Parcelable 接口、
  ContainerHelpers/ArrayUtils 和 PrintWriter 执行 Java；旧 Sparse 侧表和迁移类声明已删除。
  ArrayUtils 从固定原始 Java 源用本地 AOSP dx 编译，其余来自 pinned jar；来源单独记账。
- **Activity 身份**：getLocalClassName/getComponentName/getPreferences 与每实例 Intent
  已接通，根启动保留 alias 名，普通切换读取真实 ComponentName；setIntent 不改组件身份。
  UTF-16 append 依赖已补。一般组件解析和非根 alias 切换仍未扩展。
- **Title**：用户报告的 PvZ getLocalClassName 缺口已补，尚未重跑游戏定位下一缺口。
  Tales 首错 LocationListener。A6 既有 gc_long
  三轮各 3000 帧、无 guest fault 且 clean shutdown；本轮未重跑游戏 gate。

## 最近验证

- DVM-107：774 类全链接、无普通算法 overlay、双后端值类/Activity/字符串边界通过；
  根 alias 与实际切换 fixture 通过。BootDex 双次构建/check、日期审计与 builder 自测通过，
  最终定向回归 25 用例/8504 断言（含 Cipher/证书）及 5 项门禁通过；来源 pin 和重复
  声明反例均拒绝。另 3 个 staging 用例/17 断言及 staging payload 校验通过。
  记录 `.local/review/dvm107/`。

- 2026-09-06 DVM-105：双后端 NIST AES、两个 guest 线程各 16 次往返、资源回收通过；
  定向回归 139 用例/13653 断言，后续 GC 与所有权复验通过，日志 `.local/review/dvm105/`。
- DVM-106：Cipher/证书/X500/NativeBN/JNI/GC 定向回归 52 用例、11512 断言通过。
  760 类全链接、BootDex check、日期审计（42 类/47 native）、payload/staging 与 5 项
  定向构建/文档门禁通过，均为 libcrypto 撤回前证据；记录 `.local/review/dvm106/`。
- 临时文件来自已核对 SHA-256 的 MoKee API 19 ARMv7 设备，存放于
  `.local/android-device/20260906-cipher/`。手机已断开；本轮未做手机对照，正式发行前
  须自行构建替换临时制品。macOS Release 沿用既有 WARNINGS_AS_ERRORS=OFF，
  仅构建 ogplay_tests 及 ogplay 依赖；未跑全量测试或 Windows/Linux 验收。
- 已知门禁遗留：architecture.platform_boundaries 在既有 GUI process_manager.cpp:131
  平台分支失败，本轮未修改、未重跑该门禁。ADR 继续按 6 个主题维护，追加
  [0038](../adr/dexvm.md#adr-0038)，不新增独立 ADR 文件。

## 下一步与边界

1. 按现有 playbook 复跑 PvZ，确认越过 Activity 查询并定位下一缺口；本轮只做定向验收。
2. 后续自行构建 API 19 ARM libcrypto，更新来源/哈希并复验，替换当前本地临时制品。
3. Windows/Linux 验证固定 ICU 与 crypto 发行构建；处理既有 GUI 平台门禁。
4. 继续 DH 主菜单 gate；有可复用 fixture 后补 Diagnostics 外部触发验收。

OGPlay 是老游戏兼容层；complete 只覆盖登记范围。具名时区/历史 DST、完整大数与
formatter、对象流长尾、高争用集合、RSA Cipher、完整 JCA/TLS/系统服务仍未交付。
长期限制见 [KNOWN-ISSUES](KNOWN-ISSUES.md)。

索引：[DexVM](../tasks/dexvm/README.md) · [APK Startup](../tasks/apk-startup/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Playbook](../playbook/README.md)
