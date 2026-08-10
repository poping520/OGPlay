# 当前状态

更新：2026-08-10 · M6 AI 自动化测试已打开

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

- 无；下一项为 WU-0350 exact-APK 场景/checkpoint schema 与严格自检。

## 最近完成

- [WU-0349] MCP 测试文档增加坐标网格调用与“带网格定位、干净截图验证”流程，并明确
  网格不改变尺寸或实时 guest 帧。
- [WU-0348] `frame_capture` 增加可选 `overlay="coordinates"`：在截图副本上绘制每 100 px
  主线、每 25 px 边缘刻度和顶部/左侧标签；默认干净截图、尺寸和 guest 帧均保持不变；
  Asphalt 5 exact 800×480 JPEG 为 93,136 字节且网格可读，full CTest 472/472。
- [WU-0347] 新增简明 OGPlay MCP 测试使用文档，覆盖启动、截图、点击、判定与当前边界，
  并从全局索引、M6 任务索引和 AI 自动化 roadmap 双向关联。
- [WU-0346] `run-apk --mcp` 固定监听 `127.0.0.1:15971/mcp`，与自定义 `--mcp-port`
  互斥；Asphalt 5 exact ping/capture sequence 425 成功，full CTest 470/470。
- [WU-0345] `frame_capture` 缺省返回 quality 85 JPEG，并以 `format=png` 返回压缩 PNG；
  Asphalt 5 800×480 exact frame 分别为 82,349 与 790,327 字节，full CTest 470/470。
- [WU-0344] 固定官方 stb commit `2c980bb` 的 `stb_image_write` v1.16，保留 MIT/Public
  Domain 文本并隔离第三方编译告警；Debug tests 与 Release `ogplay` 均已构建。
- [WU-0343] 可测试 dispatcher 在两个 guest loop 主线程逐 step 派发 MCP click down/up 并与
  桌面鼠标互斥；Release exact request 1/frame 428 后推进到 frame 473，full CTest 469/469。
- [WU-0342] loopback HTTP server 接受调用方 click queue；真实 POST 返回 request/frame
  sequence 与坐标，down/up 从同一队列取出，focused 3/3。
- [WU-0341] MCP 发布严格 `click{x,y}` schema 与 64 项线程安全队列；最近 guest frame
  边界校验、无帧/越界/队列满失败及 down/up 连续 take 均有 focused 测试。
- [WU-0340] ANGLE 缺少 PVRTC 时调用官方 decoder 转为 RGBA8 上传；MCP sequence 686 的
  800×480 帧纹理清晰，Release 300 帧 15.058 秒正常退出，
  Windows full CTest 464/464。
- [WU-0339] 原样 vendor PowerVR Native SDK 固定 commit 的 `PVRTDecompress.cpp/.h` 和 MIT
  license；OGPlay 薄 adapter 只做校验与调用，非均匀 twiddled word 黄金哈希在内 focused
  CTest 3/3 通过。
- [WU-0335] Control Service、JSON-RPC、Logger JSONL 与诊断包清除手写 JSON，统一经 core
  yyjson 严格解析/构造；focused 31/31、Windows full CTest 460/460。
- [WU-0334] MCP envelope/params/schema 迁移到严格 yyjson；语法、重复键、超限、非法 id、
  嵌套字段误取和工具参数均明确失败，真实 loopback transport 保持通过。
- [WU-0309] Dungeon Hunter 目标 ELF 74/74 GL imports 获得显式 handler；Asphalt 5 的
  62/62 目标 GL imports 也已闭合。

## M6 起点

已有 Control Service/JSON-RPC、固定步长 Clock、能力账本、结构化日志、GPU 查询和
Software/ANGLE 黄金帧基础，但 `agent-stdio` 尚未拥有 `run-apk` 的 Profile-backed exact
guest session；输入、frame capture、检查点和退出状态也未在同一自动化会话闭环。

## 下一步（按优先级）

1. 创建 WU-0350，冻结 exact-APK 场景/checkpoint schema 与严格自检。
2. 建立结构化 action/assertion/result 与证据包契约，所有动作均有 frame/tick/wall-time 上限。
3. 将当前标题页触摸→重复 logo 电影请求作为首个自动化场景，断言 movie request、
   suspend/resume、稳定检查点和正常 shutdown。

## 阻塞

- 触摸标题页后 guest 每帧重复请求同一 logo MP4；Activity suspend/resume 尚未接到已发布的
  电影请求。M6 首个端到端场景应稳定复现并机器判定该边界。
- WU-0343 的 Release exact MCP click 已排队并保持帧推进；静态 splash 无画面变化，因此
  后续场景断言仍需等待电影请求 checkpoint 接入，不把帧推进伪作 UI 已响应。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
