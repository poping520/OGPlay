# INP-8 · 输入和显示归用户数据

目标：删除 Profile 输入与全部类型模板，仅内置默认 + 用户 input.toml；显示尺寸及超采样
通过游戏 > 全局 > 默认解析。决定：[ADR-0142](../../adr/session.md#adr-0142)。
依赖：INP-5..7 的编辑器/RPC 与 APS-15 的 Profile v4；不新增依赖或窗口偏好。

## 改动与边界

- 删除 Profile input、模板数据/API/列表/RPC/编辑器选择与专用测试，包括旧无生产消费者的
  InputTemplateCatalog。旧 template/input_template 均严格拒绝，不迁移用户数据。
- 保留用户文件位置、原子保存、热应用与内置直通/Esc；首次布局无区域，用户自己创建。
- surface_width/surface_height 默认 800×480，每边 1..4096；supersample 1..4。
  三者同走 EffectiveGameSetting 与 GameRunOptions，GUI 字段和 CLI 校验一致。
- staging 明确移除历史 data/input，应用 payload 不再发布模板。

## 验收

- 构建：仅 ogplay_tests、ogplay_app_tests、ogplay-cli 与 ogplay_bundle（dev）。
- 12 项 control_config/编辑器 RPC/通道/Profile 定向用例通过；47 项 game settings、
  ResolveLaunch、GUI RPC 与参数往返用例通过。宽高零值/上限/非法 CLI、直接 facade
  值、游戏覆盖全局及删除覆盖后继承均有机器判据。
- TypeScript 检查、20 项 controls/game-settings/settings Vitest 通过；GUI/controls 制品
  已重建。profile/quirk 校验通过，能力账本检查通过。
- CLI options 门禁使用过时的无参数 usage 断言，改为检查 --help 的现有输出，并加入
  两个显示参数的帮助检查；全门禁通过，没有改写 CLI 的现有短 usage 行为。
- 实测原 APK Angry Birds 2.3.0：非 Profile 路径，--surface-width 1024 --surface-height 600，
  显式用户 input.toml 和独立持久沙盒。F2 打开实际 Web 编辑器，首次无区域；通过 UI
  添加 button_1、绑定 J、应用并保存。第一次呈现 255 帧，正常退出 0。
- 同参数重启，F2 编辑器直接显示已保存的 J button_1；control.get 的 user/effective
  与首次保存完全相同，surface 为 1024×600，响应不含 templates。恢复后实按 J，游戏
  到达章节选择，F1 提示显示中心 J 区域；第二次呈现 1250 帧，正常退出 0。
- 证据：`.local/user-controls-proof/{input.toml,first-run.log,second-run.log,first-config.json,second-config.json,proof.json}`。
  只有有界启动/保存重载证据，不是全 title gate。未新增官方游戏键位。

## 剩余边界

- Windows/Linux 未实跑；未做全量测试或完整游戏兼容验收。
- 旧 input.toml 的 template 字段、旧设置的 input_template 字段都显式报错；用户自行
  移除/重建，不静默忽略和迁移。实测中默认库旧 input_template 也触发了该明确错误，
  未修改用户现有库；验证使用独立用户文件和沙盒。
- 控制会话未启用时 MCP shutdown 仍明确失败；本次两次运行均通过正常关闭游戏窗口退出。
