# APS-9 · Integration gates 与设计迁移收尾

## 目标（一句话）

用无 Profile APK fixture、reentrancy fixture 和现有 exact title gate 完成本次架构验收，
并把旧设计中的 Profile/root-so 启动假设标记为 superseded。

## 依赖

- APS-1..8
- `docs/design/apk-startup/08-verification.md`

## 设计锚点

- 01 §5
- 08 全篇
- 10 §6

## 变更

- 补齐 APK fixture matrix A–J；
- 选择至少一个现有 dexvm pilot title 跑三轮 exact Scenario；
- 记录实际 native explicit load 顺序；
- 清理仅为 root-preload 服务的 frontend dead path（若无 legacy consumer）；
- 在 `docs/design/entry-scope/`、`docs/design/dexvm/` 冲突位置增加指向本设计的
  superseded note，不改写历史验收事实；
- 同步相关 `MODULE.md`、capability ledger、`CURRENT.md`。

## 验收（机器可判定）

- 08 §2 A–J 全部通过；
- reentrancy gate 无 deadlock；
- no-profile generic APK 启动通过；
- exact title Scenario 连续三轮通过并 clean shutdown；
- `run-apk` 无 `so_sha256` 启动门禁；
- frontend 无显式 app root `JNI_OnLoad`；
- full CTest 全绿；
- 文档冲突均有 superseded 链接、没有两份同时自称权威的启动规则。

## Fixture matrix 与机器证据

| Case | 机器证据 |
| --- | --- |
| A/B | `minimal Application startup preserves order identity and native loads` 的 clinit/onCreate 子例 |
| C/D | `AndroidAppProcess keeps Activity native loads Java-driven` 的 clinit/onCreate 子例 |
| E | `DexVM System load APIs support nested JNI OnLoad Java reentry` 的 synthetic path 调用 |
| F | `native library loader appends dependency constructors and one explicit JNI_OnLoad` 的重复 load |
| G | DexVM→A JNI_OnLoad→Java callback→B load 的真实嵌套 fixture；记录顺序 A、B |
| H | 同一 DexVM fixture 的 missing name/path → `UnsatisfiedLinkError` |
| I | loader malformed/SONAME 负例与 selected-ABI inventory 隔离用例 |
| J | `Application failure prevents launcher construction and surface effects` |

## 结果

- A–J 由同一组 deterministic DEX/ELF/Manifest fixtures 覆盖；G 同时断言 A/B 各一次、
  无 pending exception、stop 后无运行 process，避免只用 timeout 猜测无死锁。
- frontend 源码 gate 同时锁定 `AndroidAppProcess::Create` + optional selector，并拒绝
  `MatchApkTitleProfile`、`PrepareApkProfileLaunch`、`InitializeJniLibrary` 与
  `so_sha256` 出现在 CLI 启动决策中；GUI 导入也不得回退 exact gate。
- `entry-scope` 与 DexVM integration 的冲突段已原位标记 superseded，并保留历史验收事实。
- exact gate 采用 DVM-31 已与改动前 HEAD 对照确认的 Main Menu 基线
  `f91150b40c053cd86d7cffd7ebb5afb20fe1135927bbf797be6c0fc036fec270`；旧
  `9ee57323…` 在本设计开始前已经漂移，不把它误记为 APS 回归。
- exact 第一轮暴露 rootless shell 未保留 `libstdc++.so` 动态依赖源；通用 loader 现拥有
  API19 system source，并只把应用 `DT_NEEDED` 可达闭包追加进 process。对应 fixture
  固定 system dependency constructors 与仅 root JNI_OnLoad 语义。
- `.local/evidence/aps9-asphalt5-r1..r3/` 三轮均为 468 frame/468000 tick、同一 golden、
  无 guest fault、suspend/resume 与 clean shutdown。三轮结构化 explicit load 顺序均为
  `1: libasphalt5.so`；`libstdc++.so` 等仅作为依赖初始化。
