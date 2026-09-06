# 当前状态

更新：[DVM-103](../tasks/dexvm/DVM-103.md) 完成集合家族与 Externalizable 迁移，
BootDex 共 390 类，CollectionRuntime 和对应集合 intrinsic 已删除。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP libc/libm/libdl/libstdc++/libz、390 类 BootDex 与 ICU4C 51.1 数据，来源、哈希、
  ELF/DEX、NOTICE 与 staging 受检。API 22/23 尚未纳入。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。固定输入及 recipe 可确定性重建；当前 DEX/JAR
  SHA-256 分别为 `9093d7cb6acacba5cb0046bbcb3ff4ce0cdb427f54ee4b144d56e30c14e39630` /
  `a78badef86f6020129f0f2712834d2c04daa6d25cc7166f14eb3f13506c7e5e2`。
- **集合**：List/Collection/Map 及实现、视图、迭代器、Tree/Sorted/Navigable、Weak/Identity/
  Enum、concurrent 容器与 Arrays/Collections 来自 API 19 字节码；Observable/Observer、
  Random、ThreadLocal 一并迁入。普通字段/数组为唯一集合存储，弱 referent 清空并入队；
  高争用、定时/取消、XML 和完整集合序列化长尾未全量验收。
- **日期格式化**：Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、
  Date/Calendar/GregorianCalendar/TimeZone/SimpleTimeZone 由 BootDex 拥有。固定 ICU formatter
  使用 per-VM 逻辑令牌，clone、失效令牌、GC/teardown、整数 format/parse、Date 序列化受检。
  具名时区库/历史 DST、大数、double/digit-list formatter 与完整 ICU 查询仍明确失败。
- **Java/Android**：文件流、资源 XML、Object streams 有界对象图、Locale、URL/form codec、
  Intent/IntentFilter、Context 私有文件和平台 enum 已闭合。Externalizable 协议 2 支持显式
  UID、公共无参构造、真实回调、共享 handle 与尾部跳过；协议 1、默认 UID、对象流数组、
  任意私有 custom hooks、完整 NIO/XmlPullParser 与系统服务长尾仍 deferred。
- **Unsafe/DexVM**：逻辑字段/数组位置、int/long/ref CAS、GC 强边、单例权限、跳过构造分配、
  park/unpark 与统一 Clock 已建立。linker 延迟解析 intrinsic 的 BootDex 层级，Miranda
  分派不污染 own-member 反射。一个 guest 线程对应一个宿主线程，解释执行由
  `VmExecutionLock` 串行；Runtime 发布单执行通道事实 1，nanoTime 只读统一 Clock。
  threaded 生产默认关闭。
- **Title**：PvZ 关闭 survey 已越过 Externalizable，当前首错是
  `javax.security.auth.x500.X500Principal`，位置为 `BaseCore.isAppSigned`；这是 reached-fault，
  不是游戏 gate。Tales 首错为 `android.location.LocationListener`。A6 既有 `gc_long`
  同一 Scenario 三轮各 3000 帧、无 guest fault 且 clean shutdown；本轮未重跑 title gate。

## 最近验证

- 2026-09-06 DVM-103：集合/日期/对象流、GC、反射与 catalog 定向回归通过；另运行受影响的
  Thread、monitor、Unsafe、Android/Context/Intent 与 JNI 桥接定向测试。修复 Thread 的
  ArrayList 漏调构造器、弱引用入队后重复 enqueue 和 Externalizable 尾部对象跳过。
  详细数量与日志索引见 [DVM-103](../tasks/dexvm/DVM-103.md)。
- 390 类全链接；BootDex build/check、日期 audit（仍为 42 类/47 native）、builder self-test、
  source/staging payload、intrinsic layout、文档布局与能力单调性校验通过。
- 构建限制：macOS Release 沿用本地 `OGPLAY_WARNINGS_AS_ERRORS=OFF`（既有 minimp3 等告警），
  只构建受影响的 `ogplay_tests` 及其 `ogplay` 依赖；未运行全量测试，未验证 Windows/Linux。
- 门禁遗留：`architecture.platform_boundaries` 在既有
  `src/frontend/gui/process_manager.cpp:131` 平台分支失败；本轮未改该文件、未重跑该门禁。
- ADR 继续按 6 个主题维护，本轮在 DexVM 主题追加 [0033](../adr/dexvm.md#adr-0033)。

## 下一步

1. 按真实首错处理 PvZ `X500Principal`、NetworkImpl 与 Tales `LocationListener`。
2. 在 Windows/Linux 验证固定 ICU 构建；另行处理既有 GUI 平台分支门禁。
3. 继续 DH 主菜单 gate；出现可复用停滞 fixture 时补 Diagnostics 外部触发子进程验收。

## 边界

OGPlay 是老游戏兼容层；能力账本的 complete 仅覆盖登记范围，不代表完整 Android 或
任意 title 可玩。长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)，缺失能力继续记账并明确失败。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics](../tasks/diagnostics/WU-DIAG-01.md) ·
[Playbook](../playbook/README.md)
