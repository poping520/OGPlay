# 自动化场景

本目录保存 AI 与 CI 共用的纯数据 exact-APK 场景。场景文件命名为
`<scenario-id>.scenario.toml`，使用 `scenario-v1.schema.json`，且不得超过 400 行。

Scenario v1 只冻结运行身份与 checkpoint 外壳：

- `profile` 用 package、单个 version code、单个 `.so` SHA-256 和 ARM ABI 标识 exact APK。
  Title Profile 可选：同包没有 Profile 时走通用路径；同包存在 Profile 时必须精确命中其身份。
- `fixture` 只保存规范化 logical id 和 `apk` / `obb` / `external` 类型；至少有一个 required
  APK fixture。不得保存宿主路径、下载地址、凭据或受版权保护内容。
- `limits` 同时限制 startup 与整个场景的 frame、guest tick 和 wall time。
- 每个 `checkpoint` 都声明自己的三重上限、状态 provider 和证据类型；startup 与所有
  checkpoint 的最坏预算之和不得超过总预算。
- 每个 checkpoint 包含一个 closed-enum action 和至少一个逐类型 assertion；结果使用
  `scenario-result-v1.schema.json`，稳定报告首个失败、frame/tick/wall time、证据和清理状态。

示意结构：

```toml
schema = 1
id = "legacy_title.first_frame"

[profile]
package = "org.example.legacy"
version_code = 1
so_sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
abi = "armeabi-v7a"

[[fixture]]
id = "legacy_title.apk"
kind = "apk"
required = true

[limits]
startup_frames = 300
startup_ticks = 200000000
startup_wall_time_ms = 10000
total_frames = 600
total_ticks = 400000000
total_wall_time_ms = 20000

[[checkpoint]]
id = "first_frame"
provider = "frame"
max_frames = 300
max_ticks = 200000000
wall_time_ms = 10000
evidence = ["frame", "state"]

[checkpoint.action]
type = "step"
frames = 1

[[checkpoint.assertion]]
type = "frame"
minimum_sequence = 1
```

提交场景前运行：

```text
python tools/validate_scenarios.py --schema data/scenarios/scenario-v1.schema.json \
  --scenarios data/scenarios --profiles data/profiles
```

`tools/scenario_model.py` 是 AI/CI runner 共用的强类型 action/assertion/result 模型；工具
不得绕过严格 validator 直接从未验证 TOML 构造执行计划。

执行一个场景时，宿主 fixture 必须在命令行按 logical id 映射，路径不会写回 Scenario 或
Result。runner 会创建专用证据目录，按 startup/checkpoint/total 的 frame、guest tick 与
wall-time 三重预算驱动同一个 `run-apk --mcp-manual-step` 会话；任何结果都会请求正常
shutdown，超时才清理自己启动的子进程。

```text
python tools/run_scenario.py \
  --scenario data/scenarios/<id>.scenario.toml \
  --profiles data/profiles --ogplay-cli build/dev/ogplay-cli \
  --fixture <logical-id>=<host-path> \
  --evidence-dir <new-output-directory>
```

证据引用始终相对输出目录；`frame` 写无网格 PNG，`state` 写原子 session JSON，`logs` 引用
同一子进程 stdout/stderr。当前 MCP 尚未发布 GPU trace，因此请求 `gpu_trace` 证据会明确
失败，不会伪造空 trace。

## 基础场景与运行设置

九款基础场景的 exact identity 来自原 APK（所选 ARM `.so` 的解压字节 SHA-256）；当前
九款均无 Title Profile，不新增游戏 Profile。场景不等于正式 title gate，未实测通过的
场景仍须按任务报告处理缺口。[WU5](../../docs/tasks/diagnostics/DIAG-6.md) 记录实际结果。

| 场景 | 目标 / 必需设置 |
| --- | --- |
| tml.basic | 标题，暂不点击；APK 自带数据走现有装配，无额外 OBB fixture |
| lili.basic | 开场影片后的存档菜单 |
| wild_blood.basic | external fixture 映射原 Android 目录，`--external-guest-dir /sdcard/Android`；开场/菜单基础帧 |
| tales.basic | 1920×1080（`--surface-width 1920 --surface-height 1080`）；New Game → Slot 1；存档重载需另提供既有沙盒 |
| dead_trigger.basic | OBB fixture；基础启动，第一关/存档需另行固化交互 |
| dungeon_hunter.basic | external fixture 映射原 data 目录，`--external-guest-dir /sdcard/gameloft/games/DungeonHunter --surface-width 1024 --surface-height 600` |
| asphalt5.basic | 语言选择 |
| pvz.basic | TermsActivity 首帧，后续帧/交互不在已验范围 |
| angry_birds.basic | 5000 呈现帧，绘制内容仍未验收 |

默认每次在证据目录创建新磁盘沙盒，复用 run-apk 初始化/OBB 挂载；指定既有存档用
`--sandbox-dir` 并记录来源。输入覆盖与 survey 的成对运行/排序纪律见
[操作手册](../../docs/playbook/SCENARIO-RUNNER.md#适配与回归相同输入两种模式)。
survey 的 Result 标记 `survey=true`，成功状态是 `survey` / checkpoint `observed`，
失败仍 `failed`；任何 survey 结果都不能作为回归通过证据。
