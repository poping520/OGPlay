# 当前状态

更新：2026-08-10 · M5 能力范围已封板，M6 AI 自动化测试待打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 的 `WU-0199..0327` 共 129 项，已冻结为 M5-A Profile/启动基础、M5-B 首个
  exact-title guest/GLES bring-up、M5-C 音频/输入/第二 title/lifecycle 三个批次；历史任务
  不移动、不重编号，正式 M5 验收尚未声明。
- 下一个开发方向为 M6 AI 自动化测试，从 `WU-0328` 开始；目标是让 AI 与 CI 通过同一
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

- 无；M6 首个 Work Unit 尚未创建。

## 最近完成

- [WU-0327] Profile guest lifecycle 新增受检 suspend/resume；挂起期间拒绝 frame/input，
  Stop 不重复 pause。电影策略与自动触发仍未声明。
- [WU-0326] legacy big-audio looping play 将 JNI boolean 同步提交到 voice state 与真实
  Ogg mixer；Release exact APK 300 帧 32.513 秒、退出码 0。
- [WU-0325] A32 observer slice 调整为 2000 万 tick，降低 Dynarmic 重入开销并保持窗口响应。
- [WU-0324] GLSurfaceView 把 A32 slice observer 接到非消费式 SDL event pump；长 guest call
  期间窗口保持响应且不提前消费输入。
- [WU-0321] guest module finalizer 在 shutdown 与 managed ANGLE surface close 之间执行。
- [WU-0319] `audio.load_movie` 发布线程安全、递增序号且可查询的电影请求，不伪造宿主播放。
- [WU-0309] Dungeon Hunter 目标 ELF 74/74 GL imports 获得显式 handler；Asphalt 5 的
  62/62 目标 GL imports 也已闭合。

## M6 起点

已有 Control Service/JSON-RPC、固定步长 Clock、能力账本、结构化日志、GPU 查询和
Software/ANGLE 黄金帧基础，但 `agent-stdio` 尚未拥有 `run-apk` 的 Profile-backed exact
guest session；输入、frame capture、检查点和退出状态也未在同一自动化会话闭环。

## 下一步（按优先级）

1. 创建 WU-0328：冻结 exact-APK 场景/checkpoint 纯数据 schema 与严格自检。
2. 建立结构化 action/assertion/result 与证据包契约，所有动作均有 frame/tick/wall-time 上限。
3. 把现有 Profile guest session 接入 Agent Control，再实现有界 step/until、输入和 readback。
4. 将当前标题页触摸→重复 logo 电影请求作为首个自动化场景，断言 movie request、
   suspend/resume、稳定检查点和正常 shutdown。

## 阻塞

- 触摸标题页后 guest 每帧重复请求同一 logo MP4；Activity suspend/resume 尚未接到已发布的
  电影请求。M6 首个端到端场景应稳定复现并机器判定该边界。
- 最新 M5 WU 记录的 Windows full CTest 为 441/443；既有 ETC1 参数和 VFS 大小写目录树
  两项失败仍需在 M5 正式验收前复核，不能宣称全量测试全绿。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
