# Playbook：把一款新 title 带上 dexvm 路线

写给下一位接手 AI。上一款（Dungeon Hunter）用**几十轮**「构建 → 跑 →
读第一条失败 → 补一个 intrinsic」才到标题画面。本文把那些轮次归类成根因，
并给出用现有工具**一次收割一批**的流程。目标不是少写代码，而是把
「发现下一个缺口」的成本从一轮运行降到一条命令。

前置阅读：`AGENTS.md`、`docs/state/CURRENT.md`、`docs/design/dexvm/README.md`、
`src/runtime/dexvm/MODULE.md`、`src/runtime/integration/MODULE.md`；在办的 title
阻塞点见 `docs/tasks/dexvm/README.md`。范围红线以 `AGENTS.md` 为准：survey
模式是诊断工具，不是放宽「禁止伪造成功」。

---

## 1. 上一轮的轮次都花在哪里

| 轮次类别 | 症状 | 为什么要一轮运行 | 现在怎么消掉 |
| --- | --- | --- | --- |
| 链接期缺类 | `class is not available: L…;` | 链接在第一个缺失层级类上失败 | 链接器已一次报全 + 静态预检（§2 步 1），批量补占位 |
| 常量池缺方法 | `method cannot be resolved: L…;->m()V` | 解释器在第一条未声明方法上中止 | **survey 模式**（§2 步 3）一次跑出全部命中 |
| 缺 handler | `intrinsic handler is not implemented` | 同上 | 同上 |
| 虚分派落空 | `virtual dispatch failed for m` | 同上 | 同上 |
| 空接收者连锁 | `invoke on null receiver: setVisibility` | 报错不含类名/调用点，需要反复加日志定位 | 诊断已带声明类、寄存器与调用者 pc |
| 样板量 | 占位类与中性方法骨架要手写 | 纯打字 | `tools/dexvm_stub_gen.py` 生成逐类 `Declare_*()` 骨架 |
| 误报 | 报告把已声明的类算成缺口，白补一轮 | 目录解析漏了 builder/循环/工厂式声明 | 报告读取当前 `IntrinsicClassBuilder` 目录并收集裸 descriptor 字面量；self-test 锁定发现路径 |

判断标准：**任何需要「再跑一次才知道下一个缺什么」的步骤都是要消掉的。**

---

## 2. 标准流程

### 步 1 · 静态预检（不跑游戏）

```bash
python tools/dexvm_gap_report.py --apk <title>.apk --output .local/<title>_gap.json
```

输出两层：`link_blocking`（必须先补，否则链接失败，含 `role` 字段区分
superclass/interface）与 `runtime`（**潜在**命中，是优先级清单而非必做项）。
APK 派生产物一律留在 `.local/`，不入库。

### 步 2 · 生成占位与中性行

```bash
python tools/dexvm_stub_gen.py --gaps .local/<title>_gap.json
```

输出是可编译的逐类 `Declare_*()` builder 骨架：void/基元返回使用
`NeutralHandler(shorty)`，String 占位使用 `PlaceholderString()`；引用返回值进入
**需要人工决策**清单。将骨架审阅后保存为
`src/runtime/integration/dexvm_android/<类名>.cpp`，并把声明加入 `catalog.h` 与
`catalog.cpp`。人工决策项不许随手桩掉——要么实现真实行为，要么保留记账失败。

### 步 3 · survey 运行：一次收割全部真实命中

```bash
ogplay run-apk <title>.apk --system-dir … --profiles-dir data/profiles-dexvm \
  --exit-after-frames 120 --survey-gaps .local/<title>_survey.json
```

survey 模式下，未声明的平台类/方法会被合成为**中性桩**（0/null/void）、逐次
记账，运行继续往下走。产物按命中次数排序，就是下一批的工作单：

```json
{"summary": {"missing_classes": 3, "missing_members": 41, "stub_hits": 900},
 "missing_members": [{"class": "Landroid/…;", "member": "getX()I", "hits": 812}]}
```

配合 `tools/dexvm_stub_gen.py --survey … --reached-only` 直接得到「真正被执行
到」的行。

**survey 的纪律**：默认关闭；开启时 CLI 与日志都会大声标注该次运行是诊断、
不是兼容性结论；产物 JSON 带 `"survey": true`。任何 gate、golden、Scenario
结论都不得来自 survey 运行。中性桩会掩盖行为差异——它只回答「游戏碰了什么」，
不回答「游戏需要什么值」。

### 步 4 · 按命中批次实现

热点优先。每批遵守既有约定：
- 每个平台类进入 `dexvm_android/<类名>.cpp`，用 `Declare_<类名>(context)` 同址
  声明并直接绑定 handler；聚合点只更新 `catalog.h/cpp`，禁止恢复 misc 或分面批次。
- 能诚实回答的才回答（真实状态、真实资源、真实文件）；无法诚实回答的保持
  记账 + 明确失败，不要静默返回零。
- 游戏名/包名不得进 `src/`，差异只能进 `data/profiles/`。

### 步 5 · 关闭 survey 复跑

```bash
ogplay run-apk … --exit-after-frames 600      # 不带 --survey-gaps
```

