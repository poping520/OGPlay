# OGPlay MCP 测试使用

OGPlay MCP 用于在真实 `run-apk` 会话中读取最新 guest 画面并注入点击。M6 的长期自动化
目标与未完成项见 [AI 自动化测试规划](roadmap/10-ai-automation-testing.md)。

## 启动

先构建 `ogplay`，再为目标 APK 启用 MCP：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config Release --target ogplay
build\windows-msvc\Release\ogplay.exe run-apk <apk> `
  --system-dir <api19-lib-dir> --mcp
```

`--mcp` 固定使用 `http://127.0.0.1:15971/mcp`。需要其他端口时改用
`--mcp-port <1..65535>`；两者不能同时使用。省略两者时不启动 MCP。

## 测试流程

1. 等待终端输出 `OGPlay: MCP ready at ...` 和窗口首帧。
2. 调用 `frame_capture {}` 获取默认 JPEG；需要无损证据时调用
   `frame_capture {"format":"png"}`。
3. 根据截图的 guest 像素坐标调用 `click {"x":400,"y":240}`。
4. 再次截图，对比 `sequence`、画面和相关结构化状态，记录首个未满足条件。
5. 关闭窗口，确认 guest、音频、surface 和 MCP listener 正常清理。

点击坐标基于 MCP 截图尺寸，不是宿主窗口尺寸；黑边、缩放和窗口位置不参与计算。
截图失败、无首帧、坐标越界或点击队列满都会返回明确 tool error。

## 当前边界

- 已有工具：`frame_capture`、`click`。
- MCP 不负责启动或终止 APK，也不提供 drag、按键、`step/until` 或场景断言。
- 帧序号推进不等于 UI 动作成功；人工调试可检查画面，CI 必须等待后续结构化 checkpoint
  和 golden 断言，不能只依赖目测。
- 服务只绑定 `127.0.0.1`；端口占用会使启动明确失败。

当前实现状态见 [CURRENT.md](state/CURRENT.md)，后续 Work Unit 见
[M6 任务索引](tasks/m6/README.md)。
