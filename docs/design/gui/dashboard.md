# OGPlay 运行时 Dashboard 设计

状态：DASH-01/02/03/04 Windows 实现已落地；支持边界与验收见任务单，DASH-05 操作手册已完成。效果图见 [`dashboard.png`](dashboard.png)，静态原型见
[`dashboard.html`](dashboard.html)（纯 HTML/CSS，浏览器直接打开）。

## 1. 目标与非目标

目标：在一次 `run-apk` 运行期间，以 frame 为主时间轴，实时展示各生产模块的结构化状态，
并让相互依赖的模块之间可以联动（选中一个 guest 线程/一帧/一个能力缺口，所有相关面板同步
筛选高亮）。它服务于首错排查、停滞取证与兼容验收，读取的是与 CI/MCP 同一份结构化状态
（ADR-0006）。

非目标：不是游戏内 HUD，不替代 `diag snapshot`/procdump 的完整宿主栈，不解析自由文本日志
判断状态，不引入任何游戏特判，不成为第二套控制面。

## 2. 模块梳理（Dashboard 视角）

依赖方向自上而下；每层列出 Dashboard 需要的**事实**及其**现有来源**（`√` 已有可读接口，
`△` 有状态但缺只读快照，`✗` 需要新增记账）。

| 层 | 模块 | 职责（一句话） | Dashboard 关心的事实 | 来源 |
| --- | --- | --- | --- | --- |
| 编排 | `frontend` | CLI/GUI 入口，装配 session，提供 loopback MCP/JSON-RPC | 连接、Profile/instance/API、`--dexvm-interpreter`、supersample | √ 启动参数 |
| 编排 | `session` | Activity 生命周期、每帧泵 Looper、Clock 推进、pacer、输入分发、VFS flush | lifecycle phase/generation、frame/ticks/presented、Clock 类型与倍率、pause/flush 事件、touch capture | √ `McpSessionControl` 原子快照；△ capture/flush |
| 编排 | `agent` | 传输无关 Control Service + MCP 适配 | 命令队列深度、gesture 队列 | √ |
| VM | `runtime/dexvm` | 有界 Dalvik 解释器：链接、对象模型、GC、线程/monitor、intrinsic | 堆 used/target/growth/max、GC 次数/STW、类注册/链接计数、invoke 三路计数、Java 线程表、monitor owner/waiters、confirmed cycle、execution lock 持有者、DexVM 事件环 | √ `DiagnosticDexVmSnapshot`/monitor provider；△ 堆/GC/类计数/invoke 计数 |
| VM | `runtime/jni` + `jni_guest` | JNI/JavaVM 语义与 32 位 guest ABI 表 | slot 绑定覆盖（217/233）、local/global/weak 引用数、pending exception、在途 native 调用、未绑定槽命中 | √ native call 环；△ 引用表计数 |
| VM | `runtime/framework` / `ui` | 声明式框架 HLE；View hierarchy/layout/hit-test | UiTree generation/节点数/dirty/focus owner、overlay 合成事实 | △ |
| VM | `runtime/integration` | 组合进程会话、动态加载、出口报告 | NativeLibraryLoader registry（Loading/Loaded/Failed）、dlopen handle 表、`/proc` 事实 | △ |
| 边界 | `runtime/boundary` | Virtual SO catalog、dense thunk、fast router、FrameService | 已 seal 的 VSO 列表、每 VSO 调用计数、unbound thunk 命中、pending fault、GLES trace、GPU stats | √ `TryTrace`/`GpuStateProvider`；△ per-VSO 计数 |
| 边界 | `gles` | ANGLE 接入、EGL 生命周期、GLES 目录 | backend 名、ES 版本、draw/state 计数、texture/buffer/program 数、GL error、supersample | √ `gpu.stats/render_targets/capabilities/trace` |
| 边界 | `audio` | OpenSL/AudioTrack/MediaPlayer/SoundPool | 每 player written/consumed/queue/underrun、music 实例与编码窗口预算、SoundPool voices、mixer 状态 | √ AudioTrack 诊断快照；△ 汇总 |
| 边界 | `video` | VideoView 拉模型解码 | FFmpeg 可用性与原因、播放位置、队列水位 | √ `FfmpegAvailable`；△ 播放状态 |
| 边界 | `runtime/vfs` | 唯一路径索引、backing、FD、沙盒 | mount 表、FD 数、`IoStatistics`（backing 读/物化/预算/高水位）、lease 数、FlushAll 事件 | √ `IoStatistics`；△ mount/FD 快照 |
| 系统 | `runtime/bionic` / `syscall` / `execution` | 真实 Bionic、ARM syscall 分派、线程 runner/clone | syscall 环与分组覆盖率、ENOSYS/EINTR 计数、线程 lifecycle 状态机计数、watchdog advanced/idle、execution 登记（host_tid/guest_tid/PC） | √ `DiagnosticState` 环与 execution 表 |
| 系统 | `cpu` | A32 解释器/Dynarmic、GuestThreadGroup、FutexTable | 每线程 PC/ticks/stop reason、code cache 用量与 flush、futex 等待集与 wake 历史 | √ `FutexTable::TrySnapshot`；△ code cache/stop 统计 |
| 系统 | `memory` | 4 GiB 地址空间、权限、fault | 按权限统计映射页、fault 计数与最近 fault | △ |
| 宿主 | `hal` | window/gfx/audio/input/clock/thread 接口 | SDL 窗口、`FrameRateSampler` FPS、音频设备格式、Clock 后端 | √ |
| 横切 | `core` | Logger 环、CapabilityLedger、JSON | 结构化日志尾部、未实现命中 top-N、null-call 观测 | √ `log.tail`/`hle.*` |

