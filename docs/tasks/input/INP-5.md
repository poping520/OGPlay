# INP-5 · 运行时操控会话接口

状态：完成。依赖：INP-3。设计：[Input 设计](../../design/input/README.md) 第 10 节；
决策：[ADR-0134](../../adr/session.md#adr-0134)。

目标：run-apk 在既有 loopback 服务上提供可编辑操控配置的会话接口，供 Web 编辑器使用。

范围：
- `control.get`：合并结果、用户层（区域与绑定）、可选模板列表与内容摘要、guest surface 尺寸、
  当前是否冻结。
- `control.freeze` / `control.resume`：宿主停止/恢复 guest 推进与输入投递，不触发 Android
  生命周期；冻结时取消全部触点，恢复后需重新按键。与 MCP 手动步进、后台暂停的状态互不覆盖。
- `control.frame`：冻结时返回当前画面（复用帧快照存储）。
- `control.apply`：校验用户层（合并、区域越界、源冲突），成功后热应用到 `ControlMapper`
  并原子写回实例 `input.toml`；失败返回结构化错误且不改变运行状态。
- `ControlLayer` 序列化为 TOML，与解析往返一致。
- 写操作需要每次启动生成的令牌（由启动方传入或由 run-apk 生成并告知 GUI）；继续只绑定
  loopback 并拒绝非 loopback Origin。
- 未指定 `--input-config` 时（裸 CLI 启动）约定默认保存位置：所选沙盒实例目录下的
  `input.toml`。

不含：编辑器页面（INP-6）、GUI 入口（INP-7）。

验收：接口的成功、校验失败、令牌缺失/错误、冻结与 MCP 手动步进交互、写回往返均有测试；
真实会话中 `control.apply` 改键后无需重启即生效。

实现：core `JsonValue::Number/Keys`；input `Reconfigure`/`Layout`/`FormatInputSource`/`AnchorName`；
session 用户层 TOML/JSON 序列化与原子保存、模板列表；agent `ControlEditorChannel`；frontend
`/controls/rpc`（Bearer 令牌、loopback Host）与 `HandleControlRequest`；run-apk 接线（令牌取
`OGPLAY_CONTROL_TOKEN` 或随机生成，冻结停止推进与输入，默认键位文件
`<沙盒根>/<installation_id>.input.toml`）；GUI 总传 `--input-config`；状态行即时刷新 stdout。

验证（2026-10-13，macOS）：dev 构建 frontend/agent/input、配置与 JSON 用例 164 个通过（新增
JSON 访问、Reconfigure/Layout、序列化往返与保存、通道顺序/超时撤回/关闭、请求处理、HTTP
令牌与 Host 校验）。Release 原 APK（Dungeon Hunter，独立沙盒，`--mcp-port`）：无令牌 401；
`control.get` 返回 1024×600、5 个区域与模板；冻结后 1.5 秒内帧序号不变，恢复后继续推进；
越界布局以 -32010 拒绝且布局不变；`apply` 把 primary 半径改为 70 后实时生效并写回文件；
跑满 4000 帧正常退出。证据 `.local/input-e2e-20261013/inp5-run.log`。

缺口：`control.*` 仅在 loopback 服务（`--mcp`/`--mcp-port`）启用时可用，GUI 入口需在 INP-7
确保服务开启并经环境变量传令牌；Windows 未实跑。
