# M6 · AI 自动化测试

M6 从 `WU-0328` 开始，目标是把 exact-APK 调试与兼容性验证变成 AI/CI 共用的结构化、
确定性、有界执行层。当前操作见 [`../../OGPLAY-MCP-TESTING.md`](../../OGPLAY-MCP-TESTING.md)，
详细范围和出口见 [`../../roadmap/10-ai-automation-testing.md`](../../roadmap/10-ai-automation-testing.md)。

建议按以下依赖顺序创建 bounded WU，但只在实际开始时分配下一个全局编号：

1. 场景 schema 与严格校验器（依赖已完成的 yyjson 严格基础设施）。
2. action/assertion/result 强类型模型和 JSON schema。
3. Profile-backed Agent session adapter。
4. 有界 step/until 与输入注入。
5. frame capture/readback/golden 断言。
6. GPU/HLE/fs/lifecycle/movie/audio 检查点。
7. 首个 exact-title 端到端自动化场景。
8. 三次确定性门禁、证据包和批处理汇总。

每个 WU 仍需一句话目标、显式依赖、触及文件不超过 10 个和机器可判定验收。不得把 APK、
外部数据、凭据或测试机绝对路径提交到仓库，也不得以人工截图确认替代断言。

`WU-0331..0332` 按明确请求插入，用于清除 Windows full CTest 的 ETC1 后端差异与 VFS
非跨平台夹具；`WU-0333..0335` 接入 yyjson 并迁移 core、Control Service、JSON-RPC、MCP、
日志和诊断输出；`WU-0336..0338` 修复 exact-title 失败清理 futex 卡死并补充 tick 现场；
`WU-0339..0340` 原样接入 PowerVR 官方 PVRTC decoder，并以 MCP exact frame 闭合 ANGLE
fallback；`WU-0341..0343` 增加严格 MCP click、loopback transport 与主线程输入派发。
`WU-0344..0345` 固定官方 stb 图像编码器，并让 MCP 截图缺省返回 JPEG、按参数返回压缩
PNG；`WU-0346..0347` 增加固定 15971 端口的 `--mcp` 测试入口和简明使用文档。
`WU-0348..0349` 为 MCP 截图增加不污染 guest 帧的可选坐标网格，并记录“带网格定位、干净
截图验证”的 AI 使用方式。`WU-0350..0352` 增加按 guest step 确定性派发的有界 MCP swipe
手势队列、严格工具协议与简明使用文档。原计划的场景 schema 顺延到 `WU-0353`，上述依赖
顺序不变。
