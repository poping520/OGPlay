# DIAG-6 · WU5 场景输入与 survey 标准流程

状态：runner/场景已实现，真实游戏回归未全部闭合。依赖：[DIAG-5](DIAG-5.md)、现有 Scenario v1、run-apk VFS/沙盒。
依据：WU4 空闲 600 帧零命中，静态 6,524 项只触达 0.08%；优先级由操作覆盖决定。

## 范围与验收

- runner 透传 `--survey-gaps`，Result 明示 survey；survey 无 passed 结果，不作兼容证据。
- OBB 复用 run-apk 标准只读挂载，不解包、不新增游戏分支。
- 九款 exact-APK 基础场景，身份来自原 APK；现有 Profile 必须精确匹配，无 Profile 不编造。
- TML 仅标题，不增加待触摸分发修复的点击；其他只固化已确认操作。
- 每款正常一次 + survey 一次；基础回归不执行 title gate 三轮。
- validator、runner/model 自测及 capabilities check；真实命中不实现，运行时不修改。
- `.local/analysis/wu5/REPORT.md` ≤60 行：逐游戏、跨游戏去重排序、前十能力分组。

## 实施记录

- OBB 已有透传实现，原手册/账本的“不支持”过时；补 launch 参数回归。
- runner 的严格 session_state 字段缺少既有 successfulPresents，按现有 MCP 契约同步。
- 内存沙盒触发 Lili `/data`、Dead Trigger/DH 路径问题；默认使用证据目录内新的磁盘沙盒，
  仍复用 run-apk；显式 --sandbox-dir 可指定状态，必须注明状态来源。
- 透传既有 surface-width/height 与 external-guest-dir，覆盖已知 DH 1024×600 与特殊挂载点、
  Tales 1920×1080；这些是宿主启动选项，不是游戏例外。
- 批量逻辑帧不能证明实际呈现推进；基础场景用 presentedFrame minimum_sequence 驱动。
- 初始契约/沙盒与预算探针不计最终结果；原始记录留 contract-probe/、authoring-probe/。

- Tales 固化 New Game → Slot 1 的两个点击；每步等待呈现帧推进并断言无 fault。
  画面内容只按截图记录，不以 checkpoint 名称替代画面验收。
- WU5 发现的运行首错/退出失败仅记录；不缩短目标路径或放宽 shutdown 判据来伪造 pass。

## 验证与剩余边界

- `ctest --test-dir build/release -R '^tools\.(scenario_runner_self_test|scenario_validator_self_test|scenarios_current|scenario_model_self_test)$' --output-on-failure`：4/4 通过。
- `python3.12 tools/validate_scenarios.py --schema data/scenarios/scenario-v1.schema.json --scenarios data/scenarios --profiles data/profiles`：9 场景通过；均少于 400 行。
- `python3.12 tools/capabilities.py check`：414 条目通过。只修改工具/数据/文档，未构建 C++。
- 正常与 survey 的每最终版各一次证据、精确宿主映射及排序见 `.local/analysis/wu5/REPORT.md`、
  `fixtures.json`、`summary.json`、逐游戏 command.json/result.json。未把编写探针混入最终结果。
- 新沙盒并未重现所有 CURRENT 到达点：Lili 为商店提示、Tales 选 Slot 1 后加载黑帧、DH 末帧黑色；
  基础帧/无 fault 断言与内容验收分开记账，没有增加 golden 或声称通过 title gate。
- TML、Dead Trigger 退出错误，Wild Blood 初始化停滞，Angry Birds 手动步进停滞：
  均不为得到 pass 修改运行时或放宽 runner 清理规则。九款全部通过这一验收仍未完成。
- 当前九款均无 Profile；TML 使用原 APK 自带资源，无独立 OBB 可映射；Dead Trigger 的原 OBB
  已复用 run-apk 标准包目录挂载。未引入另一套 OBB/存档机制。

## 最终汇总

- 九款最终版均正常一次 + survey 一次；正常 5/9 passed：Lili、Tales、Dungeon Hunter、Asphalt5、PVZ。
- TML/Dead Trigger 因退出失败，Wild Blood/Angry Birds 因超时失败；未满足全部通过，不标完成。
- 18 份 Result 通过 JSON Schema，全部 survey 均未出现 passed 状态；仅 7 份 survey JSON 可收集，
  WB/Angry Birds 强制清理前未能写出 JSON，命中数未知，不记作零。
- 跨游戏去重 1 项：Tales `Display.getRealSize(Point)`，reflection、skeleton、首次 #1、2 次命中；
  前十候选只含“显示尺寸”，不足三个主题不补凑。最终报告 38 行。
