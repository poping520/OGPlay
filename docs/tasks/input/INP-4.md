# INP-4 · F1 提示叠加层与宿主快捷键保留

状态：待开始。依赖：INP-3。设计：[Input 设计](../../design/input/README.md) 第 10 节；
决策：[ADR-0134](../../adr/session.md#adr-0134)。

目标：玩家在游戏窗口内能随时查看当前键位，宿主快捷键不再漏进 guest。

范围：
- `ControlMapper` 提供只读的当前布局查询（区域、绑定键名、当前模式），供绘制使用。
- frontend 在 guest 画面合成之后、窗口呈现之前绘制半透明区域与键名；F1 切换显示；
  不进入 guest 帧和默认 MCP `frame_capture` 截图。字体复用现有位图字体，不新增依赖。
- F1、F2 及带 Cmd/Ctrl 修饰的按键由 frontend 截留，不交给 `ControlMapper`、不传入 guest；
  F2 本任务只预留，由 INP-7 接入。

不含：编辑功能、`control.*` 接口。

验收：叠加层绘制与截图隔离、快捷键截留（含组合键按下后松开修饰键的顺序）有单元测试；
macOS 实机截图确认提示与区域对齐。
