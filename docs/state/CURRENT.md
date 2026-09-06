# 当前状态

更新：[DVM-102](../tasks/dexvm/DVM-102.md) 已修复独立验收发现的问题并完成 macOS 定向复验。API 19 日期格式化家族已迁入
pinned BootDex，发行制品由 47 类扩展为 89 类；旧 `java_text.cpp` 与日期/日历
C++ 行为副本已删除。PvZ exact 入口已越过 `DateFormat.format(Date)`，新首错为
`java.io.Externalizable`。
运行时已实际链接固定 ICU4C 51.1.0.1，哈希校验的数据嵌入程序；手写区域数据和数字算法已删除。
默认时区、解析、Locale、Matcher、catalog 及 BootDex 静态字段初始化顺序缺陷已修复。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP libc/libm/libdl/libstdc++/libz、89 类 BootDex 与 ICU4C 51.1 数据，来源、哈希、
  ELF/DEX、NOTICE 与 staging 受检。API 22/23 尚未纳入。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。DVM-100 使用固定 AOSP jar 与精确 recipe
  双次重组 canonical JAR；当前 DEX/JAR SHA-256 分别为
  `54cf68c55c9571d8cca343b2f7719996c5b2fcbeb137fa4edf58cd10cc54bd4e` /
  `00266a1c22c279310d422d8572f06fb4ebd1222fbb6dd24fc15b36c4fdb7e668`。
- **日期格式化**：Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、
  Date/Calendar/GregorianCalendar/TimeZone/SimpleTimeZone 由 pinned API 19 字节码拥有。
  per-VM formatter 使用逻辑令牌，clone、失效令牌、GC sweep 和 teardown 受检；
  Date 序列化、区域数据、默认时区与整数 format/parse 的既有及独立反例均通过。
  具名时区库/历史 DST、大数、double/digit-list formatter 与完整 ICU 查询仍明确失败。
- **Java/Android**：文件流、资源 XML、Object streams 有界对象图、Locale、URL/form codec、
  Observer、Intent/IntentFilter、Context 私有文件和现有平台 enum 已闭合；
  Externalizable/custom serialization、完整 NIO/XmlPullParser 和系统服务长尾仍 deferred。
- **Unsafe**：受检逻辑字段/数组位置、int/long/ref CAS 与读写、GC 引用强边、
  单例权限、绕过构造器分配、复用 Thread 的 park/unpark 已实现；epoch deadline 转入
  统一单调 Clock。
- **DexVM**：稳定 linker metadata、`MethodShape`、owner state/GC、反射基础、guest 线程、
  monitor、switch/threaded 共享语义已建立。解释执行仍由 `VmExecutionLock` 串行，
  threaded 生产默认关闭。
- **Title**：PvZ 已越过日期格式化，当前缺少 `java.io.Externalizable`；Tales 首错为
  `android.location.LocationListener`。A6 `gc_long` 同一 Scenario 三轮通过，每轮 3000 帧、
  无 guest fault 并 clean shutdown。其他 title gate 见 [DVM-47](../tasks/dexvm/DVM-47.md)。

## 最近验证

- 2026-09-06 [ADR 文档整合](../adr/README.md)：32 条决策归并为 6 个主题文件，保留编号与
  决策沿革，引用改为章节锚点；仅做正文保全、UTF-8、链接及差异静态检查，无运行时变化。
- 2026-09-06 工具整合：日期闭包清单与审计并入 `api19.json` / `build_bootdex.py audit`，
  删除独立文件与重复 CTest 入口。统一自测、真实闭包审计、build/check、payload 和能力
  单调性定向 3/3 CTest 通过；89 类 BootDex 的 DEX/JAR 哈希不变。
- 2026-09-05 独立验收修复：日期、Locale、Matcher、catalog、Date serialization、EnumSet、
  Unsafe 与 GC 定向复验通过；BootDex check、42 类/47 native audit、源码及 staging payload
  校验通过。39 项定向 CTest 38 项通过（唯一失败见下）；最后日期补验 16/16、596/596
  断言通过。新构建关闭 survey 复跑 PvZ，仍越过 DateFormat 并停于 Externalizable。
- 构建限制：当前 macOS Release 因既有 minimp3 等告警，以本地
  `OGPLAY_WARNINGS_AS_ERRORS=OFF` 构建 `ogplay_tests` / `ogplay`；新 ICU backend、binding 和
  linker 改动另行通过 Werror 编译检查。未运行全量测试，Windows/Linux 构建未在本机验证。
- 门禁遗留：`architecture.platform_boundaries` 在 HEAD 已有的
  `src/frontend/gui/process_manager.cpp:131` 平台分支失败；该文件本批未改。
  intrinsic layout 与能力单调性门禁通过；这不等于全仓架构门禁通过。
- 2026-09-05 macOS Release：DVM-102 定向 6/6，390/390 断言；Locale、Calendar、Date
  serialization、intrinsic state table、BootDex EnumSet 和 Unsafe 定向回归通过。
- 2026-09-05 macOS：BootDex build/check、日期家族 audit、payload/staging、capability monotonic
  与 intrinsic layout 门禁通过。固定 ICU4C 51.1.0.1 源码已在当前 Clang 上完整构建。
- 2026-09-05 exact 运行：A6 `gc_long` 3/3 通过；PvZ 关闭 survey 实跑越过
  `DateFormat.format(Date)`，停于 `java.io.Externalizable`。后者是 reached-fault，不是游戏 gate。

## 下一步

1. 后续在 Windows/Linux 验证固定 ICU 构建；另行处理已有 GUI 平台分支门禁。
2. 再按真实首错处理 `java.io.Externalizable`、NetworkImpl 与 Tales `LocationListener`。
3. 继续 DH 主菜单 gate；出现可复用停滞 fixture 时补 Diagnostics 外部触发子进程验收。

## 边界

- OGPlay 是老游戏兼容层，不是完整 Android；Binder/system_server、完整 framework、任意联网、
  SQLite 或任意 title 全流程不因能力账本 `complete` 而成立。
- guest 时间、键盘/IME 与长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)；缺失能力继续记账并明确失败。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics](../tasks/diagnostics/WU-DIAG-01.md) ·
[Playbook](../playbook/README.md)
