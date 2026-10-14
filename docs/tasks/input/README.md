# Input · 通用 PC 操控映射

设计见 [Input 设计](../../design/input/README.md)，决策见
[ADR-0133](../../adr/session.md#adr-0133)、[ADR-0134](../../adr/session.md#adr-0134)。
编号 `INP-N`，按设计第 12 节分阶段。

| 任务 | 内容 | 依赖 | 状态 |
| --- | --- | --- | --- |
| [INP-1](INP-1.md) | input 核心：`ControlMapper`、统一触点分配、`direct`/`button`/`stick`/`key`/`mode`、默认配置 | — | 完成 |
| [INP-2](INP-2.md) | 配置数据：`[input]` schema、类型模板、分级装载与合并、用户配置文件 | INP-1 | 完成 |
| [INP-3](INP-3.md) | run-apk 接线：替换触摸路径、MCP 并存、取消规则、GUI 传入 `input.toml` | INP-1、INP-2 | 完成 |
| [INP-4](INP-4.md) | F1 提示叠加层与宿主快捷键保留 | INP-3 | 待开始 |
| [INP-5](INP-5.md) | 运行时操控会话：`control.*` 接口、冻结/恢复、热应用与写回、令牌 | INP-3 | 完成 |
| [INP-6](INP-6.md) | Web 按键编辑器（由 run-apk 提供） | INP-5 | 待开始 |
| [INP-7](INP-7.md) | 编辑器入口：GUI“按键设置”窗口、游戏内 F2、浏览器回退；P0.5 实机验收 | INP-4、INP-6 | 待开始 |
