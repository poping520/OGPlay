# 当前状态

更新：2026-08-11 · M8 JNI Contract Complete；DexVM 设计定稿（ADR-0017，未启动）

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 的 `WU-0199..0327` 共 129 项已冻结为三个批次；历史任务不移动、不重编号，
  正式 M5 验收尚未声明。
- M6 AI 自动化测试已从 `WU-0328` 开始；目标是让 AI 与 CI 通过同一
  Profile-backed runner 执行 exact APK 的有界场景、输入、readback、断言和证据收集。
- M8 兼容性冲刺从 `WU-0360` 开始，新任务改用里程碑内编号 `WU-M8-001..`；Asphalt 6 按
  静态盘点→JNI/Java→GLES2→线程/VFS→媒体→主界面三轮 gate 批次推进。
- M8 JNI Guest ABI 扩展（`WU-M8-001..007`）为 Contract Complete：233 个 JNIEnv 槽中
  212 个 behavior-backed，JavaVM 4 个，其余 reserved 或显式 expected-unbound；绑定集合
  以精确集合等价机器验证。这只是契约结论，不代表任何 exact-title 运行里程碑。
- Windows/MSVC、Linux/x64 与 macOS/arm64 的 M4 基线均在 commit `f1b59bb` 以 ANGLE、
  warnings-as-errors 和严格全量 CTest 302/302 通过，见 [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |
| M5 去硬编码机制 | 待验收 | 尚未建立 | [三批索引](../tasks/m5/README.md) |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 进行中

- Asphalt 6 exact 已越过旧 mixed GLES buffer-state/client-pointer 故障；当前在 Profile
  native call 5 的 `CallStaticIntMethodV requires a valid class reference` 明确失败，尚未
  声称首帧或主界面。该失败是独立跟踪的 class reference 发布/解析问题，不是缺 JNI 槽。

## 最近完成

- [WU-M8-007] monitor 的临时中断与永久关闭拆成 `InterruptWaiters`/`Shutdown` 两个语义：
  interrupt generation 唤醒当前 waiter 但不禁用后续 `MonitorEnter`，session teardown 改为
  临时中断 → join child → guest fini → root detach → 永久 shutdown，finalizer 阶段仍可加锁。
  aggregate binding 改为精确集合等价并显式验证 expected-unbound 与 reserved 槽。
- [WU-M8-006] guest MonitorEnter/MonitorExit 接入真实可重入 monitor table；owner、recursion、
  waiter 与 condition wakeup 按 object identity 隔离，非 owner exit 明确失败，JavaVM detach
  释放 ownership。最终 aggregate 为 JNIEnv/JavaVM 212/4。
- [WU-M8-005] guest nonvirtual 30 槽复用现有 descriptor/A32 decoder 和 invocation engine；
  ABI 从 r3 取 method、从栈取首参数或 V/A pointer。ThrowNew 创建带 class/Modified UTF-8
  message 的真实 throwable，ExceptionDescribe 写结构化诊断且保留 pending identity。
  Windows/MSVC focused 3/3 与 full CTest 518/518 通过，当前 aggregate 为 JNIEnv/JavaVM
  210/4。
- [WU-OPT-CLOSURE-01] 正式闭合前 12 项优化验收：统一 active texture 与 GLES1 texture
  matrix unit，以 `(unit,target)` 隔离 2D/cube-map binding/metadata/delete，并让超采样下
  GLES1/GLES2 viewport/scissor query 返回 logical state；六类高频 setter 不再复制整个
  `SharedGlState`，2048 项 raw trace ring 改用独立 mutex。Windows/MSVC warnings-as-errors
  构建、focused 486/486 与 full CTest 509/509 通过；Asphalt 6 exact 仍越过旧 mixed
  GLES/client-pointer 故障，停在后续 `CallStaticIntMethodV` class reference 边界。
- [WU-0379] guest transfer 错误新增 `module!symbol`、寄存器、attribute provenance 与
  GLES1/GLES2 buffer binding 诊断。exact 将 null pointer 收敛到共同 `glBindBuffer`
  状态分裂，而非 guest 坏指针；full CTest 497/497。
- [WU-0378] Dungeon Hunter 第 75 帧命中的 guest 内置 PVRTC 解压批次经测量需约
  96.99 亿 tick；Profile/通用上限提高到仍受限的 100 亿，exact 120/240 帧均通过。
  `run-apk` 新增有界结构化启动、帧、长调用和 teardown 日志；full CTest 497/497。

## M6 起点

Scenario→exact Profile session→action/step→assertion→evidence/Result→shutdown 已闭合；
Asphalt 5 标题流三轮通过。OBB fixture 与 MCP GPU trace 仍明确未实现。

## 下一步（按优先级）

1. 修复 Asphalt 6 Profile native call 5 的 static class reference 发布/解析，再继续
   license/VFS/媒体与主界面 Scenario 三轮 gate。该阻塞与 JNI 槽位覆盖无关。
2. 把 `JniObjectArrayStore` 从 array binder 内部持有改为 session 级 Java object-model
   统一所有权；当前不影响正确性，已并入 DexVM 设计（`docs/design/dexvm/`）。
3. （非阻塞长线）DexVM 有界 DEX 解释器方案已定稿并 Accepted：ADR-0017 与
   `docs/design/dexvm/` 六章；启动排期待定，M8 继续按 profile 路线推进。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
