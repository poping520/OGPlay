# 按键编辑器 Web 前端

ADR-0134 的 Web 按键编辑器，由 run-apk 的 loopback 服务以 `/controls/` 提供，只调用同源
`/controls/rpc` 的 `control.*`（INP-5）。

- 令牌：启动方打开 `/controls/#token=<令牌>`；页面把令牌移入本标签页的 sessionStorage 并
  从地址栏与历史中移除，片段不发往服务器。无令牌或 401 时要求粘贴令牌。
- 打开即 `control.freeze` → `control.get` → `control.frame`。只在暂停时编辑：“试玩”先应用（有改动时）再
  `control.resume`，画面与面板只读，“返回编辑”重新冻结并取帧；“完成”应用后 `control.resume {close:true}`，
  启动器据 `close_requests` 关闭窗口，浏览器打开时停在完成页。存在冲突时两者都不可用。页面卸载（pagehide）时以
  keepalive 请求 `control.resume`；每 2 秒 `control.status {client:"editor"}` 心跳，心跳中断
  8 秒后 run-apk 自行恢复，不让游戏停在冻结状态。GUI 窗口经 sessionStorage 预置令牌。
- 坐标一律为 guest 像素：`resolveZone` 与 `ControlMapper::SetSurface` 同一锚点规则；编辑时
  区域取整像素并限制在 surface 内，锚点按两侧剩余空间比例自动选择（`chooseAnchor`）。
- 键码：`KeyboardEvent.code` 映射到 `input::ParseInputSource` 的物理键名；F1、F2 与
  Cmd/Ctrl 组合键保留给宿主，拒绝绑定；捕获状态存于 ref，按键不依赖下一次渲染。
- 冲突规则与 `session::BuildControlConfig` 相同（同模式或任一为 `*` 且共享源）；存在冲突时
  不允许应用。
- 首次打开没有官方区域；只有内置鼠标/触摸直通和 Esc→Back。用户点击添加按钮/摇杆、
  放置区域并绑定按键。新摇杆直接取第一组未被占用的方向键（WASD，其次方向键），两组都被占用才逐个捕获四键；
  每个方向可单独点击换键，四个方向不得重复。没有模板列表、选择或模板重置。
- 保存：编辑对象为内置默认与用户层合并后的布局；control.apply 发送 zone/binding，
  删除内置按键时写 action=none。保存到既有用户 input.toml，重新打开/重启加载用户文件。
  TOML/JSON 的 template 字段都明确拒绝。
- 中英文：使用 ui-kit i18n 核心，只注册 controls 英文表；收到 control.get 前（含无效令牌页面）
  按 navigator.language 的 zh 前缀选择中文，其余英文；收到后使用游戏固定的 language，
  同步 document.title 与 html lang，不跟随启动器后续切换，不改变 guest Locale。
- 不含导入导出、社区预设与离线编辑。
- 外观：先导入 ui-kit theme.css（磨砂黑令牌、`.pill`、`.seg`、`.led`），style.css 只补编辑器布局与舞台绘制；
  需要键位徽标或线性图标时用 ui-kit 的 `Key`/`Glyph`；不引入 gui/kit.tsx（其 logo 等资源不在 `/controls/` 提供的两个文件内）。不跟随玻璃紫。
