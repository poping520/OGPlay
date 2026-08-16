# M8 · 兼容性冲刺

M8 以 M6 的 Profile-backed Scenario runner 为唯一 exact-title 验证出口，按
“先盘点、后分组、再实现”推进。不得退回人工 smoke，也不得为单个缺失符号
创建一个 Work Unit。

Asphalt 6 是首个 M8 目标，以
`docs/demo/games/Asphalt6/Asphalt_6_Adrenaline_HD_GLstore_v1.3.2.apk` 和对应外置
payload 为本地 fixture；APK、payload、证据和宿主绝对路径不进入 Profile、Scenario
或 Result。

M8 的 Work Unit 文件名使用里程碑内递增编号 `WU-M<里程碑>-<三位序号>.md`，例如
`WU-M8-001.md`。Layout UI 专项使用语义化子系统编号 `LUI-1.md`。M8 早期沿用四位全局编号创建的
`WU-0360..0379` 保持原名不重编号，新任务一律使用里程碑内编号。

适配按以下批次切分，只在实际开始时分配下一个里程碑内编号：

| 顺序 | 批次 | 机器出口 |
| ---: | --- | --- |
| 1 | exact 身份、payload 布局和静态能力矩阵 | Profile 校验、ELF/Bionic 预检和聚合缺口清单 |
| 2 | JNI/Java 调用族 | 以 DEX、ELF 字符串和有界启动现场一次统计类/方法/签名/槽位，按通用语义批量实现；**仅限语义无歧义的调用族**（见下"Java 胶水过渡纪律"） |
| 3 | GLES2/RTT 调用族 | 对目标 96 个 GL/EGL 导入做已绑定/未绑定矩阵，按 state/resource/query/draw 批量闭合 |
| 4 | 线程、VFS 与安卓边界 | 真宿主线程、外置数据和启动阶段在统一 session 中有界运行 |
| 5 | 音频/影片及其他主界面前置能力 | 只补真实命中的通用缺口，不伪造播放或网络成功 |
| 6 | 主界面 Scenario 与 gate | 启动→标题触摸→Main Menu golden→shutdown 连续三轮稳定 |

每个实现批次仍须遵守单 WU 不超过 10 个文件；批次内允许一次实现多个同类
函数，但不允许把 JNI、GLES、VFS 和线程无边界混在同一 WU。

## Java 胶水过渡纪律（WU-M8-011 复盘后生效）

WU-M8-011 实证：Dungeon Hunter profile 引用 16 个无 handler 的 impl id，人工
只能诚实闭合语义无歧义的 3 个（analytics 记账/计数），license/billing/online
等 13 个的返回值改变游戏行为，逐个反编译取证的成本随题量线性且不摊销。
据此（详见 [design/dexvm 06 裁决 13/14](../../design/dexvm/06-migration.md)）：

- v1 `[[java.class]]` 通用 handler 目录**冻结增长**：只补当前 gate 实际阻塞
  且语义无歧义的调用族；行为敏感组保持 missing-handler 明确失败，缺口清单
  登记为 DexVM 方法级接管候选，不再人工实现。
- profile impl id ↔ handler 目录对账应做成机器门禁（小 WU）：全部
  `data/profiles/*.toml` 引用的 impl id 对照代码注册目录出缺口报告，
  让缺口在构建期可见而不是运行期点击时爆出。
- 每次人工补 handler 在 WU 文档记录实际成本（定位、取证、触及文件数），
  作为 DexVM 排期的证据流。

## JNI Guest ABI 扩展工作单（里程碑内编号）

| Work Unit | 标题 | 状态 | 目标 |
| --- | --- | --- | --- |
| [WU-M8-001](WU-M8-001.md) | JNI Guest Binding 统一注册入口 | 完成 | 用唯一 `BindJniGuestSlots` context 组合全部 family 并封口 dispatcher |
| [WU-M8-002](WU-M8-002.md) | RegisterNatives / UnregisterNatives | 完成 | RegisterNatives/UnregisterNatives 接入唯一 `JniNativeRegistry` 与通用 A32 executor |
| [WU-M8-003](WU-M8-003.md) | Instance Field 与 UTF-16 String | 完成 | 19 个 instance field 槽与 5 个 UTF-16 string 槽复用统一 field/string store |
| [WU-M8-004](WU-M8-004.md) | Primitive Array 与 Object Array | 完成 | 8 类 primitive 的 40 槽与 object array 3 槽接入统一 array store 和受检 guest arena |
| [WU-M8-005](WU-M8-005.md) | Nonvirtual Call + Exception API | 完成 | 30 个 CallNonvirtual 槽复用 descriptor 驱动 ABI，ThrowNew/ExceptionDescribe 使用真实 throwable |
| [WU-M8-006](WU-M8-006.md) | JNI Monitor | 完成 | MonitorEnter/MonitorExit 使用真实可重入、按 object identity 隔离的 monitor table |
| [WU-M8-007](WU-M8-007.md) | JNI M8 验收闭环 | 完成 | 修复 monitor 临时中断/永久关闭生命周期，binding 改为精确集合等价，闭合索引与能力记录 |

M8 JNI Guest ABI 扩展在 WU-M8-007 后为 Contract Complete：233 个 JNIEnv 槽中 212 个
behavior-backed，JavaVM 4 个，其余为 reserved 或显式 expected-unbound。exact-title 运行
里程碑与之独立，见 `docs/state/CURRENT.md`。

## Runtime 结构重构工作单（ADR-0018）

| Work Unit | 标题 | 状态 | 目标 |
| --- | --- | --- | --- |
| [WU-M8-008](WU-M8-008.md) | 迁移 guest JNI ABI 到 runtime/jni_guest | 完成 | 8 个 `jni_guest_*` 实现与 7 个公共头纯机械迁出 integration |
| [WU-M8-009](WU-M8-009.md) | 迁移 Android native 边界到 runtime/boundary | 完成 | boundary HLE、GLES1/GLES2 组件、GuestGlContext 与 A32CallFrame 迁出 integration |
| [WU-M8-010](WU-M8-010.md) | integration 契约收敛与模块文档瘦身 | 完成 | 三份 MODULE.md 与模块索引和拆分后代码事实一致 |

## 兼容性批次工作单

| Work Unit | 标题 | 状态 | 目标 |
| --- | --- | --- | --- |
| [WU-M8-011](WU-M8-011.md) | Dungeon Hunter analytics 调用族 handler | 完成 | 闭合 track_first_run/启动计数 analytics 族；license/服务组保持明确失败待反编译证据 |

## 早期全局编号工作单

- [WU-0360](WU-0360.md)：Asphalt 6 exact 身份、静态能力矩阵与根 SONAME 别名预检。
- [WU-0361](WU-0361.md)：exact APK + external payload 有界启动采样与四组缺口聚合。
- [WU-0378](WU-0378.md)：恢复 Dungeon Hunter 有界加载预算并补齐 run-apk 结构化进度日志。
- [WU-0379](WU-0379.md)：定位 Asphalt 6 混合 GLES draw 的 buffer-state 分裂根因。
