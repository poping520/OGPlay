# 当前状态

更新：[DVM-105](../tasks/dexvm/DVM-105.md) 完成 Cipher AES；BootDex 共 566 类，
Conscrypt Java 经精简 guest JNI 调用 ARM libcrypto。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP 五库、566 类 BootDex 与 ICU4C 51.1 数据；按用户授权临时加入设备 libcrypto
  和源构建 JNI 桥，来源/哈希/ELF/NOTICE 单列受检。API 22/23 尚未纳入。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。固定输入及 recipe 可确定性重建；当前 DEX/JAR
  SHA-256 分别为 `750f26ab7b2945c7f729dc4aeb15ac4b5253e805141cb9e021f61153a9b5d7f7` /
  `d5d94f2b2dd8eb8e490fe5213e5f9526f019f51391ad645783fde74ea2d1ce4d`。
- **Cipher**：AES 128/192/256；ECB/CBC 的 NoPadding/PKCS5Padding、CTR/NoPadding，
  裸 AES 默认 ECB/PKCS5Padding。Java 算法归 BootDex，11 个 native 进入 guest OpenSSL；
  分段/原位/异常、IV reset、OS 安全随机源、真实线程及 token GC/teardown 受检。
  自设 seed、AES AlgorithmParameters 编码、RSA/证书/TLS 与完整 JCA 未纳入。
- **集合**：List/Collection/Map 及实现、视图、迭代器、Tree/Sorted/Navigable、Weak/Identity/
  Enum、concurrent 容器与 Arrays/Collections 来自 API 19 字节码；Observable/Observer、
  Random、ThreadLocal 一并迁入。普通字段/数组为唯一集合存储，弱 referent 清空并入队；
  高争用、定时/取消、XML 和完整集合序列化长尾未全量验收。
- **日期格式化**：Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、
  Date/Calendar/GregorianCalendar/TimeZone/SimpleTimeZone 由 BootDex 拥有。固定 ICU formatter
  使用 per-VM 逻辑令牌，clone、失效令牌、GC/teardown、整数 format/parse、Date 序列化受检。
  具名时区库/历史 DST、大数、double/digit-list formatter 与完整 ICU 查询仍明确失败。
- **后续 Luni**：工具/事件/beans、同步器/普通 atomic、Choice/MessageFormat、内存/包装
  IO、Reader/Writer、X500 名字/DER 和指定 key spec 已迁入。标准六字符集与 Locale
  大小写复用固定 ICU；InputStreamReader 增量解码并持有源 monitor。BigInt/NativeBN
  只支持 ASN.1 标签键所需的 11 个最多 64 位原语，另 24 个 native 明确失败。
- **Java/Android**：文件/VFS、资源 XML、Object streams 有界对象图、Locale、URL/form
  codec、Intent/Context 与平台 enum 保持。普通流状态归 guest 字段/数组，对象流经
  guest source/sink 保留身份，删除 wrapper-adoption。Externalizable 协议 2 支持显式
  UID、公共构造、真实回调/共享 handle；协议 1、默认 UID、数组、任意私有 hooks、
  完整 Charset provider/数字 formatter/证书验证与系统服务长尾仍 deferred。
- **Unsafe/DexVM**：逻辑字段/数组位置、int/long/ref CAS、GC 强边、单例权限、跳过构造分配、
  park/unpark 与统一 Clock 已建立。linker 延迟解析 intrinsic 的 BootDex 层级，Miranda
  分派不污染 own-member 反射。一个 guest 线程对应一个宿主线程，解释执行由
  `VmExecutionLock` 串行；Runtime 发布单执行通道事实 1，nanoTime 只读统一 Clock。
  threaded 生产默认关闭。
- **Title**：PvZ 关闭 survey 已越过 X500Principal，当前首错是
  `java.security.cert.CertificateException`；这是 reached-fault，
  不是游戏 gate。Tales 首错为 `android.location.LocationListener`。A6 既有 `gc_long`
  同一 Scenario 三轮各 3000 帧、无 guest fault 且 clean shutdown；本轮未重跑 title gate。

## 最近验证

- 2026-09-06 DVM-105：双后端 NIST AES、填充/错误、两条 guest 线程各 16 次往返、
  真随机 IV 和资源回收通过；定向回归 139 用例/13653 断言，后续 GC 与类所有权复验
  分别 12 用例/1500 断言、4 用例/4611 断言。日志见 `.local/review/dvm105/`。
- 临时文件来自已核对 SHA-256 的 MoKee API 19 ARMv7 设备，存放于
  `.local/android-device/20260906-cipher/`；原 pinned core.jar/五库不变。手机现已断开，
  本轮未做真机对照；正式发行前须自行构建替换临时制品。
- 566 类全链接，普通 Cipher/集合方法无 intrinsic overlay；BootDex build/check、日期 audit
  （固定 42 类/47 native）、builder 自测、payload/staging、文档布局、能力单调性受检。
- 构建限制：macOS Release 沿用本地 `OGPLAY_WARNINGS_AS_ERRORS=OFF`（既有 minimp3 等告警），
  只构建受影响的 `ogplay_tests` 及其 `ogplay` 依赖；未运行全量测试，未验证 Windows/Linux。
- 门禁遗留：`architecture.platform_boundaries` 在既有
  `src/frontend/gui/process_manager.cpp:131` 平台分支失败；本轮未改该文件、未重跑该门禁。
- ADR 继续按 6 个主题维护，本轮在 DexVM 主题追加 [0035](../adr/dexvm.md#adr-0035)。

## 下一步

1. 按用户要求接入 Certificate/X509Certificate/CertificateFactory，再处理 NetworkImpl。
2. 在 Windows/Linux 验证固定 ICU 构建；另行处理既有 GUI 平台分支门禁。
3. 继续 DH 主菜单 gate；出现可复用停滞 fixture 时补 Diagnostics 外部触发子进程验收。

## 边界

OGPlay 是老游戏兼容层；能力账本的 complete 仅覆盖登记范围，不代表完整 Android 或
任意 title 可玩。长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)，缺失能力继续记账并明确失败。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics](../tasks/diagnostics/WU-DIAG-01.md) ·
[Playbook](../playbook/README.md)
