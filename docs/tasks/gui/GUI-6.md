# GUI-6 · 启动器 Dashboard 联动

状态：Windows 主流程完成。设计：[GUI v2](../../design/gui/README.md)。
依赖：[GUI-5](GUI-5.md)、[DASH-03](DASH-03.md)。Linux 暂缓。

- 侧栏运行实例列表和游戏详情 Dashboard 入口，展示宿主记录的 PID、端口及就绪/错误原因。
- MCP 未开启或预检模式禁用入口；相同端口的第二个实例在 spawn 前明确失败。
- 后台有界探测空 section 快照，核对服务进程 PID；只打开已跟踪且就绪的实例。
- 独立 WebView 仅允许指定 loopback `/dash/`，不绑定启动器 RPC；重复打开聚焦已有窗口。
- 自动打开设置现已生效，仅启用 MCP 且服务首次就绪后执行；关闭窗口后可再次手动打开。
- 游戏退出移除运行入口并关闭对应监控窗口；关闭启动器回收监控窗口但不终止游戏。

验证（2026-09-22）：

- Windows Release `ogplay_tests`、`ogplay-gui` 构建通过；前端类型检查及 26 项测试通过。
- GUI/Dashboard/MCP HTTP 48 项定向回归通过；补充 4 项 Dashboard 回归通过（与前者有重叠）。
  覆盖身份/协议错误、真实 loopback 探测、closed RPC、启动元数据、端口冲突、退出回收，
  以及解除进程跟踪后子进程仍能继续执行。
- 7 项 GUI/Dashboard CTest 通过。新增真实 WebView 测试验证独立页面无 RPC、重复打开不新增
  页面、关闭再打开，以及 Dashboard 存在时主窗口 WM_CLOSE 能完整退出；最后一项补充后单独复跑通过。
- 空库和 CJK 非空库冒烟通过，截图检查了运行实例入口；未运行全量测试。

边界：本次用真实 HTTP/WebView 和受控子进程验证联动组件；未重新执行真实 APK 的手动点击
与自动打开全链路。真实游戏 Dashboard 数据链路沿用 DASH-03 的验证，新增来源/面板不属本任务。