结论：约 60% 事实已有只读入口（主要来自 ADR-0026 的 `DiagnosticState` 与 `agent`），
其余是给现有对象补 `TrySnapshot()`/原子计数器，不需要新记账语义。

## 3. 联动关系（模块间依赖 → 面板联动）

Dashboard 的联动只使用运行时已有的**共享键**，不新造关联：

| 共享键 | 产生方 | 消费面板 | 联动效果 |
| --- | --- | --- | --- |
| `guest_tid` / `context_token` / `host_tid` | cpu、execution、dexvm、jni_guest、syscall、monitor、futex | 线程表、Java 栈、A32 状态、native 在途调用、syscall 环、monitor 边 | 选中线程 → 全部面板按该键筛选高亮，右侧“联动焦点”纵向拼出一条 guest→host 调用链 |
| `frame` / `presented` / `steady_ns` | session、FrameService、GC、audio、ledger | 帧时间轴（主轴）、所有事件表 | 拖动帧游标 → 各事件表只显示该帧窗口；GC/GL error/underrun/miss 以刻度落在同一轴 |
| `capability id` | CapabilityLedger、DexVM unimplemented 事件、JNI/GLES 未绑定槽 | 能力账本、日志、DexVM 事件 | 点击缺口 → 定位最近触发方法与 ctx，日志过滤该 id |
| `lifecycle_phase` / `generation` | session、pacer、AudioOutputPump、UiTree | 顶栏、拓扑、GLES pacer、音频 | 阶段切换 → 时间轴 lifecycle 泳道打点，pacer/surface generation 同步刷新 |
| `monitor object` / `futex addr` | dexvm、cpu | 线程表 wait 列、monitor 表 | `BLOCKED entry 0x1c3f ← #5` 可点击跳转 owner 线程；只有 entry→owner 成环才标红（ADR-0026） |
| `node_id`（VFS）/ `fd` | vfs、syscall file、IoRuntime | VFS 表、syscall 环 | 点击 fd → syscall 环过滤该 fd 的 open/read/lseek |
| `player id` | audio、session pump | 音频表、时间轴 underrun 泳道 | underrun 打点 → 该 player 行闪烁并显示当帧队列字节 |

拓扑面板（左栏）按依赖方向绘制层级，每层灯色只由该层自身事实推导：ok / 有告警（GL error、
underrun、堆越过 target）/ 未实现命中 / 未装配（如 FFmpeg 不可用）。

