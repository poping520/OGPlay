# INP-7 · 编辑器入口与 P0.5 验收

状态：实现完成，Windows GUI 与 P0.5 实机验收待用户。依赖：INP-4、INP-6。设计：[Input 设计](../../design/input/README.md) 第 10 节；
决策：[ADR-0134](../../adr/session.md#adr-0134)。

目标：玩家从 GUI 或游戏内都能打开同一个编辑器。

范围：
- GUI 实例详情新增“按键设置”：游戏运行中时，以独立 WebView 窗口打开该实例的编辑器地址
  （同 Dashboard 窗口，核对 PID/端口，携带写令牌）；未运行时提示先启动游戏。
- 游戏内 F2：run-apk 发布“请求编辑”状态；GUI 在既有子进程轮询中读取并打开编辑器窗口；
  无 GUI（CLI 直接启动）时用系统浏览器打开同一地址。
- 编辑器窗口关闭时，若游戏仍冻结则自动恢复。

验收：GUI 入口与 F2 请求有模型层测试；Windows GUI 与 macOS 浏览器回退各实机一次；
玩家不改文件完成一款摇杆动作类游戏的配置并试玩（P0.5 验收证据记入本任务单）。

实现：
- run-apk：`--key-editor-port <port>`（与 MCP 互斥）启动只含 `/controls/` 的服务
  （`McpHttpServer::StartKeyEditor`）；无服务时首次 F2 在空闲端口懒启动。F2 在 3 秒内有启动器
  轮询（`control.status {client:"launcher"}`）时累加 `edit_requests` 交给启动器，否则以
  `hal::OpenUrlInBrowser` 打开 `/controls/#token=…`。冻结期间 8 秒无编辑器请求自动恢复；编辑器
  页面每 2 秒心跳，关闭时 keepalive 恢复。
- GUI：非预检启动生成令牌（子进程环境 `OGPLAY_CONTROL_TOKEN`），无 MCP 时选空闲端口传
  `--key-editor-port`；`GuiProcessManager` 每 500ms 探测编辑器并发现 F2；`controls.open` 与详情/
  运行实例的“按键设置”按钮；`WebViewHost::OpenKeyEditor` 只允许 `/controls/`，令牌经
  `webview_init` 写入 sessionStorage；游戏退出关闭窗口。

验证（2026-10-14，macOS）：dev 用例覆盖 `control.status`、编辑器专用服务路由、启动器探测
（真实 loopback，错令牌拒绝）、LaunchPlan 端口/令牌不进 argv、`controls.open` 就绪判定；
`frontend.run_apk_options` 新增端口校验。Release 原 APK（DH，`--key-editor-port` + 环境令牌）：
`/mcp`、`/dash/` 404，页面 200；冻结后仅启动器轮询时约 8 秒自动恢复并记 warn；浏览器打开编辑器
期间 15 秒仍保持冻结，离开页面后恢复。

缺口：Windows GUI（进程环境、WebView 窗口、F2 轮询）只写未编译、未实跑；F2 实按与浏览器
回退未在本机自动化验证（无法向游戏窗口合成按键）；P0.5“玩家不改文件完成配置并试玩”待用户实测。
