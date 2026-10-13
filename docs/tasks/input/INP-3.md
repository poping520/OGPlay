# INP-3 · run-apk 接线与 MCP 并存

状态：完成（端到端 fixture 未做，见下）。依赖：INP-1、INP-2。设计：[Input 设计](../../design/input/README.md) 第 3、7 节。

范围：run-apk 事件循环改用 `ControlMapper`（替换 `TouchMapper` 路径），每帧在 step 前
输出；MCP click/swipe 作为外部触点进入同一分配器，取消与本地触摸的互斥；
`input_reset`、后台、Activity 切换与 surface 尺寸变化统一取消。

验收：MCP 手势与鼠标/按键合成同时按住时 guest 收到正确的多点序列；现有 MCP、
输入与 session 相关测试保持通过；DexVM fixture 端到端记录 MotionEvent。

实现：run-apk 新增 `--input-config`，启动前构建 `ControlConfig` 并按 Profile surface 校验；
窗口事件全部经 `ControlMapper`，每个 guest step 前 `Flush` 入队；`McpPointerDispatcher`
改为把 MCP 手势作为外部触点泵入，取消与本地触摸的互斥；GUI 在实例目录有 `input.toml`
时传入。运行时数据新增 `input/templates`。

验证（2026-10-13，macOS）：dev 构建 frontend/agent 127 个用例、input/session 配置与
MCP 分发用例通过，`frontend.run_apk_options` 通过。Release 原 APK（Dungeon Hunter P1000、
外部数据、独立持久沙盒、`--input-config` 覆盖 stick_action 五个区域）：
- 区域越界的覆盖文件在 preflight 前以 `zone move exceeds the 1024x600 guest surface` 失败；
- 运行 240 呈现帧正常退出 0，无 guest fault；
- MCP 手动步进中 click 经外部触点到达游戏，标题页 “Touch the screen to continue” 进入
  主菜单，`shutdown` 后正常停止（1496 呈现帧）。
证据 `.local/input-e2e-20261013/`。`--ephemeral-sandbox` 下该 APK 在约 106 帧抛出
`Invalid path: /sdcard`，去掉 `--input-config` 同样复现，属既有问题。

缺口：DexVM fixture 记录 MotionEvent 的端到端测试未做；键盘绑定只有单元测试与 session
投影测试，尚无实机按键证据（INP-4 一并验收）。
