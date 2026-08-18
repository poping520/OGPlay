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
