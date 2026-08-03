# 02 · AI 主导开发的工程规范

本篇解决一个问题：**如何让一个几乎全部由 AI 编写的大型 C++ 项目，在几百个会话之后
依然保持架构完整、不重复劳动、不悄悄退化。**

---

## 1. 先承认 AI 开发的三个失效模式

| 失效模式 | 表现 | 根本原因 |
| --- | --- | --- |
| **上下文截断** | 改 A 模块时不知道 B 模块的约定，写出重复或冲突的实现 | 窗口装不下整个项目 |
| **跨会话失忆** | 三个月后推翻自己当初的架构决定；同一个坑踩两次 | 决策没有落盘，只存在于聊天记录里 |
| **自验证缺失** | "我觉得改好了" → 实际引入回归；或者反复修一个其实没坏的地方 | 没有机器可判定的正确性标准 |

对策总纲三句话：

> **让仓库自己携带上下文。让正确性可机器判定。让任务小到一次会话能做完。**

---

## 2. 仓库结构即上下文

### 2.1 模块契约 `MODULE.md`

每个模块目录下放一份契约，AI 修改模块 X 时**只需读 X 的源码 + 相邻模块的
`MODULE.md`**，不需要读邻居的实现。这是控制上下文规模最有效的手段。

```markdown
# 模块：cpu

## 职责
翻译并执行 guest ARM 代码。只负责取指、执行、异常上报。

## 公共 API
- `Cpu::Run(ticks) -> RunResult`
- `Cpu::Regs()`
- 内存访问通过 `MemoryBus` 接口回调，不直接持有 GuestMemory

## 不变量
- 任何内存访问失败必须产生 Fault，不得静默返回 0
- 不得包含任何游戏相关分支

## 禁止
- 不得直接调用 HLE 层
- 不得写日志到 stdout（用 Tracer 接口）

## 测试
`tests/cpu/` · `ctest -R cpu_`
```

**规则**：`MODULE.md` 与代码不一致时，以 `MODULE.md` 为准，修代码。改契约必须单独提交。

### 2.2 顶层 `AGENTS.md`

工作流的唯一入口。规定：接任务前必读什么、提交前必做什么、什么绝对不能做。

### 2.3 知识库分区

```
docs/
  roadmap/      长期规划（本目录，稳定，很少改）
  adr/          架构决策记录（只追加，不修改）
  modules/      各模块契约的汇总索引
  playbook/     排查经验（按症状索引，持续追加）
  state/        当前进度与交接（每个会话更新）
  tasks/        任务单
  INDEX.md      自动生成的全局索引，供 AI 按关键词检索
```

---

## 3. 架构决策记录（ADR）

`docs/adr/NNNN-短标题.md`，**只追加不修改**。要推翻旧决定，写一份新 ADR 并标注
`Supersedes: ADR-0007`。

模板：

```markdown
# ADR-0012 · 使用真实 Bionic 而非重实现 libc

- 状态：Accepted
- 日期：2026-08-02
- 相关：ADR-0003

## 背景
（为什么现在要做这个决定）

## 选项
1. 重实现 libc（DEMO 现状）
2. 加载 AOSP 预编译 Bionic，只 HLE syscall
3. 混合

## 决定
选 3。默认走真实 Bionic，对 memcpy/pthread/mmap 等做选择性拦截。

## 后果
- 好：ABI 保真度按 Android 版本天然正确；HLE 面从 ~2000 个函数缩到 ~150 个 syscall
- 坏：需要正确的 futex/clone/TLS；调试栈更深
- 需要：syscall 层必须先于此完成
```

**价值**：AI 接手时读 ADR 目录就能知道"哪些路已经走过、为什么不走"，避免反复论证。

---

## 4. 会话交接协议

`docs/state/CURRENT.md` 是**每个会话的起点和终点**。

```markdown
# 当前状态

更新：2026-08-02 · 会话 #147

## 进行中
- [WU-0231] JNI 反射调用族（GetMethodID/CallXxxMethod）
  - 已完成：GetMethodID、CallVoidMethodV
  - 未完成：CallXxxMethodA 变体、异常传播
  - 卡点：无

## 最近完成
- [WU-0230] syscall futex FUTEX_WAIT/WAKE

## 下一步（按优先级）
1. WU-0232 JNI 异常传播
2. WU-0233 局部/全局引用表

## 已知问题
- Asphalt 6 在窗口模式下第 2 次 resume 会丢一帧（未定位，非阻塞）
```

用 Cursor Hook 在会话结束时强制检查该文件是否被更新。

> 这个模式在 DEMO 阶段已被验证有效：当时用一份手写的进度记录跨多次会话保住了上下文，
> 让每次接手不必从零重建理解。正式版把它制度化并强制执行。

---

## 5. 任务分片规范（Work Unit）

**一个 Work Unit = 一次会话能干完的最小完整交付。**

硬性约束：

- 目标可以一句话说清
- 触及文件 ≤ 8 个
- **必须有机器可验证的验收标准**
- 独立提交，可单独回滚
- 依赖显式声明

`docs/tasks/WU-0231.md`：

