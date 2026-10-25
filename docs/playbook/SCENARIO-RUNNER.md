# 自动化测试：`tools/run_scenario.py`

把一次试玩固化成**可复跑、机器可判定**的用例。runner 用一个
`run-apk --mcp-manual-step` 会话按帧/tick/wall-time 三重预算逐个执行 checkpoint，
产出符合 `scenario-result-v1.schema.json` 的 Result v1 与证据目录。

分工：手工探索用 [MCP-TESTING.md](MCP-TESTING.md)（自由截图、点击）；固化与回归用
本篇。场景文件的字段语义与写法约束见
[`data/scenarios/README.md`](../../data/scenarios/README.md)，schema 是唯一权威。

## 前置

1. 构建 `ogplay-cli`（Windows 用 `windows-msvc` 预设，其他平台用 `dev`）。
2. 场景已通过校验：

```text
python tools/validate_scenarios.py --schema data/scenarios/scenario-v1.schema.json \
  --scenarios data/scenarios --profiles data/profiles
```

3. 场景的 `[profile]` 标识 exact APK（package + 单一 version code + `.so` SHA-256 + ABI）。
   Title Profile 可选；`--profiles` 目录里存在同包 Profile 时，身份必须精确命中它。

## 跑一次

ANGLE 动态库与 `ogplay-cli` 同目录，经 `@rpath`/可执行文件目录加载；runner 仍把工作目录设为
可执行文件所在目录，命令中的相对路径以它为准：

```text
python tools/run_scenario.py \
  --scenario data/scenarios/<id>.scenario.toml \
  --profiles data/profiles \
  --ogplay-cli build/windows-msvc/Release/ogplay-cli.exe \
  --fixture <logical-id>=<host-apk> \
  --evidence-dir .local/evidence/<id> --fresh
```

- `--fixture` 按 logical id 逐个映射宿主路径，可重复；场景里所有 `required`
  fixture 都必须映射，恰好一个 `apk`，最多一个 `external`（映射为
  `--external-dir`）和一个 `obb`（映射为 `--obb`）。无匹配 Profile 时，external
  挂到 `/sdcard`，OBB 原文件挂到 `/sdcard/Android/obb/<package>/`；Profile 可另行
  声明特殊挂载和必需资源。
- `--evidence-dir` 必须是新目录；`--fresh` 才允许覆盖已有目录。
- 默认沙盒为新证据目录下的 `sandbox/`，使用 run-apk 的磁盘沙盒初始化，不继承既有存档。
  `--sandbox-dir <dir>` 可显式复用状态，报告必须注明来源；watch 每代默认各有独立沙盒。
- `--surface-width` / `--surface-height` 与 `--external-guest-dir` 透传既有 run-apk 设置。
  场景不保存宿主路径；复跑时保留相同启动设置，并与 Result 一同记录命令。
- 音频默认走 `SDL_AUDIODRIVER=dummy`，除非环境里已设定。
- 退出码：`0` 正常通过或 survey 无执行错误（必须读 status），`1` 场景失败，`2` 参数/校验/环境无效。三种情况 stdout 都
  是单个 JSON 文档，失败时形如 `{"schema":1,"status":"invalid","reason":"…"}`，
  可直接被包装脚本消费。

## JavaTraversal 独立 staging（VD-11）

先构建当前 `ogplay-cli` 并生成配对制品，再复制为新的独立目录；脚本不启动游戏，
不会覆盖仓库 data 或原 CLI。可执行文件相邻依赖库与 data 副本一起复制，只有
`data/android/19/framework/bootdex.jar` 被实验 JAR 替换；生产 manifest 仍是原副本，
实验身份以配对 JAR 与 staging-manifest.json 为准。

```text
python3 tools/bootdex/compose_bootdex.py build --view-measure-test-output build/release/dexvm_fixtures/view-measure.jar
python3 tools/stage_java_traversal.py --ogplay-cli build/release/ogplay-cli \
  --bootdex build/release/dexvm_fixtures/view-measure.jar --output .local/staging/java-traversal
```

`staging-manifest.json` 列出全部相对路径/SHA-256、模式版本和唯一替换项，可逐项重算。
输出目录必须不存在；未知模式、封存身份不符、缺主题投影拒绝。VM owner 在建立前固定，
运行中不可切换，不自动回落 Legacy。后续运行沿用上节 runner 参数，改为
`--ogplay-cli <staging>/ogplay-cli`（Windows 为 `.exe`）；保持相同 fixture/存档源/显示/
后端并使用新证据目录。11-1 尚未完成生产遍历接线，不代表实验九场景已通过。

## 适配与回归：相同输入，两种模式

先按 MCP 已验证路径固化启动与输入 checkpoint；只加已知能走通的交互。每次点击后
至少推进两帧以派发 down/up，并在操作后的 checkpoint 断言 presented frame 推进、
no_guest_fault。批量逻辑 step 的帧数不等于 presentedFrame；等待加载/影片应给足预算，
用 frame minimum_sequence 驱动，不能凭逻辑帧数宣称到达菜单。截图留作到达点审查，
无 golden 的帧/无 fault 断言只证明基础进展，不自动证明某个画面内容。

