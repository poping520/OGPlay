# 当前状态

更新：[DVM-104](../tasks/dexvm/DVM-104.md) 完成后续 Luni 家族迁移，
BootDex 共 521 类；普通流与 atomic 算法转由 API 19 字节码执行。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP libc/libm/libdl/libstdc++/libz、521 类 BootDex 与 ICU4C 51.1 数据，来源、哈希、
  ELF/DEX、NOTICE 与 staging 受检。API 22/23 尚未纳入。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。固定输入及 recipe 可确定性重建；当前 DEX/JAR
  SHA-256 分别为 `1bb0ab430cba7e551fbcf38bb93cf2d534558a34c834b92ceec5ea8ad58a3513` /
  `1d861473de2eaf580e40e7705c1b816558938a7ff852100c0ac1966a54b943d0`。
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

- 2026-09-06 DVM-104：双后端工具/事件、同步器/atomic、字符集、内存流、X500 DER、
  NativeBN 令牌/GC 与文件/AssetManager 衔接定向验收；同期回归日期/集合/对象流、
  线程/monitor/Unsafe、NIO/反射和相关集成。数量与日志见 [DVM-104](../tasks/dexvm/DVM-104.md)。
- 521 类全链接，迁入普通方法无 intrinsic overlay；BootDex build/check、日期 audit
  （固定 42 类/47 native）、builder 自测、payload/staging、文档布局、能力单调性受检。
- 构建限制：macOS Release 沿用本地 `OGPLAY_WARNINGS_AS_ERRORS=OFF`（既有 minimp3 等告警），
  只构建受影响的 `ogplay_tests` 及其 `ogplay` 依赖；未运行全量测试，未验证 Windows/Linux。
- 门禁遗留：`architecture.platform_boundaries` 在既有
  `src/frontend/gui/process_manager.cpp:131` 平台分支失败；本轮未改该文件、未重跑该门禁。
- ADR 继续按 6 个主题维护，本轮在 DexVM 主题追加 [0034](../adr/dexvm.md#adr-0034)。

## 下一步

1. 按真实首错处理 PvZ 证书家族/NetworkImpl 与 Tales `LocationListener`。
2. 在 Windows/Linux 验证固定 ICU 构建；另行处理既有 GUI 平台分支门禁。
3. 继续 DH 主菜单 gate；出现可复用停滞 fixture 时补 Diagnostics 外部触发子进程验收。

## 边界

OGPlay 是老游戏兼容层；能力账本的 complete 仅覆盖登记范围，不代表完整 Android 或
任意 title 可玩。长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)，缺失能力继续记账并明确失败。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics](../tasks/diagnostics/WU-DIAG-01.md) ·
[Playbook](../playbook/README.md)
