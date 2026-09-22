# OGPlay Web UI

GUI-1..5：Windows WebView2 游戏库、APK 导入、全局及每实例设置。
DASH-03：浏览器 Dashboard 顶栏、拓扑、帧采样、线程/事件与联动焦点，复用 run-apk 服务。
支持原生文件/数据包目录选择及 APK 拖放，后台分析、确认新实例、原子入库；分包格式明确拒绝。
Linux 暂缓；macOS WebView 宿主尚未接入，本阶段仅构建 Windows GUI。

## 构建

需要 Node 24.x（`.nvmrc`）、npm、已初始化的 `third_party/webview` 子模块。
Windows 首次在仓库根运行 `./webui/prepare-sdk.ps1`，显式下载并校验固定 WebView2 SDK；
安装后的 WebView2 Runtime 由系统提供，不打包浏览器。CMake 配置期不访问网络。

在本目录运行 `npm ci`、`npm run build`；生成 `data/webui/gui/` 和 `data/webui/dashboard/`，不入库。
`npm run check` 执行 TypeScript、协议、库交互及分块上传测试。
也可配置 `-DOGPLAY_BUILD_WEBUI=ON` 后构建 `webui`，GUI 目标会先构建并校验制品。
默认关闭该选项时，GUI 构建消费已有制品，缺失或哈希不符明确失败。

`cmake --build --preset windows-msvc --config Release --target ogplay-gui ogplay_tests`
之后运行 `ctest --test-dir build/windows-msvc -C Release -R "^frontend.gui_" --output-on-failure`。

## Dashboard

构建后运行 `ogplay run-apk <apk> --mcp`，在浏览器打开终端输出的
`http://127.0.0.1:15971/dash/`。自定义端口沿用 `--mcp-port`。
页面只读；控制继续使用既有 MCP。无需启动 Node 服务或额外进程。

`ctest --test-dir build/windows-msvc -C Release -R "^frontend.dashboard_" --output-on-failure`
验证静态制品和 HTTP 路由。运行时来源和共享键面板已接通，支持范围与
首错/停滞取证流程见 [Dashboard 操作手册](../docs/playbook/DASHBOARD.md)。

## 结构

- `apps/dashboard/`：HTTP JSON-RPC 客户端、限量 snapshot/selection store 和 uPlot 帧采样图。
- `apps/gui/`：Preact 视图及 JSON-RPC 客户端，只呈现宿主事实。
- `packages/ui-kit/`：共享主题，Dashboard 可复用。
- `scripts/manifest.mjs`：源码提交、工作区状态、Node/npm、文件 SHA-256 清单。
- Vite 输出经典 IIFE，避免 `file://` 模块跨源限制；静态 CSP 禁止网络、子框架和表单提交。
- 宿主只允许当前本地入口导航，禁止新窗口；唯一 JS 绑定为 `rpc(string)`。

新增直接依赖限于 ADR-0072 允许的 Preact、uPlot、Vite、TypeScript、Vitest。
初始锁文件由精确版本生成；依赖安装一律 `npm ci`，不使用浮动版本。

## 依赖许可

| 依赖 | 版本 | 许可 |
| --- | --- | --- |
| webview | 0.12.0 | MIT，原文见 [LICENSE](../third_party/webview/LICENSE)，源码由子模块固定提交 |
| Microsoft.Web.WebView2 SDK | 1.0.1150.38 | Microsoft WebView2 SDK 许可，原文随 NuGet 包保存在 `.local/gui-v2/webview2/LICENSE.txt` |
| Preact | 10.29.8 | MIT |
| uPlot | 1.6.32 | MIT |
| Vite | 8.3.0 | MIT |
| TypeScript | 5.9.3 | Apache-2.0 |
| Vitest | 4.1.0 | MIT |

WebView2 是预编译 SDK/header 包，由 [prepare-sdk.ps1](prepare-sdk.ps1) 显式下载并校验
固定 SHA-256，不打包浏览器。npm 直接及传递依赖的版本、完整性哈希和许可元数据见
[package-lock.json](package-lock.json)，许可原文保留在安装后的 `node_modules` 中。
Node 仅用于构建。
