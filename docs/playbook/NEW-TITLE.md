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
| 首次触达缺层级类 | `class hierarchy is not available: L…;` | APK 可选类已注册，但真实路径首次链接它时缺父类/接口 | 静态预检列潜在清单；survey（§2 步 3）只收割真实命中 |
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
mkdir -p .local/analysis/preflight
build/release/ogplay-cli dexvm dump-surface > .local/analysis/preflight/surface.json
python3 tools/dexvm_api19_surface.py \
  --jar .local/aosp/core.jar --jar .local/aosp/framework.jar \
  --jar .local/aosp/framework2.jar --jar .local/aosp/ext.jar \
  --output .local/analysis/preflight/api19.json
python3 tools/title_preflight.py --apk <title>.apk \
  --surface .local/analysis/preflight/surface.json \
  --reference .local/analysis/preflight/api19.json \
  --output .local/analysis/preflight/title.json
```

运行时表面来自实际 catalog/BootDex/平台绑定，不再使用 C++ 正则推测。
`gaps` 按 java/native 与缺口类型排序：API19 存在但缺失、非 API19 探测、已声明未绑定；
ELF 导入另标 `native-import-missing`，强弱与版本分开。JNI 字符串配对带置信度，
动态 guest JNI 标记 unverified；静态依赖不代表可达，仍须步骤 3/5 确认真实命中。
题库可用 `--games-dir .local/games --output-dir .local/analysis/preflight/library` 汇总。
表面、参考及 APK 派生产物一律留 `.local/`；旧 gap report 输出可继续供步骤 2 的旧生成器使用，
新体检不生成桩，也不直接作为旧生成器输入。完整边界见 [DIAG-4](../tasks/diagnostics/DIAG-4.md)。

### 步 2 · 生成占位与中性行

```bash
python tools/dexvm_stub_gen.py --gaps .local/<title>_gap.json
```

输出是可编译的逐类 `Declare_*()` builder 骨架：void/基元返回使用
`NeutralHandler(shorty)`，String 占位使用 `PlaceholderString()`；引用返回值进入
**需要人工决策**清单。将骨架审阅后保存为
`src/runtime/integration/dexvm_android/<类名>.cpp`，并把声明加入 `catalog.h` 与
`catalog.cpp`。人工决策项不许随手桩掉——要么实现真实行为，要么保留记账失败。

若缺口属于 Luni `java.lang`，先从仓库固定的 API-19 surface 生成 class shape，避免
手抄字段和方法 descriptor：

```bash
python tools/dexvm_stub_gen.py \
  --surface data/dexvm/api19-java-lang-surface.json \
  --class Ljava/lang/Integer;
```

该模式使用当前 `IntrinsicClassBuilder` API，字段直接生成
`BoundInstanceField/BoundStaticField` handle，方法默认显式未实现，待逐项绑定真实
handler。固定清单的来源与复验命令见 `tools/README.md`。

### 步 3 · survey 运行：一次收割全部真实命中

```bash
ogplay-cli run-apk <title>.apk --profiles-dir data/profiles-dexvm \
  --exit-after-frames 120 --survey-gaps .local/<title>_survey.json
