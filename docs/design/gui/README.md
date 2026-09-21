# OGPlay 主界面 + 设置页 设计（GUI v2）

状态：Proposal（未实施）。本设计**推翻**现有 ImGui 视图层，但复用 `frontend/gui` 中已与 ImGui/SDL
解耦的模型层。效果图：[`library.png`](library.png)、[`game-settings.png`](game-settings.png)、
[`settings.png`](settings.png)；可交互原型：[`prototype.html`](prototype.html)（浏览器直接打开，
左侧导航 / 「游戏设置」按钮可切换视图）。姊妹设计：[运行时 Dashboard](../dashboard/README.md)。

## 1. 目标与边界

- 目标：一个现代、可维护的桌面启动器：游戏库管理、导入、启动、每游戏设置、全局设置、
  打开沙盒/日志目录、进入运行时 Dashboard。
- 边界（沿用 `frontend/gui/MODULE.md` 契约）：GUI 进程不运行 guest；只装配同目录
  `run-apk` 子进程；不解析自由文本日志判断兼容性；游戏身份只来自 Profile；GUI 退出不杀游戏。
- 所有"预留"设置项在 UI 上显式标记 `预留 · 运行时未实现`，落盘但不传给 `run-apk`；
  运行时能力落地后再打开对应传参，与 `capabilities.toml` 的状态对齐。

## 2. 技术栈选择

### 现状问题

ImGui 是即时模式调试 UI：无原生文本渲染栅格/字距、无 CSS 式布局、无动画与无障碍，表格与
表单交互成本高，做产品级启动器每个控件都要手写。它在本项目的唯一优势（复用 SDL3+ANGLE）
对启动器并不重要——启动器不需要 GLES。

### 候选

| 方案 | 语言/构建 | 优点 | 缺点 |
| --- | --- | --- | --- |
| A. **系统 WebView 宿主 + Web 前端**（推荐） | C++ 宿主 + Vite/TS | 复用现有 C++ 模型层与 `JsonRpcAdapter`；与 Dashboard 共用一套前端栈、主题与组件；Win 用 WebView2、macOS 用 WKWebView、Linux 用 WebKitGTK，无需打包浏览器；产物小 | 引入 Node 构建链（Dashboard 同样需要，一次 ADR 覆盖）；Linux 需 WebKitGTK 运行时依赖 |
| B. Qt 6 / QML | C++ | 成熟原生桌面框架，控件完备 | 依赖体积大、LGPL 分发约束、QML 与现有 JSON 模型二次映射；与 Dashboard 无法共用 UI |
| C. Tauri 2 | Rust + Web | 与 A 同样的 Web 前端；生态成熟 | 引入 Rust 工具链；库模型需在 Rust 重写或跨语言桥接，重复 `LibraryStore`/APK visuals 逻辑 |
| D. Electron | Node | 最省事 | 150 MB+ 运行时，与"轻量兼容层"定位相悖 |

### 决定：A

