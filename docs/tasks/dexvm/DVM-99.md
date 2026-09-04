# DVM-99 · API 19 Boot DEX + EnumSet Pilot

## 目标

在一个 WU 内建立 curated API 19 Boot DEX 混合执行链，并让 PvZ 真实执行 libcore
`EnumSet.allOf` 后越过 `Ljava/util/EnumSet;` 类缺失。

## 依赖

- ADR-0017、DVM-94～96、DVM-98；
- `.local/docs/1.md`、`.local/docs/2.md`；
- AOSP `android-4.4.4_r2.0.1` libcore/core.jar 与 Dalvik DEX 035；
- `data/android/19/framework/bootdex.jar`。

## 交付与边界

- `bootdex.jar` 成为 API 19 bundled payload 的必需文件，来源、jar/dex 哈希、DEX 035
  header、class 数量和 NOTICE 受机器校验。
- runtime 解包唯一 `classes.dex`，linker 为 Boot/Application 建立独立 DexUnit、常量池和
  resolution cache；jar 内 11 个 class_def 全量装载，不设运行时 allowlist。
- 同类时 DEX 提供 class/field/hierarchy，已有签名精确的 intrinsic method 成为 overlay；
  未覆盖 native 方法 fail closed。应用平台前缀过滤与动态/multidex 本 WU 不扩张。
- `Enum.getSharedConstants` 提供按 ordinal 校验的 enum 常量 typed-array 强根缓存；不手写
  `EnumSet` 行为。
- 修正 DEX long shift 的 cat1 移位量规则，使 `MiniEnumSet.complement()` 可执行。

## 验收

- [x] switch/threaded 均验证 11 个 Boot DEX 类全量登记、DexUnit 归属、精确 method
  overlay、enum constants 缓存及 `EnumSet.allOf → MiniEnumSet(size=2)`；
- [x] bundled data 与 Android payload validator 将 `bootdex.jar` 视为必需受检制品；
- [x] macOS Release 受影响目标构建通过；
- [x] PvZ exact Profile 实跑越过原 `class is not available: Ljava/util/EnumSet;`，并真实
  经过 `EnumSet.allOf`/`MiniEnumSet.complement`；新首错为
  `Executors.newFixedThreadPool(I)`。

状态：已完成。