## 4. 技术栈选择

### 候选

| 方案 | 说明 | 优点 | 缺点 |
| --- | --- | --- | --- |
| A. 进程内 ImGui overlay | 在 `run-apk` 窗口叠加 Dear ImGui 面板 | 无新依赖；与帧同步 | 违背 ADR-0026“不依赖 SDL 主循环存活”；与 guest GLThread 争 GL currency；主循环停滞时 Dashboard 一起死；图表/表格能力弱 |
| B. 进程外 Web Dashboard | 浏览器页面经既有 loopback JSON-RPC/MCP 读取结构化快照 | 与运行进程隔离，停滞时仍可读；与 CI/MCP 同一份状态（ADR-0006）；图表、表格、联动实现成本最低；零 C++ UI 代码 | 引入 Node 前端构建链（需 ADR）；需在 CLI 上多开一个只读路由 |
| C. `ogplay-gui` 进程内 ImGui 客户端 | 复用现有 GUI shell，经 loopback 读取同一 JSON | 无 Web 工具链 | 大量 C++ 表格/图表代码；需 vendor ImPlot；ImGui 表格联动交互成本高 |

### 决定：B（Web，进程外）+ C++ 侧薄快照层

- **服务端（C++，现有模块）**
  - `agent::ControlService` 新增只读方法组 `dash.*`（见 §5），复用 `JsonRpcAdapter`
    与 `core::JsonWriter`；不返回预序列化 JSON 字符串，遵守 `gpu.*` 既有约束。
  - 各模块补 `TrySnapshot()`/原子计数器，规则与 `DiagnosticState` 相同：只允许
    atomic、固定 ring、有界 `try_lock` 快照；busy 时 section 标 `unavailable`；
    **禁止等待 `VmExecutionLock`**、禁止持锁 I/O、禁止输出宿主指针或未受检 guest 字符串。
  - `frontend` 的 loopback 服务新增 `GET /dash/*` 静态路由，从 `data/webui/dashboard/`
    交付构建产物（生成制品，不入库，由 `webui` 目标构建，见 ADR-0072）。
    同源即可通过既有“拒绝非 loopback Origin”检查，不放宽任何 transport 约束。
- **前端（Web）**
  - Vite + TypeScript + Preact（或 React 18）：体积小、构建产物为纯静态文件。
  - uPlot：高性能时间序列/泳道（帧时间轴每帧 7 条泳道 × 600 帧仍 <1 ms）。
  - 无 UI 框架/无 CSS 框架，使用 CSS 变量深色主题（原型即此风格）。
  - 状态层：一个 `snapshot store`（最近 N 个 `dash.snapshot`）+ 一个 `selection store`
    （`gtid/frame/capability`），面板订阅两者，联动由 selection 驱动、纯前端完成。
- **拉取模式**：8 Hz 轮询 `dash.snapshot`（差量：`events` 用 `since_sequence` 游标），
  不改造 transport 为 SSE/WebSocket；停滞时轮询照常，section 显示 `unavailable`。

