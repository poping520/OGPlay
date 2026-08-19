# DexVM · 有界 DEX 解释执行

设计根节点：[`docs/design/dexvm/`](../../design/dexvm/README.md)（ADR-0017）。
本专项把设计展开为可分片执行的 WU 批次；编号沿用子系统缩写 `DVM-N`，
创建后不移动、不重编号，依赖用文件名声明。

## 目标

Asphalt 5（pilot title）删除 profile 全部 `native_call` 与 `[[java.class]]` 段，
经 dexvm `dex_activity` 生命周期通过与迁移前相同的 exact Scenario gate 并进入
主界面。

## 批次划分（对应设计 06 §1 阶段）

| 批次 | 阶段 | 内容 | WU |
| --- | --- | --- | --- |
| 批次 0 | 阶段 0 · 地基 | AOSP 参考资料、题库测量、opcode 目录、dexasm | DVM-1..5 |
| 批次 1 | 阶段 1 · 解释器内核 | dex_code 读取、类链接、对象模型、指令家族、异常、clinit、GC-A | DVM-6.. |
| 批次 2 | 阶段 2 · 边界互通 | JNI 出向编组、入向第三路由、java.* P1 intrinsic | 随批次 1 收敛后分配 |
| 批次 3 | 阶段 3 · 生命周期反转 | Manifest 组件、dex_activity、android.* intrinsic、profile v2、pilot 迁移 | 随批次 2 收敛后分配 |

## 任务索引