1. 不带 survey 运行固定场景：只有 `status=passed` 且 `survey=false` 才是回归证据。
2. 同场景、相同 fixtures/显示设置，使用新的证据目录，并添加
   `--survey-gaps <absolute-or-relative-output.json>`。输出路径按调用目录解析，再透传 run-apk。
3. Result 必有 `survey`；survey 成功为 `status=survey`，成功 checkpoint 为 `observed`，
   执行/退出错误仍 `failed`。断言的 `passed` 布尔值仅表示该诊断断言求值，不能作通过证据。
   `--watch` 同样遵守此规则，最终回归需关闭 watch 与 survey。
4. 汇总 survey schema 2 的 `members_by_first_hit`：按 class/member/kind 去重，先按命中游戏数
   降序，再按各游戏最小 first_hit_index 升序，最后按成员名确定同序。不同游戏的序号仅为
   本游戏命中流中的顺序，不是统一时间线。前十项按能力主题提出候选，不在收割时实现。
5. 正常模式未通过时记首错与缺口，不用 survey 的继续执行替代通过。基础场景各一次；
   正式 title gate/golden 验收另按三轮执行。

命令示例：在“跑一次”的命令上添加
`--survey-gaps .local/analysis/<id>/survey.json`，且使用独立的 `--evidence-dir`。
无 Profile 时从原 APK 提取 package/version/.so SHA-256/ABI，并明确报告无 Profile；
有同包 Profile 时必须精确匹配，不得编造身份。

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
改 runner 或 schema 后这三项必须过。真 APK 场景不入 CI（题库不入库），基础回归各跑一次；正式 title gate/golden 才三轮复跑。

## View 遍历迁移离线比较

`python3 tools/compare_ui_scenarios.py --baseline A.json --candidate B.json --output AB.json`
比较九个Result v1；同样比较A/C。`--self-test`只生成临时合成夹具，不启动APK。
退出码0通过，1失败/needs_review，2输入或证据无效。签名改变总是needs_review，
即使阶段更晚也不自动通过；阶段提前或成功checkpoint前缀减少失败。

矩阵schema=1、normalizer_version=`vd11-reviewed-fault-v1`、cases长度9。
每case包含result/evidence的`{path,sha256}`引用（相对矩阵目录或绝对路径），以及：

- inputs：scenario_sha256、profile_sha256、quirks_sha256、fixture_sha256_by_id、
  sandbox_seed_sha256、display_settings、backend、hardware、run_options、budgets。
- implementation：binary_sha256、bootdex_sha256、member_manifest_sha256、mode、
  mode_version、ROM_hashes、font_hashes、build_id。
- fault_source：passed为null；failed为`{phase,checkpoint,log:{path,sha256}}`。
  phase是原始trace审阅后的Application/Activity/Surface/frame/shutdown；checkpoint是首个
  failed checkpoint ID（启动/退出无failed checkpoint时null），不能按耗时/帧数猜阶段。

归一化器读取原始log中的`exception: L...;`和紧随的`message:`，保留异常、精确成员及
消息（含尺寸/计数）；没有消息内成员时读取首个Java stack成员。native支持显式
`native fault: <category> module=<soname> relative_pc=0x...`证据，拒绝裸guest PC。
退出码从Result的`exited with status N`提取，timeout另列。未知格式、缺模块映射、
无精确成员或阶段证据须先审阅归一化规则，不能手写fault签名或把退出1当相同首错。
阶段注记须匹配原始`[owner] phase=...`记录、明确的生命周期错误上下文或Result的checkpoint阶段；
未知阶段拒绝。仍需人工trace审阅，哈希只证明证据未变化，不替代该审阅。

`freeze` 将已完成的 B/C 证据封存为矩阵，不启动进程：

```text
python3 tools/compare_ui_scenarios.py freeze --group B \
  --evidence-root <preserved-B-evidence> --bundle <preserved-B-bundle> --output B.json
python3 tools/compare_ui_scenarios.py freeze --group C \
  --evidence-root <C-evidence> --bundle <C-bundle> --output C.json
```

bundle 可以是已有 schema=1 矩阵文件（保留原实现身份），或可重算 CLI/库/BootDex/font
哈希的目录。目录须含 `comparison-manifest.json`：同矩阵 schema/normalizer_version/cases，
每 case 保存完整 inputs、implementation 的 ROM_hashes 及 scenarioId；也可直接复制一份
已封存输入矩阵作为 sidecar。目录实际 BootDex pairing 决定模式，B=Legacy、C=JavaTraversal；
不得拿当前 bundle 冒充旧运行制品。没有保存的历史输入身份即拒绝，不从当前环境推断。
旧矩阵无 scenarioId 时读取并核验其原 Result 哈希及 profile，再按 scenarioId 匹配新证据。
输出前重新验证九个 Result、全部证据及首错；失败保留已有输出。

无 fault 的 checkpoint wall-time 超时以 Result 的已开始 checkpoint 证明 frame 阶段，
签名含 checkpoint 和 presented 序号；后续 teardown 超时仍单列，不替换首错。
未知 fatal/native/cause 或缺少无 fault 断言仍拒绝；此分类不宣称画面通过验收。
