# 当前状态

更新：2026-08-10 · M8 GLES blend/raster state 批次已完成

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 的 `WU-0199..0327` 共 129 项已冻结为三个批次；历史任务不移动、不重编号，
  正式 M5 验收尚未声明。
- M6 AI 自动化测试已从 `WU-0328` 开始；目标是让 AI 与 CI 通过同一
  Profile-backed runner 执行 exact APK 的有界场景、输入、readback、断言和证据收集。
- M8 兼容性冲刺从 `WU-0360` 开始；Asphalt 6 按静态盘点→JNI/Java→GLES2→
  线程/VFS→媒体→主界面三轮 gate 批次推进，不为单个缺失函数切 WU。
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

- Asphalt 6 exact 已越过 JNI_OnLoad、mixed GLES capability discovery、FBO 建立与剩余
  blend/raster state；96 个唯一 GL 导入已静态盘点，当前稳定进入 AudioTrack Java/JNI
  媒体类边界。尚未声称首帧或主界面。

## 最近完成

- [WU-0373] 按导入差集一次补齐 blend color/equation、sample coverage 与 flush，并覆盖
  mixed GLES1/GLES2 的共享 sample-coverage trap；flush 不触发 present。exact 越过图形
  状态批次，稳定进入 AudioTrack class lookup；focused 2/2、full CTest 494/494。
- [WU-0372] 一次接入 framebuffer/renderbuffer 生成、删除、绑定、storage、两类 attachment、
  status 共 10 项及 mipmap；名称数组、A32 栈参数和 ANGLE 错误均受检。exact 越过完整 FBO
  建立，稳定进入 `glBlendEquation`；focused 1/1、full CTest 494/494。
- [WU-0371] 为同时链接 GLES1/GLES2 的 guest 批量转发 shading-language string、8 项 shader/
  texture/uniform/varying capability 与 current-program/framebuffer/renderbuffer 三项状态；五个
  string 结果使用独立只读槽。exact 越过完整 discovery，稳定进入 `glBindFramebuffer`；
  focused 1/1、full CTest 494/494。
- [WU-0370] session 增加一次性 root JNI library 初始化，GLSurfaceView 前端只在 Profile
  class registry 装配后、startup callback 前调用；静态盘点并一次声明 SUtils、Device、
  GameInstaller 的 OnLoad lookup。exact 越过 JNI_OnLoad 与前五个 startup callback，稳定
  停在 GameRenderer nativeInit 的 GLES1 string query；full CTest 494/494。
- [WU-0369] 冻结 root-only JNI_OnLoad exported function 选择、JavaVM/null A32 调用帧与
  JNI 1.1/1.2/1.4/1.6 返回校验；不误调用 ELF dependency 同名导出，执行顺序接线由
  WU-0370 承接；focused 2/2、full CTest 494/494。
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
## M6 起点

已有 Scenario v1 身份/fixture/预算/checkpoint/action/assertion/result 契约、Control
Service/JSON-RPC、固定步长 Clock、能力账本、结构化日志、GPU 查询和 Software/ANGLE
黄金帧基础。通用 runner 已闭合 Scenario→exact Profile session→action/step→assertion→
evidence/Result→shutdown，Asphalt 5 exact 标题流已连续三轮通过。实测证明标题页触摸直接
进入 Main Menu 且 `movieRequest=null`，已用真实 UI golden 更正旧推测。OBB fixture 与 MCP
GPU trace 仍明确未实现。

## 下一步（按优先级）

1. 静态盘点 AudioTrack 及同一初始化路径引用的媒体类/方法，按完整类族批量实现。
2. 继续按 96 项 GL 导入的 shader/program、uniform/client-array/draw 子批次闭合，再处理
   license/VFS 并固化主界面 Scenario 与三轮 gate。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
