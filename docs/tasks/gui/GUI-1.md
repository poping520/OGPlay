# GUI-1 · Windows WebView 宿主与 RPC 骨架

状态：Windows 首阶段完成。

范围：按 [GUI v2](../../design/gui/README.md) 第一阶段替换 ImGui，复用所有模型；
本轮只接 Windows，Linux 按用户要求暂缓，macOS WebView 宿主另行接入。

- 固定 webview 0.12.0 子模块；WebView2 1.0.1150.38 SDK 显式下载与 SHA-256 校验，
  CMake 不隐式联网。SDK 按预编译交付；Node 构建遵循 ADR-0072。
- `JsonRpcAdapter` 增加同步 handler 注入，GUI 不创建 guest/session；保留运行时旧构造入口。
- `library.list/launch/open_dir` 严格参数与结构化错误；库事实、LaunchPlan 和子进程跟踪复用。
- Vite/TS/Preact 最小视图，CSP 与本地导航限制；生成产物附 manifest，构建校验、随 CLI/GUI 分发。
- 移除 ImGui 视图及依赖；导入、设置、删除界面按后续阶段恢复，模型与测试保留。

验收（2026-09-22）：Windows Release `ogplay-gui` / `ogplay_tests` 构建通过；
按源码文件筛选原 GUI 模型与 control_service 回归：48 用例、343 断言通过。
前端 `npm run check` 的类型检查及 3 用例通过；4 项 GUI CTest 通过，包含制品哈希、
参数校验、空库与 CJK 非空库真实 WebView 加载/RPC/PNG 截图。截图已检查。

命令与构建前提见 [Web UI 说明](../../../webui/README.md)。
未执行全量测试、真实 APK 兼容验收或远端 CI。下一阶段 GUI-2；导入、设置和 Dashboard
尚未接入，不能将本阶段视为完整启动器体验验收。
