# DVM-58 · Threaded gate 复验与默认裁决

## 目标（一句话）

把 threaded 后端接入受检 Profile/CLI/Scenario 选择链，执行 title gate 与帧耗时采样，
并据机器证据裁决默认后端及能力状态。

## 依赖

- DVM-57
- `docs/design/dexvm/10-interpreter-threaded.md` V2-6
- `docs/playbook/SCENARIO-RUNNER.md`

## 交付

- Title Profile v2 新增 `runtime.dexvm.interpreter = "switch" | "threaded"`；省略时
  仍为 `switch`。`run-apk --dexvm-interpreter` 是受检的单次运行覆盖，Scenario
  runner 原样透传该选择，CLI 覆盖优先于 Profile。
- 结构化启动日志明确记录实际 backend 与选择来源；非法值、重复参数与 Profile
  未知枚举均明确失败。
- 默认后端保持 `switch`：A5 title 级采样未显示 threaded 的稳定收益，且 DH gate
  在两个后端上都命中同一当前启动阻断；A6 按本轮明确测试边界未执行。

## 验证与裁决

- Windows MSVC Debug 编译通过；Profile/Schema/Scenario runner 相关 4 项 CTest
  通过，最终全量 CTest 见 `CURRENT.md`。
- A5 `asphalt5.title_flow` threaded 三轮均为 468 帧 / 468000 ticks、主菜单
  `f91150b4...`、无 fault、clean shutdown。三轮总 checkpoint wall time 为
  24,484 / 24,297 / 24,858 ms；同构建 switch 对照为 24,297 ms，未形成稳定收益。
- DH threaded 首轮在 240,047 ms 到达 wall-time gate，呈现 sequence 仍为 0，
  forced cleanup；同构建 switch 对照也停在 Activity switch 后的相同
  SMS/network 边界且无进展。该证据不能证明 threaded 三轮持平，也不能把现有
  backend-neutral 阻断误报为 threaded 语义回归。
- A6 按本轮用户指定边界跳过。因三 gate 未闭合且 title 采样无收益，
  `dexvm.interpreter_threaded` 保持 `partial`，默认切换条件不成立。

状态：完成（复验与裁决完成；gate 未闭合，能力保持 partial）。