```markdown
# WU-0231 · JNI 反射调用族

目标：实现 GetMethodID 与 CallXxxMethod 全部签名变体。
依赖：WU-0210（JNIEnv 函数表骨架）
文件：src/runtime/jni/*.cpp（≤6）

验收：
- [ ] tests/jni/reflection_test 全绿
- [ ] capabilities.toml 中 jni.reflection 状态由 partial 改为 complete
- [ ] 题库回归：无新增 unimplemented
```

**不允许出现"顺手改一下别的"**。发现别的问题 → 开新 WU。

---

## 6. 正确性可机器判定（命门）

AI 无法可靠地"目测"正确性。每一层能力都必须有对应的自动断言。

### L1 单元测试
纯逻辑：ELF 解析、重定位、指令编码、路径解析。

### L2 契约测试
HLE 函数的 ABI 行为。**基准来自真机/AOSP 源码，不是来自"看起来对"**。
例：`snprintf` 的返回值语义、`readdir` 的 `d_type`、JNI 局部引用表溢出行为。

### L3 黄金帧测试
headless 跑固定帧数，截图与基线比对（感知哈希 + 像素差阈值双判据）。
DEMO 已有 `TALESHLE_CAPTURE_*` 雏形，正式版做成一等公民。

**CI 必须能在无 GPU 环境运行** → ANGLE + SwiftShader/llvmpipe 后端。这条不满足的话，
黄金帧测试进不了 CI，整套机制就失效了。

### L4 题库回归矩阵
每个游戏一串检查点，每个检查点断言**可量化指标**而非人眼：

```toml
[[title.asphalt6.checkpoint]]
name = "main_menu"
reach = { touch_at_frame = 80, xy = [640, 360] }
assert_frame = 120
expect.draw_fbos_min = 2          # 渲染到纹理必须生效
expect.gl_errors = 0
expect.null_pointer_calls = 0     # 不允许有被吞掉的失败
expect.unimplemented_hle = 0
golden = "goldens/asphalt6/main_menu.png"
```

> 本次 A6 菜单缺失的 bug，`draw_fbos_min = 2` 这一条就能自动挡住。

---

## 7. 能力账本（Capability Ledger）

`capabilities.toml` —— 机器可读的"我们实现了什么"。

```toml
[libc.pthread_mutex_lock]
status = "complete"        # unimplemented | stub | partial | complete
test   = "tests/libc/pthread_test.cpp:mutex_recursive"

[jni.CallNonvirtualObjectMethodA]
status = "unimplemented"

[gl.glDrawElementsInstanced]
status = "stub"
note   = "返回但不绘制；等 GLES3 里程碑"
```

用途：

1. AI 接任务前查账本，不用猜也不用满仓库 grep。
2. 运行时自报：`unimplemented` 被触发时计数并告警。
3. CI 断言状态**只能前进不能后退**。
4. 直接生成对外的兼容性文档。

---

## 8. 反熵机制（防止 DEMO 阶段的问题重演）

DEMO 现状里有两个典型的熵增，正式版必须从制度上堵死：

### 8.1 禁止无主的开关

现状：`TALESHLE_*` 环境变量已经有 **80 个**，其中相当一部分是一次性排查用的，没人敢删。

规则：

- 所有配置进统一 schema，有 `id / 说明 / 默认值 / 生命周期`
- 生命周期：`experimental` → `stable` → `deprecated` → `removed`
- `experimental` 超过 N 个版本未晋级，CI 报错，强制处理
- 排查用的临时开关一律走 Agent Control API（见 [03](03-agent-interface.md)），
  不再新增环境变量

### 8.2 禁止无解释的魔数与 quirk

规则：任何游戏相关的硬编码必须挂在一个 **quirk id** 上，并且：

- 有文字说明"绕过的是游戏的什么行为"
- **有一个测试证明去掉它就会坏**
- quirk 没有对应测试 → CI 失败

这样五年后没人敢删的"神秘 if"就不会出现——每个 quirk 都自带存在理由和验证。

---

## 9. 度量与趋势

每次 CI 输出一行机器可读指标，只允许改善不允许退化：

```
titles_passing=37/52  hle_coverage=71.4%  unimplemented_hits=0
avg_fps_asphalt6=58.2  golden_frames_passing=104/104
```

---

## 10. 与 Cursor 能力的对应

| 需求 | 机制 |
| --- | --- |
| 强制工作流 | `AGENTS.md` + `.cursor/rules/` |
| 会话结束强制更新状态 | Cursor Hooks |
| 大范围调研不污染主上下文 | 并行 explore 子代理（本次规划就用了 3 个） |
| 操控/调试模拟器 | 自建 MCP Server（见 [03](03-agent-interface.md)） |
| 复用排查套路 | Skills（把 playbook 做成 skill） |
| 代码审查 | Bugbot / security-review 子代理 |

---

## 11. 落地顺序

这套流程**必须在大规模重写之前建好**，否则重写过程本身就会失控。

1. `AGENTS.md` + `docs/` 结构 + `CURRENT.md`
2. 测试骨架 L1/L3（黄金帧）+ CI（含软件渲染后端）
3. `capabilities.toml` + 运行时 unimplemented 计数
4. Agent Control API 最小可用版
5. 之后才开始 [07-roadmap](07-roadmap.md) 的 M1
