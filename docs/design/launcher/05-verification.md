# 05 · 验证与实施

## 1. 测试策略

分三层，全部机器可判定；不做 GUI 像素 golden（字体/DPI 跨平台不稳定，
性价比为负），交互自动化留给 M7 的 GUI 行为测试批次
（`src/frontend/MODULE.md` 既有约定）。

| 层 | 覆盖 | 载体 |
| --- | --- | --- |
| 模型层单元/契约 | LibraryStore 导入/删除/损坏条目 fail closed、meta round-trip、同 package 拒绝、原子写入（临时目录 rename）、LaunchPlan argv 逐字段、GuiConfig 读写 | CTest，合成 APK fixture（沿用 `tests/loader/apk_tests.cpp` 的构造式 ZIP 手法），临时目录做库根 |
| loader 增量 | manifest application icon/label 三态（缺失/resid/字面量）、损坏失败 | CTest，扩展既有 manifest 测试 fixture |
| 提取链集成 | icon resid→arsc→PNG→缓存全链 + 每级回退占位 | CTest，合成含 arsc/PNG 的 APK fixture |
| 有界冒烟 | `ogplay gui --library-root <tmp> --smoke-frames 3` 退出码 0：真实 SDL 窗口 + ANGLE context + ImGui 渲染 + 空库视图 | CTest 集成命令，复用现有 CI 已具备的 ANGLE 窗口能力 |

启动接线的端到端（点击 → 子进程跑真实 APK）依赖 exact APK，不进仓库
测试；按 playbook 既有方式在本地以 exact fixture 验证，证据留
`.local/`。CI 层面以"argv 装配契约 + `run-apk` 自身既有受检"组合覆盖，
两段拼接处无逻辑。

## 2. 能力条目

| 条目 | 判定 |
| --- | --- |
| `frontend.gui_shell` | 冒烟命令退出 0；ImGui/SDL3/ANGLE 装配事实 |
| `frontend.gui_library` | 模型层测试全绿；损坏条目/重复导入/删除语义受检 |
| `frontend.gui_launch_spawn` | argv 契约测试；运行中状态与退出码回收受检 |
| `loader.apk_application_visuals` | manifest 增量 + 提取链测试全绿 |

状态只前进不后退；每个 WU 落地即更新对应条目。

## 3. WU 分解

| WU | 内容 | 依赖 |
| --- | --- | --- |
| GUI-1 | `third_party/imgui` submodule + `frontend/gui` shell（SDL3+ANGLE+ImGui 空窗口）+ 双击入口 `ogplay-gui`（Windows WIN32 子系统、macOS bundle）+ `gui` 子命令 + `--smoke-frames` 冒烟 + `gui.log` 落点 | — |
| GUI-2 | 模型层 LibraryStore + GuiConfig（meta/config TOML、导入复制、原子写、删除、损坏检测）+ 全部单元测试 | — |
| GUI-3 | loader manifest icon/label 增量 + 图标/名称提取链 + 缓存 | GUI-2 |
| GUI-4 | 库网格视图 + 状态角标 + 空库引导 + CJK 字体加载 | GUI-1, GUI-2 |
| GUI-5 | 导入向导（对话框、后台解析、Profile 匹配与数据包指认、摘要/确认） | GUI-3, GUI-4 |
| GUI-6 | 启动接线（LaunchPlan、SDL_CreateProcess、运行中/退出码呈现、last-run.log） | GUI-4 |
| GUI-7 | 设置页 + 删除确认 + 错误呈现收口 + 契约修订清单（[04 §5](04-integration.md)）全量核对 | GUI-5, GUI-6 |

GUI-1/GUI-2 可并行起步；每个 WU 单会话可完成、带机器可
判定测试，遵守 AGENTS.md 工作单规则。

## 4. 风险与预案

| 风险 | 预案 |
| --- | --- |
| ImGui GLES2 backend 与 ANGLE prebuilt 的装配细节（EGL config/loader 符号） | GUI-1 首日先立冒烟，装配问题在空窗口阶段暴露，不带业务复现 |
| SDL 文件对话框在无 portal 的 Linux 桌面不可用 | 对话框失败走明确错误 + 提示 `--library-root`/手册路径导入方式记入 KNOWN-ISSUES，不做自绘文件浏览器 |
| 图标为 .9.png/xml drawable/多 DPI 复杂值 | 已定回退占位（[02 §5](02-architecture.md)）；按真实命中率再评估是否扩展 |
| CJK 系统字体候选全缺失 | 已定 ASCII 回退 + 结构化警告；不阻塞可用性 |
| 库目录被外部并发修改（云盘同步等） | 与沙盒方案同则：运行中外部改动属未定义行为，仅启动时装载；损坏条目 fail closed 可删除 |
| ADR-0020 沙盒落地后的删除语义（存档是否连删） | 库布局已预留 `sandbox/` 同根；删除流程届时增加决策项，本期删除不触碰该目录 |
| 主面板退出时孤儿化的游戏子进程 | 已定策略（[04 §2](04-integration.md)）：确认后不杀进程；若实测造成困扰，M7 再议进程组管理 |
| 未签名 `OGPlay.app` 被 Gatekeeper 拦截、Linux 无 `.desktop` 条目 | 均属发行打包/签名事项，不阻塞本地构建产物双击；随发行流程立项，记入分发清单 |
