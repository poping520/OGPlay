# 当前状态

更新：2026-08-10 · M6 首个 exact-title 自动测试三轮 gate 已完成

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 的 `WU-0199..0327` 共 129 项，已冻结为 M5-A Profile/启动基础、M5-B 首个
  exact-title guest/GLES bring-up、M5-C 音频/输入/第二 title/lifecycle 三个批次；历史任务
  不移动、不重编号，正式 M5 验收尚未声明。
- M6 AI 自动化测试已从 `WU-0328` 开始；目标是让 AI 与 CI 通过同一
  Profile-backed runner 执行 exact APK 的有界场景、输入、readback、断言和证据收集。
- Windows/MSVC、Linux/x64 与 macOS/arm64 的 M4 基线均在 commit `f1b59bb` 以 ANGLE、
  warnings-as-errors 和严格全量 CTest 302/302 通过，见 [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |
| M5 去硬编码机制 | 待验收 | 尚未建立 | [三批索引](../tasks/m5/README.md) |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 进行中

- 无；单场景自动测试闭环已完成，后续可创建 WU-0360 多场景批处理与趋势汇总。

## 最近完成

- [WU-0359] 提交 Asphalt 5 exact `title_flow`：固定 frame 430 选择 English，frame 464
  点击标题页后在 468/468000 进入 Main Menu，干净 PNG SHA-256 固定为
  `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`；macOS-arm64 连续
  三轮全部 passed、无 guest fault、suspend/resume 不偷跑 frame、shutdown requested/clean，
  focused 5/5、full CTest 484/484。
- [WU-0358] Python runner 从强类型 Scenario 启动同一 Profile-backed MCP manual-step 会话，
  实时门禁 startup/checkpoint/total 三重预算，执行动作与逐帧断言，落盘相对 frame/state/log
  证据和 Result v1；成功/失败均正常 shutdown，超时清理明确记账；focused 6/6、full
  CTest 484/484。
- [WU-0357] `run-apk --mcp-manual-step` 复用 GLSurfaceView exact Profile bootstrap、ANGLE、
  lifecycle、audio 与 teardown；guest 主线程消费 step/suspend/resume/shutdown，启动、帧、
  movie/exit/fault 状态原子发布，普通交互模式不变；Asphalt 5 exact 从 0/0 精确步进到
  frame/ticks 1/1000 并正常 lifecycle/shutdown，focused 7/7、full CTest 483/483。
- [WU-0356] MCP 增加 `session_state`、`step`、`lifecycle`、`shutdown` closed-schema 工具；
  `McpSessionControl` 以原子快照和 64 项 FIFO 隔离网络 worker 与 guest 主线程，step 严格限制
  1..1,000,000 帧并返回请求/frame 范围；focused 6/6、full CTest 482/482。
- [WU-0355] Scenario v1 增加 closed-enum step/click/swipe/lifecycle/shutdown action 和
  frame/movie/lifecycle/exit/fault assertion；强类型 Python 模型与稳定 Result v1 JSON schema
  由同一机器自检约束，focused 3/3、full CTest 479/479。
- [WU-0354] 修复 GPU capability JSON、MCP Base64 测试和 sliced CPU result 在
  macOS-arm64 Clang warnings-as-errors 下的有符号转换/缺字段初始化；focused 24/24、
  full CTest 478/478。
- [WU-0353] 冻结 Scenario v1 精确 Profile 身份、逻辑 fixture、startup/total 三重预算和
  有界 checkpoint/provider/evidence 契约；严格校验器 self-test 与空的当前场景目录进入
  CTest，能力账本登记 complete。
- [WU-0352] MCP 测试文档增加 swipe 调用示例，明确 `steps` 是确定性 guest-loop motion
  数量而非毫秒，并记录端点、步数和当前手势能力边界。
- [WU-0351] MCP 发布严格 `swipe(startX,startY,endX,endY,steps)` 工具；两个端点以最近 guest
  帧校验，1..120 个 motion 阶段和响应元数据均有 schema、协议与 loopback HTTP 测试；
  Asphalt 5 exact 12 步 swipe 后 frame 500→518 且持续响应，full CTest 476/476。
- [WU-0350] MCP 输入队列泛化为 64 项手势 FIFO；click 保持 down/up，swipe 按 guest loop
  逐步输出 down、整数线性 motion 和 up，dispatcher 保真映射并与桌面鼠标互斥。
- [WU-0349] MCP 测试文档增加坐标网格调用与“带网格定位、干净截图验证”流程，并明确
  网格不改变尺寸或实时 guest 帧。
- [WU-0348] `frame_capture` 增加可选 `overlay="coordinates"`：在截图副本上绘制每 100 px
  主线、每 25 px 边缘刻度和顶部/左侧标签；默认干净截图、尺寸和 guest 帧均保持不变；
  Asphalt 5 exact 800×480 JPEG 为 93,136 字节且网格可读，full CTest 472/472。

## M6 起点

已有 Scenario v1 身份/fixture/预算/checkpoint/action/assertion/result 契约、Control
Service/JSON-RPC、固定步长 Clock、能力账本、结构化日志、GPU 查询和 Software/ANGLE
黄金帧基础。通用 runner 已闭合 Scenario→exact Profile session→action/step→assertion→
evidence/Result→shutdown，Asphalt 5 exact 标题流已连续三轮通过。实测证明标题页触摸直接
进入 Main Menu 且 `movieRequest=null`，已用真实 UI golden 更正旧推测。OBB fixture 与 MCP
GPU trace 仍明确未实现。

## 下一步（按优先级）

1. 可创建 WU-0360，为多个 Scenario 增加有界批处理、稳定汇总和趋势输出。
2. 按后续 title 需求逐项补 MCP audio/GPU/HLE/fs provider；不可用项继续明确失败。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
