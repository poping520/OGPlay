# Input · 通用 PC 操控映射

设计见 [Input 设计](../../design/input/README.md)，决策见
[ADR-0133](../../adr/session.md#adr-0133)。本目录按设计第 12 节的 P0 拆分，编号 `INP-N`。

| 任务 | 内容 | 依赖 |
| --- | --- | --- |
| [INP-1](INP-1.md) | input 核心：`ControlMapper`、统一触点分配、`direct`/`button`/`stick`/`key`/`mode` 原语、默认配置 | — |
| [INP-2](INP-2.md) | 配置数据：Profile `[input]` schema、类型模板目录、分级装载与合并、用户覆盖文件 | INP-1 |
| [INP-3](INP-3.md) | run-apk 接线：替换现有触摸路径、MCP 手势并存、失焦/后台/尺寸变化取消、端到端测试 | INP-1、INP-2 |
| [INP-4](INP-4.md) | 按键提示叠加层与 P0 实机验收 | INP-3 |
