# DVM-47 · GC-B exact-title 与长运行 gate 复验

## 目标（一句话）

以 A5/A6/DH 三轮 exact gate 和至少一次真实长运行回收，决定 `dexvm.gc` 能否推进为 complete。

## 依赖

- DVM-42..46
- `docs/design/dexvm/09-gc.md` §11、§12
- `docs/playbook/SCENARIOS.md`

## 交付

- 新增 `asphalt6.gc_long` 长运行 Scenario 和 `dungeonhunter.bootstrap` 启动 Scenario；
  DH 当前只断言可见帧与无 guest fault，不把未稳定的宿主呈现时点伪装成 golden。
- 同步 `MODULE.md`、`CURRENT.md`、`capabilities.toml` 与任务索引。
- 本地证据保存在 `.local/evidence/gc-b/`，不进入版本库。

## 本轮机器证据（2026-08-19）

- A5 `asphalt5.title_flow`：默认 75% 水位连续 3/3 通过，均为
  `468/468000`、SHA-256 `f91150b4...`、无 fault、clean shutdown。
- A5 强制回收探针：临时 Profile 使用 16 MiB 堆与 1% 水位，Scenario 通过且保持
  同一 `f91150b4...`；`runtime.dexvm.gc` 日志中 `freed_bytes`、
  `freed_objects` 多轮非零，证明真实 title 已执行 GC-B。
- A6 `asphalt6.bootstrap`：在进入 GC gate 前由 APS-4 的通用 strict loader 拒绝，
  原因是 APK `lib/armeabi-v7a/libasphalt6.so` 的 DT_SONAME 与 inventory identity
  不一致；长运行 Scenario 因同一前置阻断未执行。
- DH：两轮到达可见帧且 clean；第三轮同一宿主 step 预算下呈现序列由 100 漂移到 97，
  触发 frame budget/golden 不一致。无 guest fault，但尚不满足三轮逐位持平。
- Scenario schema/current 校验通过；DVM-42..46 的 GC 单元/DEX 夹具通过。

## 状态裁决

**受阻，不能验收为完成。** GC-B 机制与真实 A5 强制回收已经兑现，但设计规定的
A5/A6/DH 三轮持平和长运行水位回落尚未全部成立。因此 `dexvm.gc` 保持 `partial`，
不修改 APS-4 strict loader 契约，也不增加 A6/DH 专属分支来绕过门禁。

## 解除条件

1. 通用修复或重新确认 A6 native identity/DT_SONAME 契约后，A6 bootstrap 三轮通过；
2. 将 DH 的呈现时点纳入确定性控制后，DH 三轮同 golden 通过；
3. `asphalt6.gc_long`（或经设计认可的等价 exact title 长运行 gate）证明 GC 至少发生
   一次且回收后水位下降；
4. 以上满足后把本任务与 `dexvm.gc` 单调推进为 complete。