Node 工具链已由 [ADR-0072](../../adr/development.md#adr-0072) 接受：前端源码位于
`webui/apps/dashboard`，产物 `data/webui/dashboard/` 为不入库的生成制品，由 `run-apk`
的 `/dash/*` 静态路由交付。候选 C 不再作为回退方案。

## 5. 接口草案（`agent` 侧）

```text
dash.overview                      -> 顶栏 + 拓扑灯色 + 各 section 状态
dash.snapshot  {sections?: [...]}  -> 所有/指定模块的结构化快照（schema_version=1）
dash.events    {since_sequence, limit<=1000, kinds?: [...]}
                                   -> 统一事件流：gc | gles_error | syscall | native |
                                      dexvm | capability_miss | audio_underrun | lifecycle |
                                      vfs_flush，每条带 frame、steady_ns、gtid?、ctx?
dash.thread    {guest_tid}         -> 联动焦点：A32 状态、Java 栈、在途 native、
                                      最近 syscall、monitor/futex 边、pacer 关系
```

约束：
- 全部只读；控制动作继续走既有 `run.*`/MCP `step|lifecycle|shutdown|diag.snapshot`。
- 每个 section 携带 `{status: complete|partial|unavailable, captured_at_steady_ns,
  generation}`，与 `GuestStallSnapshot` 一致；前端把 `unavailable` 灰显而非置零。
- 事件环容量固定（默认 4096），`dropped` 计数必须返回。
- 输出字段名与 `capabilities.toml`、`quirks.toml`、日志 field 名对齐，不另造词表。

## 6. 面板布局（对应效果图）

```
┌ 顶栏：package/instance/profile · lifecycle · frame/presented/ticks · Clock · FPS · 连接 · Suspend/Step/Diag ┐
├───────────┬──────────────────────────────────────────────────────┬───────────────────────┤
│ 模块拓扑  │ 帧时间轴（主轴）：frame ms / GC / GL err / syscall /  │ 联动焦点（选中线程）  │
│ 分层灯色  │ underrun / capability miss / lifecycle                │ cpu→jni→dexvm→boundary│
│ 点击筛选  ├───────────────────────────┬──────────────────────────┤ →syscall→session 纵链 │
│           │ DexVM 堆/GC/类链接/事件   │ 线程·monitor·futex 表    ├───────────────────────┤
│           ├───────────────────────────┼──────────────────────────┤ 能力账本 未实现命中   │
│           │ GLES/EGL·GPU·trace        │ CPU·memory·syscall 环    │ top-N + 最近来源      │
├───────────┼───────────┬───────────────┼─────────────┬────────────┴───────────────────────┤
│           │ VFS·沙盒  │ Audio·Video   │ Input·UiTree│ 结构化日志尾部（过滤同步 selection）│
└───────────┴───────────┴───────────────┴─────────────┴────────────────────────────────────┘
```

## 7. 实施切分（每个 Work Unit 单次会话可完成）

1. **[DASH-01 快照层](../../tasks/gui/DASH-01.md)**：`agent` 增加 `dash.overview/snapshot/events/thread`，先只聚合已有
   来源（`DiagnosticState`、`McpSessionControl`、`GpuStateProvider`、`CapabilityLedger`、
   `Logger`、`IoStatistics`、AudioTrack 诊断、`FutexTable::TrySnapshot`）。
   验证：`tests/agent/` 方法分派/schema 闭合/`unavailable` 传播用例。
2. **[DASH-02 补快照](../../tasks/gui/DASH-02.md)**：dexvm 堆/GC/类计数/invoke 计数、JNI 引用表计数、Dynarmic code cache、
   memory 权限统计、VFS mount/FD、NativeLibraryLoader registry 各加 `TrySnapshot()`。
   验证：各模块定向测试 + “busy 时不阻塞”用例（持锁线程存在时快照 100 ms 内返回）。
3. **[DASH-03 静态路由 + 前端骨架](../../tasks/gui/DASH-03.md)**：`/dash/` 路由、Vite 工程、顶栏/拓扑/时间轴/线程表。
   验证：CTest 覆盖路由 Origin 校验；前端 `vitest` 覆盖 selection→筛选逻辑。
4. **[DASH-04 联动与其余面板](../../tasks/gui/DASH-04.md)**：§3 全部共享键联动、能力账本跳转、日志过滤同步。
5. **[DASH-05 playbook](../../tasks/gui/DASH-05.md)**：`docs/playbook/` 增加“用 Dashboard 定位首错/停滞”流程，与
   `diag snapshot` 流程衔接，见 [操作手册](../../playbook/DASHBOARD.md)。

## 8. 风险与开放问题

- 热路径计数器的开销：只允许 relaxed 原子递增和固定环写入；DexVM invoke 三路计数应放在
  已有的 tick 结算点，不加到每条指令。
- `dash.snapshot` 在 GC STW 期间大多数 section 会是 `unavailable`，这是预期行为，前端需
  明确显示，不能用上一拍数据静默补齐。
- 是否让 `tools/run_scenario.py` 复用 `dash.events` 做机器判定（例如“600 帧内 0 GL error”）：
  倾向复用，避免第二套指标口径。
