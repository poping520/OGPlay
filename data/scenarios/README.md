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
```

提交场景前运行：

```text
python tools/validate_scenarios.py --schema data/scenarios/scenario-v1.schema.json \
  --scenarios data/scenarios --profiles data/profiles
```

Scenario v1 暂不定义 action、assertion 和 result 的运行时载荷；这些强类型模型在后续 M6
Work Unit 中添加，并继续复用这里的身份、fixture、预算和 checkpoint 不变量。
