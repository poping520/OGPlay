# 任务：OGPlay 能力探针 APK

目标是用独立的 API 19 ARMv7 APK 产出带语义断言、调用表和 ledger 的能力报告。

运行依赖 [Probe 模块](../../samples/probe/MODULE.md)、API 19 ROM 和受检 OGPlay CLI。本次涉及 APK 和 runner，能力缺口记录在运行报告中。

## 当前状态与下一步

阶段 1–4 的探针及 runner 已接入。验证入口见 [Probe 操作手册](../playbook/PROBE.md)。

阶段 5 待推进。

1. 对游戏语料做 DEX 调用频率统计，生成高频未覆盖 API 清单。
2. 增加解释器、分配与 GC、GL draw call 和帧间隔探针的性能页及基线。
3. 用同一 APK 在真实 API 19 ARM 设备或模拟器上生成报告，与 OGPlay 对比。
4. 通过 MCP 注入真实输入并做截图比对。

## 验收边界

配置 a、b 和两种解释器可出完整报告，`selftest` 验证崩溃、超时和跨进程恢复。语料、性能、真机差分和真实输入截图的验收随阶段 5 推进。
