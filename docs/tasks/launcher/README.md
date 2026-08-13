# Launcher · 主面板（游戏启动器）

设计根节点：[`docs/design/launcher/`](../../design/launcher/README.md)。本专项按设计
05 §3 的 `GUI-N` 编号实施，代码与 `src/frontend/gui/MODULE.md` 在实施后接管当前契约。

| WU | 一句话目标 | 依赖 | 状态 |
| --- | --- | --- | --- |
| [GUI-1](GUI-1.md) | SDL3 + ANGLE + Dear ImGui shell、双入口与有界冒烟 | — | 完成 |
| [GUI-2](GUI-2.md) | LibraryStore + GuiConfig 严格持久模型 | — | 完成 |
| [GUI-3](GUI-3.md) | manifest icon/label 与图标/名称提取链 | GUI-2 | 完成 |
| GUI-4 | 库网格、状态角标、空库引导与 CJK 字体 | GUI-1、GUI-2 | 待开始 |
| GUI-5 | 导入向导与 Profile/external 摘要 | GUI-3、GUI-4 | 待开始 |
| GUI-6 | LaunchPlan、子进程与运行结果呈现 | GUI-4 | 待开始 |
| GUI-7 | 设置、删除确认、错误呈现与全部契约收口 | GUI-5、GUI-6 | 待开始 |

当前下一项为 GUI-4；GUI-1..3 落地后的 Windows/MSVC 全量 CTest 以
`docs/state/CURRENT.md` 的滚动基线为准。
