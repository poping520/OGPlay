# DASH-05 · Dashboard 排查操作手册

状态：完成。依赖 [DASH-02](DASH-02.md) / [DASH-04](DASH-04.md)，依据
[Dashboard 规划](../../design/gui/dashboard.md)。Windows 优先，Linux 暂缓。

## 交付

- [DASHBOARD.md](../../playbook/DASHBOARD.md)：页面启动、实例核对、首错联动、原始 JSON 留证、
  停滞判读、MCP `diag.snapshot` 与 OS 取证入口、回归和收尾。
- 明确 `--diag` 前置条件、manual-step 正常等待、busy/partial、历史窗口/事件覆盖、
  观测增量与实际发生时间的区别，以及 GL error/CPU/音视频/UI 当前支持边界。
- 在操作手册总览、MCP、排查手册与 Web UI 文档增加入口；修正 MCP 文档缺少
  `diag.snapshot`、会话关闭描述及 Dashboard 路由说明。

## 验证

- 对照现有 CLI 参数、MCP `diag.snapshot` 分派、Dashboard schema/前端契约与能力账本核对流程。
- 修改的 Markdown 通过 UTF-8、控制字符、链接/锚点、代码块和 `git diff --check` 静态检查；
  PowerShell 示例仅做语法解析，JSON 请求做结构检查，不实际启动进程或调用接口。
- 本任务仅改文档，未构建、未跑代码测试、未新增运行期验收；既有证据和缺口沿用 DASH-02/04。

未改变模块契约或能力范围，不修改 MODULE、capabilities 或运行状态快照。
