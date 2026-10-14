# 按键编辑器 Web 前端

ADR-0134 的 Web 按键编辑器，由 run-apk 的 loopback 服务以 `/controls/` 提供，只调用同源
`/controls/rpc` 的 `control.*`（INP-5）。

- 令牌：启动方打开 `/controls/#token=<令牌>`；页面把令牌移入本标签页的 sessionStorage 并
  从地址栏与历史中移除，片段不发往服务器。无令牌或 401 时要求粘贴令牌。
- 打开即 `control.freeze` → `control.get` → `control.frame`；页面卸载（pagehide）时以
  keepalive 请求 `control.resume`；每 2 秒 `control.status {client:"editor"}` 心跳，心跳中断
  8 秒后 run-apk 自行恢复，不让游戏停在冻结状态。GUI 窗口经 sessionStorage 预置令牌。
- 坐标一律为 guest 像素：`resolveZone` 与 `ControlMapper::SetSurface` 同一锚点规则；编辑时
  区域取整像素并限制在 surface 内，锚点按两侧剩余空间比例自动选择（`chooseAnchor`）。
- 键码：`KeyboardEvent.code` 映射到 `input::ParseInputSource` 的物理键名；F1、F2 与
  Cmd/Ctrl 组合键保留给宿主，拒绝绑定；捕获状态存于 ref，按键不依赖下一次渲染。
- 冲突规则与 `session::BuildControlConfig` 相同（同模式或任一为 `*` 且共享源）；存在冲突时
  不允许应用。
- 保存：编辑对象为合并后的完整布局；`control.apply` 发送完整布局，并在前面为下层（模板、
  Profile、上次用户层）中草稿不再覆盖的源写 `action = "none"`，被删的模板键不会复活。
- “重置”回到所选模板：模板区域保留已放位置、缺失的自动放置；非区域绑定（如 Esc→Back）保留。
- 不含导入导出、社区预设与离线编辑。
