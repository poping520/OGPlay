# DIAG-9 · WU9-0 / WU9-0b Framework 契约差分

状态：首轮只降不升基线已建立（2026-10-22）。只记录偏差，不修任何运行期行为。
依赖：DIAG-8 的基线/自测门禁形态、真实 dump-surface，以及固定 API19 ROM 骨架。

## 范围与判据

以 `data/dexvm/api19-skeleton.bin` 的 own-member 形状、flags、超类和直接接口为唯一判据，
覆盖 core/framework/framework2/ext 四个固定 ROM JAR，不以游戏、Java 规范或 ART 为判据。
二进制读取统一复用 `tools/api19_skeleton.py::unpack`，格式与
`src/runtime/dexvm/skeleton_surface.cpp` 一致；不另建 ROM 解析器。

仅比较 `source=intrinsic` 的类，排除 BootDex 和 skeleton 类。
`dump-surface` 方法条目新增 `overridable` 和 `origin`，其余字段不变；origin 优先按
`is_skeleton` 标 skeleton，其次 intrinsic kind 标 handwritten，其余标 bytecode。
现有 title_preflight、select_shapes、select_rom_java、dexvm_stub_gen 按字段读取，兼容新增字段。
字段没有 origin；intrinsic 类内 ROM own fields 不存在的字段不可能是骨架补入，按手写字段检查 C。

## 类别与稳定键

每个偏差键为 `类别|类描述符|成员名|成员描述符`。同一成员同类多个原因合并为一个键；
类修饰符用 `@class`，层级用 `@superclass` / `@interfaces`，其成员描述符为空。

- **A1 可覆写性**：ROM 非 final、非 static、非 private、非 `<init>/<clinit>` 的方法，
  运行时 overridable=false 或带 final；ROM 非 final 类在运行时为 final 也计入。
- **A2 其余修饰符**：类、方法、字段的 public/protected/private/static/final/abstract
  与 ROM 不一致。A1 已计入的新增 final 位不重复计入 A2；其他位仍独立比较。
- **B 同族/构造器缺口**：同名族至少一个 handwritten 且 implemented 的方法/构造器，
  ROM 其他 public/protected 重载为 unimplemented-ledger；另外，只要类内有任一
  handwritten 且 implemented 的方法/构造器，ROM public/protected 构造器处于
  unimplemented-ledger 就计入，不要求存在手写构造器。
- **C 多余成员**：在 ROM 存在的 intrinsic 类内，手写 public/protected own member
  在 ROM own members 中不存在。ROM 没有对应类的 intrinsic 归“OGPlay 私有类”清单，
  不参与任何偏差类别，也不进入基线类清单。
- **D 层级**：超类不同或直接接口集合不同；接口顺序不计差异。
- **E 信息项**：原版为纯 Java 的 intrinsic 类清单**未测**。
  select_rom_java 的纯 Java 准入判定耦合候选集、smali 和调用闭包，没有可直接复用的独立入口。

不推断缺失 own member 的调用可达性，不将 implemented 等同于完整语义支持；
不运行游戏场景、dalvik 或 JNI 探针，不宣称游戏兼容验收。

## 入口与门禁

```sh
build/release/ogplay-cli dexvm dump-surface > .local/analysis/wu9-0/surface.json
python3 tools/conformance/framework_contract.py --self-test
ctest --test-dir build/release -L tools -R '^tools.framework_contract_self_test$' --output-on-failure
python3 tools/conformance/framework_contract.py --surface .local/analysis/wu9-0/surface.json --report .local/analysis/wu9-0/report.md --check-baseline
# 首次初始化，或偏差减少后收紧基线：
python3 tools/conformance/framework_contract.py --surface .local/analysis/wu9-0/surface.json --update-baseline
```

默认判据和基线分别为 `data/dexvm/api19-skeleton.bin`、
`tools/conformance/framework_contract_baseline.json`；可用 `--skeleton` / `--baseline` 指定。
stdout 和 `--report` 均输出每类总数、按包前 15 名、每类最多 10 个样例、私有类清单及 E 状态。

基线保存稳定键、有 ROM 判据的 intrinsic 类清单及 ROM SHA256。
`--check-baseline` 遇新键失败，偏差减少时提示 `--update-baseline`；
`--update-baseline` 只在无新键时写入，拒绝扩大基线。
ROM 哈希变化或已纳入基线的类消失也失败，防止输入替换或不完整盘点掩盖偏差。
退出码：成功 0，新偏差/类消失 1，输入错误 2。首次无基线只能通过 update 初始化。

## 首轮数字与验证

共盘点 264 个 intrinsic 类：234 个有 ROM 判据，另 30 个为 OGPlay 私有类。

| 类别 | 偏差键 |
| --- | ---: |
| A1 | 381 |
| A2 | 384 |
| B | 274 |
| C | 13 |
| D | 74 |
| 合计 | 1126 |

- 必需阳性全部命中：A1 的 `GLSurfaceView.onPause()V`、`onResume()V`；
  B 的 `java/lang/ThreadGroup.<init>(Ljava/lang/String;)V`。
- 自测覆盖 A1/A2 拆分、仅 final/仅 overridable/双类原因、B 无手写构造器规则、
  C 排除私有类、非 intrinsic 排除、输入顺序、格式拒绝和基线拒绝新键/允许减少。
  直接 self-test 与 tools label 下的新 CTest 均通过。
- WU9-0 已仅构建 ogplay-cli、ogplay_app_tests，格式化后 surface 定向测试
  1/1、349463 断言通过；WU9-0b 没有新增 C++ 改动，未重建或重跑 surface 测试。
  class_linker.cpp 改动行 clang-format dry-run 加 `-Werror` 通过。
- 基线生成后立即 `--check-baseline` 复跑通过。未修偏差；E 仍未测。

证据目录：`.local/analysis/wu9-0/`。WU9-0 原有构建/测试日志保留；
本次结果、完整逐键原因、报告、阳性、自测、CTest、基线和格式验证日志使用 `wu9-0b-` 前缀。
