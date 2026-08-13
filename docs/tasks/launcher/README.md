# Launcher · 主面板（游戏启动器）

设计根节点：[`docs/design/launcher/`](../../design/launcher/README.md)。本专项按设计
05 §3 的 `GUI-N` 编号实施，代码与 `src/frontend/gui/MODULE.md` 在实施后接管当前契约。

| WU | 一句话目标 | 依赖 | 状态 |
| --- | --- | --- | --- |
| [GUI-1](GUI-1.md) | SDL3 + ANGLE + Dear ImGui shell、双入口与有界冒烟 | — | 完成 |
| [GUI-2](GUI-2.md) | LibraryStore + GuiConfig 严格持久模型 | — | 完成 |
| [GUI-3](GUI-3.md) | manifest icon/label 与图标/名称提取链 | GUI-2 | 完成 |
| [GUI-4](GUI-4.md) | 库网格、状态角标、空库引导与 CJK 字体 | GUI-1、GUI-2 | 完成 |
| [GUI-5](GUI-5.md) | 导入向导与 Profile/external 摘要 | GUI-3、GUI-4 | 完成 |
| [GUI-6](GUI-6.md) | LaunchPlan、子进程与运行结果呈现 | GUI-4 | 完成 |
| [GUI-7](GUI-7.md) | 设置、删除确认、错误呈现与全部契约收口 | GUI-5、GUI-6 | 完成 |
| [GUI-8](GUI-8.md) | 空库按钮 ID 冲突修复与冒烟补强 | GUI-7 | 完成 |
| [GUI-9](GUI-9.md) | 库根与持久存档路径统一 | GUI-6、GUI-7 | 完成 |
| [GUI-10](GUI-10.md) | 宿主选择器期间保持导入模态 | GUI-5 | 完成 |
| [GUI-11](GUI-11.md) | 运行结果延后排队呈现 | GUI-6、GUI-7 | 完成 |

GUI-1..11 已完成，主面板基础版闭环并持续完成首次验收补强；Windows/MSVC 全量 CTest 以
`docs/state/CURRENT.md` 的滚动基线为准，后续完整 M7 体验不回填本专项。
