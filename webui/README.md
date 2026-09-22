# OGPlay Web UI

GUI-V2-01：Windows WebView2 宿主的最小游戏库。完整游戏库交互、导入和设置在后续工作单接入。
Linux 暂缓；macOS WebView 宿主尚未接入，本阶段仅构建 Windows GUI。

## 构建

需要 Node 24.x（`.nvmrc`）、npm、已初始化的 `third_party/webview` 子模块。
Windows 首次在仓库根运行 `./webui/prepare-sdk.ps1`，显式下载并校验固定 WebView2 SDK；
安装后的 WebView2 Runtime 由系统提供，不打包浏览器。CMake 配置期不访问网络。

在本目录运行 `npm ci`、`npm run build`；生成 `data/webui/gui/`，不入库。
`npm run check` 执行 TypeScript 和协议边界测试。
也可配置 `-DOGPLAY_BUILD_WEBUI=ON` 后构建 `webui`，GUI 目标会先构建并校验制品。
默认关闭该选项时，GUI 构建消费已有制品，缺失或哈希不符明确失败。

`cmake --build --preset windows-msvc --config Release --target ogplay-gui ogplay_tests`
之后运行 `ctest --test-dir build/windows-msvc -C Release -R "^frontend.gui_" --output-on-failure`。

## 结构

- `apps/gui/`：Preact 视图及 JSON-RPC 客户端，只呈现宿主事实。
- `packages/ui-kit/`：共享主题，Dashboard 可复用。
- `scripts/manifest.mjs`：源码提交、工作区状态、Node/npm、文件 SHA-256 清单。
- Vite 输出经典 IIFE，避免 `file://` 模块跨源限制；静态 CSP 禁止网络、子框架和表单提交。
- 宿主只允许当前本地入口导航，禁止新窗口；唯一 JS 绑定为 `rpc(string)`。

新增直接依赖限于 ADR-0072 允许的 Preact、Vite、TypeScript、Vitest。
初始锁文件由精确版本生成；依赖安装一律 `npm ci`，不使用浮动版本。

## 依赖许可

| 依赖 | 版本 | 许可 |
| --- | --- | --- |
| webview | 0.12.0 | MIT，原文见 [LICENSE](../third_party/webview/LICENSE)，源码由子模块固定提交 |
| Microsoft.Web.WebView2 SDK | 1.0.1150.38 | Microsoft WebView2 SDK 许可，原文随 NuGet 包保存在 `.local/gui-v2/webview2/LICENSE.txt` |
| Preact | 10.29.8 | MIT |
| Vite | 8.3.0 | MIT |
| TypeScript | 5.9.3 | Apache-2.0 |
| Vitest | 4.1.0 | MIT |

WebView2 是预编译 SDK/header 包，由 [prepare-sdk.ps1](prepare-sdk.ps1) 显式下载并校验
固定 SHA-256，不打包浏览器。npm 直接及传递依赖的版本、完整性哈希和许可元数据见
[package-lock.json](package-lock.json)，许可原文保留在安装后的 `node_modules` 中。
Node 仅用于构建。
