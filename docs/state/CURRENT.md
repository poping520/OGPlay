# 当前状态

更新：API 19 BootDex 已扩展为 46 个 class_def；PvZ 已越过
`Class.desiredAssertionStatus()`，当前首错为 `sun.misc.Unsafe`。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP libc/libm/libdl/libstdc++/libz 和 BootDex，来源、哈希、ELF/DEX、NOTICE 与 staging
  受检。API 22/23 尚未纳入。GUI 支持游戏选择、启动和删除。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。DVM-100 以 `tools/bootdex/api19.json` 的精确类列表从
  pinned AOSP jar 双次重组 canonical JAR；当前含 EnumSet 11 类及 Executors fixed-pool
  相关 35 类，后者仅完成制品选类，并行线程池语义尚未闭合。
- **Java/Android**：文件流、资源 XML、Object streams 有界对象图、Locale、URL/form codec、
  Observer、Intent/IntentFilter、Context 私有文件和现有平台 enum 已闭合；完整 NIO、完整
  XmlPullParser、Externalizable/custom serialization、系统服务长尾仍按真实命中扩展。
- **DexVM**：稳定 linker metadata、`MethodShape`、owner state/GC、反射基础、guest 线程、
  monitor、switch/threaded 共享语义已建立。解释执行仍由 `VmExecutionLock` 串行，threaded
  生产默认关闭。
- **Title**：PvZ 已进入 `AtomicInteger.<clinit>`，当前缺少 `sun.misc.Unsafe`；Tales 首错为
  `android.location.LocationListener`。
  A6/DH exact、长运行 gate 与 threaded 默认裁决未闭合，见
  [DVM-47](../tasks/dexvm/DVM-47.md) 和 [WU-0231](../tasks/m5/WU-0231.md)。

## 最近验证

- 2026-09-05 macOS Release：BootDex 46 个 class_def 全量装载及 EnumSet 回归 1/1、224
  断言；builder/check/payload 门禁通过；`Class.desiredAssertionStatus()` 编译及 PvZ 实跑
  通过，首错前移至 `sun.misc.Unsafe`。
- 2026-09-04 Windows Release：URL、Android Support ownership、lazy hierarchy、主 Looper、
  scheduler 和 Intent 链定向回归通过。
- 2026-09-03 macOS/Windows：A5 title-flow、资源/Locale、Object streams、Context 文件流和
  Observer 定向回归通过。

## 下一步

1. 闭合 PvZ Executors 的 Unsafe/Clock/park 边界，再处理 NetworkImpl 与 Tales
   LocationListener，完成 DH 主菜单 Scenario gate。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate。
3. 出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- OGPlay 是老游戏兼容层，不是完整 Android；Binder/system_server、完整 framework、任意联网、
  SQLite 或任意 title 全流程不因能力账本 `complete` 而成立。
- guest 时间、键盘/IME 与长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)；缺失能力继续记账并
  明确失败。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics](../tasks/diagnostics/WU-DIAG-01.md) ·
[Playbook](../playbook/README.md)
