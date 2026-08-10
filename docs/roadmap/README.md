# OGPlay · 正式版开发规划

本目录是项目从 DEMO 走向正式版的完整规划，**自成体系、不依赖其他文档**。
写给两类读者：**决策者**（判断方向与投入）和 **后续接手的 AI**（作为长期上下文的根节点）。

项目定名 **OGPlay**（`OG` + `Play`，玩那些老game），命名约定见 [08](08-naming.md)。

---

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [00 · 目标、范围与非目标](00-vision-and-scope.md) | 做什么、**不做什么**、成功标准、风险 |
| [01 · 总体架构与运作原理](01-architecture.md) | 分层设计、运行全流程、关键技术决策、已知失效模式、DEMO 差距盘点 |
| [02 · AI 主导开发的工程规范](02-ai-workflow.md) | 上下文管理、ADR、任务分片、可机器判定的验收、反熵 |
| [03 · Agent Control Interface](03-agent-interface.md) | AI 操控/调试模拟器的结构化接口 + MCP |
| [04 · Android 运行时层](04-android-runtime.md) | Bionic 三版本、syscall、完整 JNI、DEX 决策框架 |
| [05 · 去硬编码：Title Profile 与 Quirk](05-title-profiles.md) | 把游戏特判变成数据，通用性的核心 |
| [06 · 用户体验](06-user-experience.md) | 资源导入、输入映射与手柄、前端 |
| [07 · 开发路线图](07-roadmap.md) | M0–M9 里程碑与出口条件 |
| [08 · 项目命名](08-naming.md) | 命名决定与配套约定 |
| [09 · 日志与诊断系统](09-logging.md) | 结构化日志、环形缓冲、崩溃诊断包 |
| [10 · AI 自动化测试](10-ai-automation-testing.md) | exact-APK 场景、检查点、证据包与 AI/CI 共用执行层 |

[03](03-agent-interface.md) 与 [09](09-logging.md) 合起来构成整个可观测性体系：
前者是**主动查询**，后者是**被动记录**。

---

## 六条最关键的判断

如果只看六句话，是这六条：

1. **非目标清单比目标清单更重要。** 判定准则是"游戏进程自己会调用这个 API 吗"。
   越界一步就变成 Android 模拟器，永远做不完。（[00](00-vision-and-scope.md)）

2. **图形层改用 ANGLE，不要再手写 GLES→桌面 GL。** DEMO 阶段最难的一个 bug
   根因正是手写能力上报出的错。ANGLE 一次性解决 GLES 语义正确性 + 三平台 +
   无 GPU 的 CI。（[01 · 4.1](01-architecture.md)）

3. **加载真实 AOSP Bionic，只 HLE syscall 层。** HLE 面从约 2000 个 libc 函数缩到
   约 120 个 syscall，且三个 Android 版本变成"换一组 `.so`"而不是重写。
   （[04 · 3](04-android-runtime.md)）

4. **线程模型必须一开始就是真线程。** 协作式调度撑不住真实游戏——同类项目
   Bogodroid 在 Unity 上就死在这里。（[04 · 4.3](04-android-runtime.md)）

5. **M0 工程地基必须先做完，不可与功能开发并行。** AI 长周期开发的失败不是写不出代码，
   而是失忆、失控、静默退化。（[02](02-ai-workflow.md)）

6. **可观测性是一等公民，不是事后补的。** 当前排查一个问题要"加日志 → 重跑 90 秒 →
   再看"，而且地址还要人工去 IDA 里查。这个循环必须先打破。（[03](03-agent-interface.md) + [09](09-logging.md)）

---

## 一个反复出现的主题：不要让失败静默

DEMO 阶段最难的一次排查是这样的：游戏主界面的 3D 场景全部正常，但右侧一列菜单按钮
完全不显示。没有崩溃，没有 GL error，draw call 数量和正常情况一模一样。

根因链条是：我们上报的 GL 扩展串少了一项 `GL_OES_rgb8_rgba8`（而且游戏的分词器要求
字符串以空格结尾，否则最后一项被丢弃）→ 引擎认为不支持 RGBA8 渲染目标 →
`createRenderTarget` 返回 `nullptr` → 上层对空指针发起虚调用 → 被空页兼容层的
`bx lr` 静默吞掉 → 整条渲染到纹理的路径消失，但**没有任何一处报错**。

代码盘点显示这不是孤例，而是系统性的：当前有 **181 个 JNI 槽位**（占全表 78%）被自动
填成"返回 0"，**39 个 GLES1 函数**默认是空操作，`pthread_join` 不等待就返回成功，
`pthread_cond_signal/broadcast` 不唤醒任何人。这些桩都不报错。

这个教训贯穿整份规划：

- 未实现的 syscall **返回 ENOSYS 并记账**，不假装成功（[04 · 4.2](04-android-runtime.md)）
- 未实现的 JNI/GL 调用**进能力账本并计数**（[02 · 7](02-ai-workflow.md)）
- 任何"吞掉错误"的 quirk **必须配套计数与查询接口**（[05 · 4.3](05-title-profiles.md)）
- Agent 接口把 `hle.unimplemented()` / `hle.null_calls()` 做成**一等公民**（[03 · 3.6](03-agent-interface.md)）
- 日志系统禁止"打条 Warn 就算处理了"（[09 · 11](09-logging.md)）
- 题库回归把 `null_pointer_calls = 0` 作为**断言项**（[02 · 6](02-ai-workflow.md)）
