# 当前状态

更新：2026-08-10 · M8 Asphalt 6 JNI modified UTF-8 字符串槽批次已完成

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

- Asphalt 6 JNI class/object 与 Android framework 批次；新 exact Scenario 已越过
  modified UTF-8 字符串访问，在第 4 个 native call 停于未绑定 `FindClass`，尚未声称
  guest 首帧或主界面。

## 最近完成

- [WU-0364] 批量绑定 modified UTF-8 的 length/chars/release/region 四槽；64 KiB guest
  lease arena 支持 NUL copy、isCopy、first-fit 与严格配对。首次 exact 清理暴露并修复
  binder 晚于 string store 析构的宿主崩溃；复采样稳定停于下一类 `FindClass`；focused
  2/2、full CTest 488/488。
- [WU-0363] 一次声明 GLResLoader 5 与 GLMediaPlayer 41 个 exact descriptor；字符串资源
  复用 `/apk/assets` direct-asset VFS，编号音频走真实 `raw_NNN.ogg` loader，Java 原版
  固定/no-op 媒体语义保留逐方法计数和音量状态。exact Scenario 越过前三个 native init，
  新类别首缺口为 `GetStringUTFChars`；focused 1/1、full CTest 487/487。
- [WU-0362] 一次声明 Device 8、GLGame 18、GameRenderer 4 个 exact Java descriptor；
  通用平台 handler 提供可注入设备/版本事实、确定性离线网络，以及可查询的 unique-code、
  background、fully-loaded、键盘和 managed-swap 状态；浏览器/商店/付费/GLive/IGP/trophy
  无宿主实现时明确失败。exact Scenario 已从第 1 个 native call 推进到第 2 个
  `GLResLoader.nativeInit`，首个新类别缺口为资源读取；focused 1/1、full CTest 486/486。
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

## M6 起点

已有 Scenario v1 身份/fixture/预算/checkpoint/action/assertion/result 契约、Control
Service/JSON-RPC、固定步长 Clock、能力账本、结构化日志、GPU 查询和 Software/ANGLE
黄金帧基础。通用 runner 已闭合 Scenario→exact Profile session→action/step→assertion→
evidence/Result→shutdown，Asphalt 5 exact 标题流已连续三轮通过。实测证明标题页触摸直接
进入 Main Menu 且 `movieRequest=null`，已用真实 UI golden 更正旧推测。OBB fixture 与 MCP
GPU trace 仍明确未实现。

## 下一步（按优先级）

1. 批量闭合 JNI class/object 与 Android framework，并补齐 `nativeGetSDFolder`
   的声明式 working-directory 参数。
2. 按 GLES2 state/resource/query/draw 子批次闭合，然后固化主界面
   Scenario 与三轮 gate。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
