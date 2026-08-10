# 当前状态

更新：2026-08-10 · M6 Profile-backed MCP 手动会话已接通

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

- 无；下一项为 WU-0358 Scenario runner 与结果/证据闭环。

## 最近完成

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
黄金帧基础。MCP 已通过 `run-apk` 接入同一 Profile-backed exact guest 会话，但尚无读取
Scenario、预算等待、结构化断言、证据写入和退出清理的 runner。

## 下一步（按优先级）

1. 创建 WU-0358，建立 Scenario runner，接入有界 step/until、输入、断言、证据和清理。
2. 提交 Asphalt 5 首个 exact 场景并执行连续三轮确定性 gate。
3. 将当前标题页触摸→重复 logo 电影请求作为首个自动化场景，断言 movie request、
   suspend/resume、稳定检查点和正常 shutdown。

## 阻塞

- 触摸标题页后 guest 每帧重复请求同一 logo MP4；Activity suspend/resume 尚未接到已发布的
  电影请求。M6 首个端到端场景应稳定复现并机器判定该边界。
- WU-0343 的 Release exact MCP click 已排队并保持帧推进；静态 splash 无画面变化，因此
  后续场景断言仍需等待电影请求 checkpoint 接入，不把帧推进伪作 UI 已响应。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