```

survey 模式与 API19 骨架共存（层级、常量、声明与正常运行一致）：被调用的未实现方法
（骨架/intrinsic/BootDex，含 JNI、反射入口）、被访问的非恒定未实现字段，以及 API19 未
声明的成员，都会返回中性值（0/null/void）并逐次记账，运行继续往下走。每条记录含来源
（skeleton/intrinsic/bootdex/missing）、首次通道（java/jni/reflection）及各通道次数、首次
命中序号/uptime/前 5 帧 guest 栈、次数。`missing_members` 按次数排序，`members_by_first_hit`
按首次命中顺序（越靠前越可能是正常运行的真实首错；之后的命中可能已被中性值带偏）：

```json
{"summary": {"missing_classes": 3, "missing_members": 41, "stub_hits": 900},
 "missing_members": [{"class": "Landroid/…;", "member": "getX()I", "source": "skeleton",
   "channel": "java", "first_hit_index": 7, "hits": 812, "first_stack": ["…"]}],
 "members_by_first_hit": [{"class": "Landroid/…;", "member": "getX()I", "first_hit_index": 7}]}
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
ogplay-cli run-apk … --exit-after-frames 600      # 不带 --survey-gaps
```

只有关闭 survey 的同路径复现才能证明越过原首错；600 帧未触发目标路径不能替代复现。
收尾按根目录 `AGENTS.md` 的改动分级执行：复用直接相关验证，只有二进制受影响才构建。
按事实变化更新对应文档，不默认修改 MODULE、能力账本及任务单。正式 title 验收另走 Scenario。

---

## 3. 失败信息判读表

| 信息 | 含义 | 处置 |
| --- | --- | --- |
| `class is not available: L…;` | 平台类未声明 | 步 1/2 批量补；确认是平台前缀而非游戏类 |
| `method cannot be resolved: …` | 类已声明、方法未声明；其后 stack 标题给出 context/thread，`fault instruction` 与 `#0` 是 APK 直接调用点 | 先按 opcode/method index、完整 method descriptor + `dex_pc` 定位；栈超过 64 帧会报告省略数。再补方法行，引用返回值先想清楚给什么对象 |
| `intrinsic handler is not implemented (id)` | 方法声明了 handler，未注册 | 在对应分面注册 |
| `virtual dispatch failed for m on L…;` | 接收者的类没有该虚方法 | 声明在**接收者**类或其父类上 |
| `invoke on null receiver: C.m() from vN called by D.f pc P` | 上游查询返回了 null | 顺着 `called by` 找那次查询（findViewById/getSystemService 之类），补的是**它** |
| `register out of range at pc N` | 预检规则或格式解码有问题 | 先怀疑我方 precheck（k22b 类 bug 有先例），用 `tools/dexdis.py` 核对 |
| `dexvm tick budget exhausted` | 预算不够，不是死循环 | 调 profile `[runtime.dexvm]`，别改解释器 |
| `DEX method_id contains an invalid index` 等解析期失败 | 还没进 dexvm，属 `loader` | 先判断是坏 dex 还是我方规则过严（已有先例：dex-format 允许数组 descriptor 作 method owner，我方只收 `L…;`），改规则要同步 `src/loader/MODULE.md` 与正/反例测试 |

---

## 4. 一次会话的节奏建议

1. 步 1：一条命令拿到潜在层级/成员缺口；不要因 APK 打包了未触达 SDK 就盲补。
2. 步 3：一次 survey 拿到运行期工作单（这一步替掉了上一款的绝大部分轮次）。
3. 步 4：按热点分 2–3 批实现，每批构建 + 复跑一次，而不是每个方法跑一次。
4. 步 5：关闭 survey 复跑，写测试与文档，提交。

经验值：Dungeon Hunter 的 61 个类 / 145 个潜在方法里，真正被执行到的是少数；
盲目补全静态清单是浪费，而不跑 survey 就只能一轮一个地发现它们。

---

## 5. 精确命令与工具

**构建与回归**（Windows Visual Studio 用 `windows-msvc`，其他平台用 `dev`）：

```bash
cmake --preset dev
cmake --build --preset dev --target <affected-target>
ctest --preset dev -R "<affected-test>"
```

**跑 title**：以下命令在构建输出目录执行，相对路径以它为准（ANGLE 动态库及 Android guest
系统库随构建产物提供，不再依赖工作目录），未通过 gate 的 v2 profile 放在 `data/profiles-dexvm/`（故意不在
`data/profiles/`，以免与生产 v1 profile 的门禁冲突）：

```bash
cd build/dev
./ogplay-cli run-apk <title>.apk \
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
4. **每批收尾**：按 `AGENTS.md` 选择最小验证和文档更新；CURRENT 仅记录当前进展与阻塞
   （≤6144 字节），MODULE 仅在契约变化时修改。不要为每个缺类或缺方法新建工作单。
   仅在用户明确要求时运行全量 `ctest`。

其他跨 title 的易误判症状见 [TROUBLESHOOTING.md](TROUBLESHOOTING.md)。
