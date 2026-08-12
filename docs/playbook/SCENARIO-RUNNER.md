# 自动化测试：`tools/run_scenario.py`

把一次试玩固化成**可复跑、机器可判定**的用例。runner 用一个
`run-apk --mcp-manual-step` 会话按帧/tick/wall-time 三重预算逐个执行 checkpoint，
产出符合 `scenario-result-v1.schema.json` 的 Result v1 与证据目录。

分工：手工探索用 [MCP-TESTING.md](MCP-TESTING.md)（自由截图、点击）；固化与回归用
本篇。场景文件的字段语义与写法约束见
[`data/scenarios/README.md`](../../data/scenarios/README.md)，schema 是唯一权威。

## 前置

1. 构建 `ogplay`（Windows 用 `windows-msvc` 预设，其他平台用 `dev`）。
2. 场景已通过校验：

```text
python tools/validate_scenarios.py --schema data/scenarios/scenario-v1.schema.json \
  --scenarios data/scenarios --profiles data/profiles
```

3. 场景的 `[profile]` 必须精确命中 `--profiles` 目录里的一个 Title Profile
   （package + 单一 version code + `.so` SHA-256 + ABI）。未迁移的 title profile
   放在 `data/profiles-dexvm/`，就把 `--profiles` 指向那里。

## 跑一次

必须在 `ogplay` 所在目录能加载到 ANGLE 动态库，runner 会自己把工作目录设为
可执行文件所在目录：

```text
python tools/run_scenario.py \
  --scenario data/scenarios/<id>.scenario.toml \
  --profiles data/profiles \
  --ogplay build/windows-msvc/Release/ogplay.exe \
  --system-dir <api19-lib-dir> \
  --fixture <logical-id>=<host-apk> \
  --evidence-dir .local/evidence/<id> --fresh
```

- `--fixture` 按 logical id 逐个映射宿主路径，可重复；场景里所有 `required`
  fixture 都必须映射，恰好一个 `apk`，最多一个 `external`（映射为
  `--external-dir`）。`obb` 启动尚未实现，映射它会明确失败。
- `--evidence-dir` 必须是新目录；`--fresh` 才允许覆盖已有目录。
- 音频默认走 `SDL_AUDIODRIVER=dummy`，除非环境里已设定。
- 退出码：`0` 通过，`1` 场景失败，`2` 参数/校验/环境无效。三种情况 stdout 都
  是单个 JSON 文档，失败时形如 `{"schema":1,"status":"invalid","reason":"…"}`，
  可直接被包装脚本消费。

## 读产物

```text
<evidence-dir>/
  result.json          Result v1：status、每个 checkpoint、firstFailure、shutdown
  stdout.log stderr.log  被测会话的完整输出
  <checkpoint-id>/
    frame.png          请求 frame 证据时的无网格截图
    state.json         请求 state 证据时的原子会话状态
    failure_logs.txt   失败 checkpoint 必留（stdout/stderr 末 200 行）
```

判读顺序：先看 `result.json` 的 `firstFailure`（稳定报告首个失败），再看该
checkpoint 目录里的 `failure_logs.txt` 与 `frame.png`，最后才回到完整日志。
`shutdown` 段说明会话是否正常收尾——预算用尽也会先请求正常 shutdown，超时才
清理子进程。

checkpoint 的 action 只有 `step`/`click`/`swipe`/`suspend`/`resume`/`shutdown`；
assertion 只有 `frame`（`minimum_sequence`，可选 `sha256` golden）、`movie_request`、
`lifecycle`、`process_exit`、`no_guest_fault`。没有“看起来对了”这种断言：结论
只来自 presented frame 序号、golden 摘要与结构化状态。GPU trace 证据当前未发布，
请求它会明确失败而不是伪造空 trace。

## 增量编写：`--watch`

从零写一个多 checkpoint 场景时，全量重放一次要几十秒，逐条试错代价过高。
`--watch` 保活会话：

```text
python tools/run_scenario.py --watch --scenario … --evidence-dir … （其余同上）
```

- 往场景末尾**追加** checkpoint 并保存 → 只执行新增部分（实测毫秒级）。
- 修改已执行过的 checkpoint，或任一 checkpoint 失败 → 会话重启，全部重放到新
  的 `gen<N>/` 子目录。
- 每步额外落 `<checkpoint-id>/frame_overlay.png`（带坐标网格），用来标定
  `click`/`swipe` 坐标；进度追加在 `watch_progress.jsonl`。
- 场景暂时写坏了不会中断会话，只打印校验错误并继续等待。
- Ctrl-C / SIGTERM 优雅收尾：请求正常 shutdown 并为最后一代写出合法 Result v1。
- 帧与 tick 预算照旧强制；只有**总** wall-time 预算按 checkpoint 重新计时，
  因为编写会话是开放式的。定稿后必须用不带 `--watch` 的一次完整运行取结论。

## 自检与 CI

`ctest` 中的 `tools.scenario_runner_self_test`（`run_scenario.py --self-test`）用
假进程与假 MCP 覆盖执行、证据与失败路径，不需要真 APK；
`tools.scenario_validator_self_test` 与 `tools.scenarios_current` 覆盖场景校验。
改 runner 或 schema 后这三项必须过。真 APK 场景不入 CI（题库不入库），本地按
gate 三轮的方式复跑。
