# 自动化场景

本目录保存 AI 与 CI 共用的纯数据 exact-APK 场景。场景文件命名为
`<scenario-id>.scenario.toml`，使用 `scenario-v1.schema.json`，且不得超过 400 行。

Scenario v1 只冻结运行身份与 checkpoint 外壳：

- `profile` 必须用 package、单个 version code、单个 `.so` SHA-256 和 ARM ABI 精确命中
  `data/profiles/` 中的一个 Title Profile。
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
  --profiles data/profiles --ogplay build/dev/ogplay \
  --fixture <logical-id>=<host-path> \
  --evidence-dir <new-output-directory>
```

证据引用始终相对输出目录；`frame` 写无网格 PNG，`state` 写原子 session JSON，`logs` 引用
同一子进程 stdout/stderr。当前 MCP 尚未发布 GPU trace，因此请求 `gpu_trace` 证据会明确
失败，不会伪造空 trace。