| WU | 一句话目标 | 状态 |
| --- | --- | --- |
| [DVM-1](DVM-1.md) | AOSP Dalvik 本地参考资料说明 | 完成 |
| [DVM-2](DVM-2.md) | dex_dependency_survey 题库静态测量工具 | 完成 |
| [DVM-3](DVM-3.md) | 声明式 opcode 目录 + 生成器 + AOSP 机器比对 | 完成 |
| [DVM-4](DVM-4.md) | dexasm 确定性 DEX 汇编器核心 | 完成 |
| [DVM-5](DVM-5.md) | dexasm try/catch、payload 与静态初始值 | 完成 |
| [DVM-6](DVM-6.md) | loader.dex_code 指令流/try-catch/静态初始值受检读取 | 完成 |
| [DVM-7](DVM-7.md) | dexvm 类链接（注册/层级/布局/vtable/预检） | 完成 |
| [DVM-8](DVM-8.md) | JavaObjectModel 统一对象模型 + GC-A 预算 arena | 完成 |
| [DVM-9](DVM-9.md) | 解释器帧/分派与 moves/const/goto 家族 | 完成 |
| [DVM-10](DVM-10.md) | 算术/逻辑/比较/条件分支指令家族 | 完成 |
| [DVM-11](DVM-11.md) | 数组/实例/switch/payload 指令家族 | 完成 |
| [DVM-12](DVM-12.md) | invoke 家族与三路由解析、异常展开、clinit | 完成 |
| [DVM-13](DVM-13.md) | java.* P1 intrinsic（语言核心 + System/Math） | 完成 |
| [DVM-14](DVM-14.md) | JNI 出向编组（解释器→A32 native） | 完成 |
| [DVM-15](DVM-15.md) | JNI 入向第三路由（native→解释器） | 完成 |
| [DVM-16](DVM-16.md) | Manifest launcher activity 读取 | 完成 |
| [DVM-17](DVM-17.md) | Title Profile v2 schema（dex_activity） | 完成 |
| [DVM-18](DVM-18.md) | android.* intrinsic 首批 + dex_activity 生命周期装配 | 完成 |
| [DVM-19](DVM-19.md) | Asphalt 5 pilot v2 profile + exact 主界面 gate | 完成 |
| [DVM-20](DVM-20.md) | Scenario runner 易用性批次（失败日志/机读输出/预算算术/--fresh） | 完成 |
| [DVM-21](DVM-21.md) | Scenario runner --watch 增量编写模式 | 完成 |
| [DVM-22](DVM-22.md) | v2-only 启动作用域 schema | 完成 |
| [DVM-23](DVM-23.md) | 入口覆盖与 provisioned 前提 | 完成 |
| [DVM-24](DVM-24.md) | guest 静态字段预设 | 完成 |
| [DVM-25](DVM-25.md) | Profile v1 完全移除 | 完成 |
| [DVM-26](DVM-26.md) | Asphalt 6 启动作用域 exact gate | 完成 |
| [DVM-27](DVM-27.md) | 解释器 per-thread 执行状态拆分 | 完成 |
| [DVM-28](DVM-28.md) | Java Thread 1:1 宿主线程执行 | 完成 |
| [DVM-29](DVM-29.md) | monitor wait-set 与 Object.wait/notify | 完成 |
| [DVM-30](DVM-30.md) | Asphalt 6 首帧与主界面 exact gate | 完成（未达首帧，边界已固化） |
| [DVM-31](DVM-31.md) | 解释执行的 EGL10/GL10 façade | 实现完成（exact gate 未完成） |
| [DVM-32](DVM-32.md) | intrinsic handler 链接期绑定（消除逐调用查找） | 完成 |
| [DVM-33](DVM-33.md) | 解释器热路径 execution 传递（消除逐指令查找） | 完成 |
| [DVM-34](DVM-34.md) | intrinsic 声明即绑定：基础设施与双通道 | 完成 |
| [DVM-35](DVM-35.md) | java.* intrinsic 按类分文件迁移 | 完成 |
| [DVM-36](DVM-36.md) | android.* intrinsic 按类分文件迁移 + 生成器切换 | 完成 |
| [DVM-37](DVM-37.md) | handler 字符串 id 通道整体删除 | 完成 |
| [DVM-38](DVM-38.md) | android intrinsic 实现与声明完全同址 | 完成 |
| [DVM-39](DVM-39.md) | Android 启动期状态接口补齐 | 完成 |
| [DVM-40](DVM-40.md) | java.lang Throwable hierarchy 聚合与 API 19 class shape 补齐 | 完成 |
| [DVM-41](DVM-41.md) | java.lang primitive wrapper family 聚合与 API 19 公共接口补齐 | 完成 |
| [DVM-42](DVM-42.md) | GC-B 根枚举与 intrinsic 侧表闭集 | 完成 |
| [DVM-43](DVM-43.md) | GC-B 精确标记器（只统计） | 完成 |
| [DVM-44](DVM-44.md) | GC-B 清扫器、预算回减与句柄复用 | 完成 |
| [DVM-45](DVM-45.md) | GC-B host-backed 宿主状态析构 | 完成 |
| [DVM-46](DVM-46.md) | GC-B 安全点水位触发、OOM 与 Profile 参数 | 完成 |
| [DVM-47](DVM-47.md) | GC-B exact-title 与长运行 gate 复验 | 受阻（实现与 A5 GC gate 完成） |
| [DVM-48](DVM-48.md) | `java.lang.Thread` Core 能力栈 | 完成 |
| [DVM-49](DVM-49.md) | 统一 Java 对象与 intrinsic 状态底座 | 完成 |
| [DVM-50](DVM-50.md) | 线程 native 上下文与统一 monitor | 完成 |

## 批次 4 · 更多 title 上 dexvm 路线（进行中）

流程、工具与失败判读见
[`docs/playbook/NEW-TITLE.md`](../../playbook/NEW-TITLE.md)；staging profile 在
`data/profiles-dexvm/`，通过各自 gate 后再迁移进 `data/profiles/`。

进度：Dungeon Hunter 已越过链接与解释期，进入标题画面。DVM-31 已补齐
Asphalt 6 自带 `GLSurfaceView` 所需的 EGL façade 与条件 swap pacer；原 EGL
缺口和死锁均已越过；DVM-39 继续闭合实际命中的 Android/JNI/VFS 启动边界，
`asphalt6.bootstrap` 可见开场检查点三轮哈希一致、无 fault、clean shutdown，A6
启动 gate 已完成。主界面与可游玩 gate 尚未验收，状态以 `CURRENT.md` 为准。

其余在办项（GC-B exact/长运行验收、`dexvm.trace`/`dexvm.stack` 诊断、两款 title 的
Scenario gate 与 profile 迁移）以 [`docs/state/CURRENT.md`](../../state/CURRENT.md)
的滚动快照为准。
