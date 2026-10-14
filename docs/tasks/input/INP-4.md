# INP-4 · F1 提示叠加层与宿主快捷键保留

状态：完成（游戏窗口实按待用户验收）。依赖：INP-3。设计：[Input 设计](../../design/input/README.md) 第 10 节；
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

实现：input `ControlMapper::Hints`（当前基础模式、`*` 与按住叠加模式中有效绑定的区域及源）。
frontend `HostShortcutFilter`（F1/F2/Cmd/Win 与 Ctrl、Cmd 组合键截留，重复与抬起跟随按下归属；
Ctrl 单独按下仍交给 guest）与 `DrawControlHints`（呈现副本上画区域、键名与顶部提示条，内置
5×7 ASCII 字形，未复用 TrueType 回退字体以免前端加载字体载荷）。run-apk 在 `ControlMapper`
之前过滤宿主键；F1 切换下一帧生效；guest 帧、MCP 与编辑器截图仍取原帧。

验证（2026-10-14，macOS）：`INP-4` 用例覆盖 F1/F2 截留与自动重复、组合键先松修饰键、guest 键
在 Ctrl 加入后仍归 guest、Cmd 键、Reset；叠加层只改副本、短缓冲不越界、标签映射；`Hints`
随按住模式变化。用 DH 真实帧（1024×600）离线渲染叠加层，区域与键名对齐、可读。

缺口：macOS 拒绝向游戏窗口合成按键（无辅助功能权限），F1/F2 实按与窗口内截图未自动化验证，
交用户实测；叠加层切换不重绘已冻结的旧帧。
