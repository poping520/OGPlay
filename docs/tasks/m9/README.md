# M9 · DexVM 有界 DEX 解释执行

设计根节点：[`docs/design/dexvm/`](../../design/dexvm/README.md)（ADR-0017）。
本里程碑把设计展开为可分片执行的 WU 批次；编号 `WU-M9-NNN`，规则与 M8 相同
（创建后不移动、不重编号，依赖用文件名声明）。

## 目标

Asphalt 5（pilot title）删除 profile 全部 `native_call` 与 `[[java.class]]` 段，
经 dexvm `dex_activity` 生命周期通过与迁移前相同的 exact Scenario gate 并进入
主界面。

## 批次划分（对应设计 06 §1 阶段）

| 批次 | 阶段 | 内容 | WU |
| --- | --- | --- | --- |
| 批次 0 | 阶段 0 · 地基 | vendor AOSP 基线、题库测量、opcode 目录、dexasm | WU-M9-001..005 |
| 批次 1 | 阶段 1 · 解释器内核 | dex_code 读取、类链接、对象模型、指令家族、异常、clinit、GC-A | WU-M9-006.. |
| 批次 2 | 阶段 2 · 边界互通 | JNI 出向编组、入向第三路由、java.* P1 intrinsic | 随批次 1 收敛后分配 |
| 批次 3 | 阶段 3 · 生命周期反转 | Manifest 组件、dex_activity、android.* intrinsic、profile v2、pilot 迁移 | 随批次 2 收敛后分配 |

## 任务索引

| WU | 一句话目标 | 状态 |
| --- | --- | --- |
| [WU-M9-001](WU-M9-001.md) | vendor AOSP dalvik 固定 tag 参考基线 + 校验 + NOTICES | 完成 |
| [WU-M9-002](WU-M9-002.md) | dex_dependency_survey 题库静态测量工具 | 完成 |
| [WU-M9-003](WU-M9-003.md) | 声明式 opcode 目录 + 生成器 + AOSP 机器比对 | 完成 |
| [WU-M9-004](WU-M9-004.md) | dexasm 确定性 DEX 汇编器核心 | 完成 |
| [WU-M9-005](WU-M9-005.md) | dexasm try/catch、payload 与静态初始值 | 完成 |
| [WU-M9-006](WU-M9-006.md) | loader.dex_code 指令流/try-catch/静态初始值受检读取 | 完成 |
| [WU-M9-007](WU-M9-007.md) | dexvm 类链接（注册/层级/布局/vtable/预检） | 完成 |
| [WU-M9-008](WU-M9-008.md) | JavaObjectModel 统一对象模型 + GC-A 预算 arena | 完成 |
| [WU-M9-009](WU-M9-009.md) | 解释器帧/分派与 moves/const/goto 家族 | 完成 |
| [WU-M9-010](WU-M9-010.md) | 算术/逻辑/比较/条件分支指令家族 | 完成 |
| [WU-M9-011](WU-M9-011.md) | 数组/实例/switch/payload 指令家族 | 完成 |
| [WU-M9-012](WU-M9-012.md) | invoke 家族与三路由解析、异常展开、clinit | 完成 |
| [WU-M9-013](WU-M9-013.md) | java.* P1 intrinsic（语言核心 + System/Math） | 完成 |
| [WU-M9-014](WU-M9-014.md) | JNI 出向编组（解释器→A32 native） | 完成 |
| [WU-M9-015](WU-M9-015.md) | JNI 入向第三路由（native→解释器） | 完成 |
| [WU-M9-016](WU-M9-016.md) | Manifest launcher activity 读取 | 完成 |
| [WU-M9-017](WU-M9-017.md) | Title Profile v2 schema（dex_activity） | 完成 |
| [WU-M9-018](WU-M9-018.md) | android.* intrinsic 首批 + dex_activity 生命周期装配 | 完成 |
| [WU-M9-019](WU-M9-019.md) | Asphalt 5 pilot v2 profile + exact 主界面 gate | 完成 |
| [WU-M9-020](WU-M9-020.md) | Scenario runner 易用性批次（失败日志/机读输出/预算算术/--fresh） | 完成 |
| [WU-M9-021](WU-M9-021.md) | Scenario runner --watch 增量编写模式 | 完成 |
| [WU-M9-022](WU-M9-022.md) | v2-only 启动作用域 schema | 完成 |
| [WU-M9-023](WU-M9-023.md) | 入口覆盖与 provisioned 前提 | 完成 |
| [WU-M9-024](WU-M9-024.md) | guest 静态字段预设 | 完成 |
| [WU-M9-025](WU-M9-025.md) | Profile v1 完全移除 | 完成 |
| [WU-M9-026](WU-M9-026.md) | Asphalt 6 启动作用域 exact gate | 进行中 |

## 批次 4 · 更多 title 上 dexvm 路线（进行中）

流程、工具与失败判读见
[`docs/playbook/NEW-TITLE.md`](../../playbook/NEW-TITLE.md)；staging profile 在
`data/profiles-dexvm/`，通过各自 gate 后再迁移进 `data/profiles/`。

进度：Dungeon Hunter 已越过链接与解释期，进入标题画面。Asphalt 6 已由 v2 entry
直接进入引擎 Activity，provisioned-data preset 实际应用，并越过 Activity/GL surface/
电话状态/URL encoding 等启动链；当前首个边界是 native `FindClass` 看不到 DexVM 平台
intrinsic `android/content/Intent`，需统一 native→DexVM intrinsic JNI 可见性，不能伪造成功。

其余在办项（GC-B 精确标记清除、`dexvm.trace`/`dexvm.stack` 诊断、两款 title 的
Scenario gate 与 profile 迁移）以 [`docs/state/CURRENT.md`](../../state/CURRENT.md)
的滚动快照为准。
