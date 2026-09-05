# 当前状态

更新：API 19 BootDex 已可重复生成，PvZ 真实越过 EnumSet，当前首错为
`Executors.newFixedThreadPool(I)`。

## 当前能力

- **运行与发行**：`run-apk` 按 exact Profile API 选择 bundled data；API 19 已内置 pinned
  AOSP libc/libm/libdl/libstdc++/libz 和 BootDex，来源、哈希、ELF/DEX、NOTICE 与 staging
  受检。API 22/23 尚未纳入。GUI 支持游戏选择、启动和删除。
- **BootDex**：Boot/Application DexUnit、unit-local 常量池、精确 intrinsic method overlay
  和 jar 内 class_def 全量装载已完成。DVM-100 以 `tools/bootdex/api19.json` 的精确类列表从
  pinned AOSP jar 双次重组 canonical JAR；当前只含 EnumSet 闭包 11 类。
- **Java/Android**：文件流、资源 XML、Object streams 有界对象图、Locale、URL/form codec、
  Observer、Intent/IntentFilter、Context 私有文件和现有平台 enum 已闭合；完整 NIO、完整
  XmlPullParser、Externalizable/custom serialization、系统服务长尾仍按真实命中扩展。
- **DexVM**：稳定 linker metadata、`MethodShape`、owner state/GC、反射基础、guest 线程、
  monitor、switch/threaded 共享语义已建立。解释执行仍由 `VmExecutionLock` 串行，threaded
  生产默认关闭。
- **Title**：PvZ 已执行 `EnumSet.allOf`/`MiniEnumSet.complement`；Tales 当前首错为
  `android.location.LocationListener`。A6/DH exact、长运行 gate 与 threaded 默认裁决未闭合，
  见 [DVM-47](../tasks/dexvm/DVM-47.md) 和 [WU-0231](../tasks/m5/WU-0231.md)。

## 最近验证

- 2026-09-05 macOS Release：BootDex builder/payload 门禁通过；EnumSet switch+threaded
  84/84；重组 DEX SHA-256 保持 `0efe25ed…`；PvZ 越过 EnumSet 后停于 Executors。
- 2026-09-04 Windows Release：URL、Android Support ownership、lazy hierarchy、主 Looper、
  scheduler 和 Intent 链定向回归通过。
- 2026-09-03 macOS/Windows：A5 title-flow、资源/Locale、Object streams、Context 文件流和
  Observer 定向回归通过。

## 下一步

1. 处理 PvZ Executors/NetworkImpl 与 Tales LocationListener，完成 DH 主菜单 Scenario gate。
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
