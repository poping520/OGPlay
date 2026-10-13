# INP-1 · input 核心：ControlMapper 与 P0 原语

状态：完成。依赖：无。设计：[Input 设计](../../design/input/README.md) 第 3–7 节。

目标：在 `src/input` 提供纯逻辑、可确定测试的 `ControlMapper`：接收 hal 输入事件，
按当前模式的绑定驱动原语，在每个 guest 帧输出有序的触摸快照与按键输出。

范围：
- `ContactAllocator`：宿主触摸、鼠标、合成触点与外部（MCP）触点共用最多 16 个触点，
  pointerId 取最小空闲值且存续期间稳定，溢出拒绝并计数。
- 原语：`direct`（鼠标主键、宿主触摸）、`button`（tap/hold/repeat）、`stick`（四键组、
  固定原点、8 向）、`key`（Android keycode）、`mode`（toggle/hold）。
- 区域：九宫格锚点 + 偏移，装载时按 surface 解析，越界明确失败。
- 帧对齐：按下与抬起至少相隔一帧；同帧多次移动合并为一次 MOVE。
- 按键：未绑定键直通，已绑定键不直通；直通按下的键其抬起始终直通。
- 内置默认配置：鼠标/触摸直通、Esc→`KEYCODE_BACK`。
- session 增加 Android keycode 输出的投影。

不含：配置文件解析（INP-2）、run-apk 接线与 MCP 改造（INP-3）、滚轮/相对鼠标/手柄
（P1）。

验收：`tests/input/control_mapper_tests.cpp` 以帧级精确断言覆盖每个原语、触点上限、
模式切换、直通规则、取消与区域解析；session 投影测试覆盖 keycode 输出。

验证（2026-10-13，macOS dev 构建）：`tests/input/control_mapper_tests.cpp` 10 个用例通过，
`tests/input` 全部 37 个用例与 session 输入投影用例通过；`architecture.no_raw_output`、
`tools.capabilities_current` 通过。`architecture.platform_boundaries` 在改动前已因
`frontend/gui/process_manager.cpp` 失败，与本任务无关。