- **宿主**：C++ `ogplay-gui`，用 [webview/webview](https://github.com/webview/webview)（MIT，单头文件，
  封装 WebView2/WKWebView/WebKitGTK）创建一个窗口并 `bind()` 一个 `rpc(string)` 入口；
  消息体是 JSON-RPC 2.0，复用 `agent::JsonRpcAdapter` 与 `core::JsonWriter`。
- **模型层保留**：`LibraryStore`、`LoadGuiConfig/SaveGuiConfig`、`ExtractApkApplicationVisuals`、
  `BuildLibraryTiles/BuildLibraryDetail`、`AnalyzeApkImport/BuildLibraryImport`、`BuildLaunchPlan`、
  `GuiProcessManager`、`ValidateGuiConfigDirectories` 全部原样复用；删除 `shell.cpp`、`*_ui.cpp`、
  `ui_button.h` 与 `ogplay_imgui` 目标。
- **文件对话框**：Windows `IFileDialog`、macOS `NSOpenPanel`、Linux `xdg-desktop-portal`
  （或保留 SDL3 `SDL_ShowOpenFileDialog` 无窗口模式，避免新依赖——实施时二选一）。
- **前端**：Vite + TypeScript + Preact + 自有 CSS 变量主题（原型即此风格），与 Dashboard 共享
  `tools/webui/` 下的 `packages/ui-kit`；不引入大型组件库（构建链约束见
  [ADR-0072](../../adr/development.md#adr-0072)）。产物打进 `data/webui/gui/`，
  由宿主以 `ogplay://` 自定义 scheme 或 `file://` 加载；**不启动本地 HTTP 服务**。
- **Dashboard 入口**：游戏运行时 `run-apk` 带 `--mcp --mcp-port N`，宿主打开第二个 webview
  窗口加载 `http://127.0.0.1:N/dash/`；两者共享主题与 ui-kit 但进程/来源隔离。

## 3. 信息架构

```
ogplay-gui
├─ 游戏库（默认）          网格 / 列表；搜索、排序、筛选；右侧详情抽屉
│   ├─ 导入向导（模态）     分析 → 摘要（身份、Profile、数据包）→ 入库
│   └─ 游戏设置（全屏页）   9 个 Tab，覆盖全局默认
├─ 运行时 Dashboard        仅在有运行实例时可用，打开独立窗口
└─ 设置（全屏页）          10 个分组
```

### 3.1 游戏库

- **卡片**：没有封面素材，因此卡片"封面"= APK 图标（`ExtractApkApplicationVisuals` 128×128）
  居中 + 由图标主色生成的模糊光晕背景（前端 canvas 取平均色，无需宿主计算）。
  标题、`versionName (versionCode)`，左上状态角标（互斥，沿用现有优先级：损坏 > Profile 目录
  不可用 > 无 Profile > 缺数据包 > 运行中 > ready），右上"运行中"角标；悬停出现 ▶。
- **详情抽屉**：图标、名称、包名、版本、Profile/API/ABI 标签；主按钮 **启动**；四个次级按钮
  **游戏设置 / Dashboard / 打开沙盒目录 / 打开日志目录**；事实表（安装实例、Profile 与
  quirk 数、数据包状态与大小、沙盒路径与大小、解释器、帧率上限、虚拟设备、导入时间）；
  **最近运行**列表（时间、时长、帧数、退出码，非零可"查看日志"）；底部：预检
  `--preflight`、带诊断启动 `--diag`、删除（保留存档与数据包，二次确认）。
- **空态**：整卡虚线"拖入 APK / XAPK / APKM / APKS 或点击导入"。
- **列表视图**：同一数据的表格形态（名称、包名、版本、Profile、数据包、上次运行、操作）。

### 3.2 导入向导

1. 选择文件/目录（拖放或对话框）→ 后台只读分析（复用 `AnalyzeApkImport`，不阻塞 UI）。
2. 摘要：图标、名称、包名、版本、API/ABI；Profile 匹配结果（匹配 / 无匹配=通用 APK）；
   required external 数据包：需要 → 选择目录（可跳过并显示"缺数据包"角标）。
3. 冲突：同 package 已存在 → 新建实例 / 取消（不覆盖）。
4. 入库（原子，复用 `BuildLibraryImport`），成功后选中新卡片。

### 3.3 游戏设置（每实例覆盖）

存储：`<library-root>/library/<installation-id>/settings.toml`（schema 1，严格键集合，未知键失败，
与 `GuiConfig` 同一 TOML 私有入口）。`BuildLaunchPlan` 读取它生成 argv；"继承全局"项不写键。

| Tab | 设置项 | 现状 | 传参/落点 |
| --- | --- | --- | --- |
| 性能 | 最大帧率 30/60/120/不限 | 可实现 | Clock 步进（`RealtimeClock` 倍率/上限） |
| | 时钟模式 Realtime/FixedStep、倍率 0.5–4× | 可实现 | 现有 Clock 后端 |
| | DexVM 解释器 switch/threaded | 已有 | `--dexvm-interpreter` |
| | JIT 代码缓存 | 预留 | Dynarmic 上限需 CLI 开关 |
| 虚拟设备 | 机型预设一键带入 + 品牌/厂商、型号、设备/产品、CPU（名称/核数）、GPU（VENDOR/RENDERER）、屏幕/密度、内存、语言/地区、时区 | **预留**（运行时无 `Build.*`/`ro.*` 配置入口） | 预设为 `data/devices/*.toml`；落地时经 Profile 同构 TOML 传入 integration |
| | Android 版本 4.4.4 (19) | 只读 | 固定 API 19 |
| | ANDROID_ID 查看/重新生成 | 可实现 | 沙盒 `meta.toml` |
| 显示 | 超采样 1–4×、窗口/全屏、等比居中、强制方向、FPS 叠层 | 超采样已有；其余部分预留 | `--supersample`；其余需 CLI 开关 |
| 音频 | 音量、静音、失焦静音 | 预留 | HAL 输出增益 |
| 输入 | 输入模板、键位映射到触摸、手柄 | 模板已有；映射预留 | `InputTemplateCatalog` id |
| 数据与沙盒 | 数据包目录（required external）、打开沙盒/日志、导出/导入存档、**重置沙盒**（危险） | 目录已有；导出预留 | `--external-dir`；沙盒复制 |
| 网络 | 策略 disabled / loopback / 允许（警示） | 预留（默认 disabled） | `NetworkPolicy` |
| 兼容性 | Profile id（只读）、quirk 列表（只读）、Profile 目录覆盖、预检、带诊断启动 | 已有 | `--preflight`、`--diag*` |
| 高级 | MCP 端口、手动步进、临时沙盒、生成 argv 只读预览 | 已有 | `--mcp*`、`--ephemeral-sandbox` |

### 3.4 全局设置

| 分组 | 设置项 |
| --- | --- |
| 常规 | 语言；主题 深/浅/跟随系统；库视图密度；启动游戏后最小化；异常退出弹日志末尾；删除二次确认；"关闭启动器保持游戏运行"固定为开（契约，只展示） |
| 目录与存储 | 库根（只读+打开）；Profile 目录覆盖；默认数据包目录；FFmpeg 目录（显示探测结果/版本）；每条目保留日志数；沙盒总占用与清理入口 |
| 图形 | ANGLE 后端偏好 automatic / hardware-only / software-only（`SelectAngleBackend` 已有）；默认超采样；默认窗口模式与缩放 |
| 音频 | 输出设备；主音量；采样率/块大小（显示 HAL 事实） |
| 输入 | 默认输入模板；全局键位；手柄死区 |
| 虚拟机 | 默认解释器；默认 Clock 模式；默认虚拟设备预设 |
| 网络 | 默认网络策略（disabled）；loopback 例外说明 |
| 诊断与日志 | 日志级别；JSONL 同源输出；停滞自动快照与 teardown 预算；崩溃 dump 目录；日志目录一键打开 |
| 控制面 (MCP) | 默认启用/端口范围；Dashboard 自动打开 |
| 关于 | 版本、BootDex/guest JNI 哈希（读 manifest）、第三方许可、诊断包导出 |

## 4. 宿主 ↔ 前端 RPC（草案）

```text
library.list                → tiles + detail facts（复用 BuildLibraryTiles/BuildLibraryDetail）
library.analyze  {path}     → 导入摘要（异步，返回 job id；library.job.poll 取结果）
library.import   {job, external_dir?}
library.remove   {installation_id}
library.launch   {installation_id, overrides?}   → pid / 单实例冲突错误
library.open_dir {installation_id, kind: sandbox|log|external}
settings.get / settings.set            （全局，schema 1）
game_settings.get / game_settings.set  {installation_id}
devices.presets                        → data/devices/*.toml
runtime.instances                      → 运行中子进程、MCP 端口、退出码回收
dialog.pick {kind: file|directory, filters}
events.poll {since}                    → 子进程退出、导入完成、目录变更
```

约束：请求/响应 schema closed；未知方法与字段明确失败；文件 IO、Profile 判断、进程管理全部留在
C++ 模型层，前端只渲染事实；错误返回结构化 `{code, message, next_step}` 由前端弹窗呈现
"可执行下一步"（沿用现有契约）。

## 5. 实施切分

1. **GUI-V2-01 宿主骨架**：webview 窗口 + RPC 桥 + `library.list/launch/open_dir`；删除 ImGui
   视图与 `ogplay_imgui` 目标；保留并复用全部模型测试。验证：`tests/frontend/` 模型测试不变；
   新增 RPC 分派/schema 用例；`frontend.gui_smoke` 改为 webview 加载完成 + 一次 `library.list`。
2. **GUI-V2-02 前端游戏库**：网格/列表/抽屉/空态/拖放；图标光晕；状态角标。
3. **GUI-V2-03 导入向导 + 对话框**。
4. **GUI-V2-04 全局设置**：`GuiConfig` schema 升级（新增键，保留 `.bak` 恢复）。
5. **GUI-V2-05 游戏设置**：`settings.toml` 读写 + `BuildLaunchPlan` 消费可实现项；预留项只落盘。
6. **GUI-V2-06 Dashboard 联动**：运行实例表 → 第二窗口打开 `/dash/`。
7. **GUI-V2-07 机型预设数据**：`data/devices/*.toml` schema 与校验脚本；运行时能力另立任务。

## 6. 需要的决策 / ADR

- ~~Node 前端构建链~~ → 已接受，见 [ADR-0072](../../adr/development.md#adr-0072)：
  工作区 `tools/webui/`，产物 `data/webui/` 为不入库的生成制品，由 `webui` 目标与 CI 构建。
- Linux 的 WebKitGTK 运行时依赖是否可接受；否则 Linux 先只发 CLI。
- 文件对话框：保留 SDL3 无窗口对话框 vs 平台原生 API。
- 虚拟设备字段进入运行时的路径（Profile 同构 TOML vs 独立 `--device` 参数），属运行时能力范围，
  本设计只预留 UI 与数据格式。
