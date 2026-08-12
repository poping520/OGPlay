# M6 · AI 自动化测试

M6 从 `WU-0328` 开始，目标是把 exact-APK 调试与兼容性验证变成 AI/CI 共用的结构化、
确定性、有界执行层。当前操作见 [`../../playbook/MCP-TESTING.md`](../../playbook/MCP-TESTING.md) 与
[`../../playbook/SCENARIO-RUNNER.md`](../../playbook/SCENARIO-RUNNER.md)，
详细范围和出口见 [`../../roadmap/10-ai-automation-testing.md`](../../roadmap/10-ai-automation-testing.md)。

自动测试闭环按以下 bounded WU 切片推进；只在实际开始时分配下一个全局编号：

| 顺序 | 切片 | 机器出口 |
| ---: | --- | --- |
| 1 | 场景与 checkpoint 纯数据契约 | 精确 Profile 身份、逻辑 fixture 和三重预算严格自检；WU-0353 |
| 2 | action/assertion/result 强类型模型 | closed enum、逐类型载荷、稳定 JSON schema 与负向测试；WU-0355 |
| 3 | Profile-backed Agent session adapter | exact APK 与 `run-apk` 复用同一 bootstrap/lifecycle/ANGLE 路径；WU-0356..0357 |
| 4 | 有界 `step` / `until` 和输入执行 | 每项都有 frame/tick/wall-time 上限，不以 sleep 判成功；WU-0356..0358 |
| 5 | 原子 checkpoint provider | 同一 frame/tick 读取 lifecycle/movie/frame/fault 状态；WU-0356..0358 |
| 6 | frame readback/golden 和结构化断言 | 原图、SHA-256、实测值和首个失败均可机器判定；WU-0358 |
| 7 | 正常 shutdown 与证据包 | 主错误和清理错误分离，guest/audio/surface/window 全部闭环；WU-0358 |
| 8 | 首个 exact-title 场景与三次确定性门禁 | 启动→输入→检查点→断言→shutdown 三次结果可复现；WU-0359 |
| 9 | 多场景批处理与趋势汇总 | 有界并行、稳定汇总和后续远端 CI 接口 |

底层 MCP 工具继续服务交互调试，但 CI 出口以场景 runner 为准；不得让 AI 通过重复截图和
自由文本推断替代 checkpoint 或断言。

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
手势队列、严格工具协议与简明使用文档。`WU-0353` 冻结 Scenario v1 的精确身份、逻辑
fixture、三重总预算与有界 checkpoint schema；`WU-0354` 插入修复这些近期 M6 改动在
macOS-arm64 Clang warnings-as-errors 下暴露的有符号转换。原计划的 action/assertion/result
强类型模型顺延到下一 WU，上述闭环依赖顺序不变。

`WU-0355..0358` 已完成强类型模型、MCP session 控制面、`run-apk` manual-step 接线和通用
Scenario runner；`WU-0359` 以 Asphalt 5 exact 标题流在 macOS-arm64 连续三轮验证固定
frame/ticks、Main Menu golden、suspend/resume、无 guest fault 与 clean shutdown。单场景
自动测试闭环已完成；顺序 9 的多场景批处理与趋势汇总仍是后续增强，不阻塞本闭环。