只有这次运行的结果能作为进展结论。收尾：`ctest --preset windows-msvc`
（或 `dev`）、更新 `MODULE.md` / `capabilities.toml` / `docs/state/CURRENT.md`。

---

## 3. 失败信息判读表

| 信息 | 含义 | 处置 |
| --- | --- | --- |
| `class is not available: L…;` | 平台类未声明 | 步 1/2 批量补；确认是平台前缀而非游戏类 |
| `method cannot be resolved: …` | 类已声明、方法未声明 | 补方法行；返回引用的先想清楚给什么对象 |
| `intrinsic handler is not implemented (id)` | 方法声明了 handler，未注册 | 在对应分面注册 |
| `virtual dispatch failed for m on L…;` | 接收者的类没有该虚方法 | 声明在**接收者**类或其父类上 |
| `invoke on null receiver: C.m() from vN called by D.f pc P` | 上游查询返回了 null | 顺着 `called by` 找那次查询（findViewById/getSystemService 之类），补的是**它** |
| `register out of range at pc N` | 预检规则或格式解码有问题 | 先怀疑我方 precheck（k22b 类 bug 有先例），用 `tools/dexdis.py` 核对 |
| `dexvm tick budget exhausted` | 预算不够，不是死循环 | 调 profile `[runtime.dexvm]`，别改解释器 |
| `DEX method_id contains an invalid index` 等解析期失败 | 还没进 dexvm，属 `loader` | 先判断是坏 dex 还是我方规则过严（已有先例：dex-format 允许数组 descriptor 作 method owner，我方只收 `L…;`），改规则要同步 `src/loader/MODULE.md` 与正/反例测试 |

---

## 4. 一次会话的节奏建议

1. 步 1 + 步 2：一条命令拿到全部链接期缺口，批量补齐后先让链接过。
2. 步 3：一次 survey 拿到运行期工作单（这一步替掉了上一款的绝大部分轮次）。
3. 步 4：按热点分 2–3 批实现，每批构建 + 复跑一次，而不是每个方法跑一次。
4. 步 5：关闭 survey 复跑，写测试与文档，提交。

经验值：Dungeon Hunter 的 61 个类 / 145 个潜在方法里，真正被执行到的是少数；
盲目补全静态清单是浪费，而不跑 survey 就只能一轮一个地发现它们。

---

## 5. 精确命令与工具

**构建与回归**（Windows Visual Studio 用 `windows-msvc`，其他平台用 `dev`）：

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

**跑 title**：必须在构建输出目录下执行（ANGLE 动态库按相对路径加载），
`--system-dir` 指向 Bionic 的 **lib 子目录**，未通过 gate 的 v2 profile 放在
`data/profiles-dexvm/`（故意不在 `data/profiles/`，以免与生产 v1 profile 的门禁
冲突）：

```bash
cd build/dev
./ogplay run-apk <title>.apk \
  --system-dir <api19-lib-dir> \
  --profiles-dir ../../data/profiles-dexvm \
  --external-dir <该 title 的外部数据目录> \
  --exit-after-frames 3
```

外部数据目录的层级各 title 不同（有的是包名目录，有的是其下的 `files`），
按 APK 实际读取路径给，不要猜。

**分析工具**：

| 工具 | 用途 | 注意 |
| --- | --- | --- |
| `tools/dexvm_gap_report.py` | 静态缺口分层报告（§2 步 1） | 只看平台前缀；`runtime` 段是优先级清单不是必做项 |
| `tools/dexvm_stub_gen.py` | 生成逐类 builder 骨架（§2 步 2） | 引用返回值/字段必须人工决策；输出不含 handler id |
| `tools/dex_dependency_survey.py` | 题库静态测量 | 产物留 `.local/` |
| `tools/dexdis.py` | 反汇编辅助 | pc/宽度显示有已知瑕疵，**不可作为权威**；权威事实用 C++ 侧 dump 或 `tools/dex_survey_lib.py` 直读字节 |

**固化为可复跑用例**：进入主界面后写 Scenario 并按 gate 三轮验证，见
[SCENARIO-RUNNER.md](SCENARIO-RUNNER.md)；通过后再把 profile 迁进
`data/profiles/`。

---

## 6. 上一轮踩到的纪律（AGENTS.md 之外）

1. **诚实失败要落在游戏能接住的地方**：平台侧缺失抛的应是游戏真能 catch 的
   Java 异常（`AssetManager.open` 缺条目抛 `java.io.IOException`），而不是宿主
   C++ 异常，也不是静默返回零。
2. **不要手算 hex**。上一轮大量时间浪费在人工数 code unit 上；要么 C++ dump
   二进制 + Python 结构化比对，要么用正式诊断能力。
3. **v1 冻结**：`[[java.class]]` 胶水目录不再增长（裁决 14）。不要为让新 title
   跑起来去补 v1 profile，直接走 dexvm 路线。
4. **每批收尾**：同步 `MODULE.md`、推进 `capabilities.toml`（状态只前进）、滚动
   更新 `docs/state/CURRENT.md`（≤6144 字节），提交前跑构建 + 全量 `ctest`。

其他跨 title 的易误判症状见 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)。
