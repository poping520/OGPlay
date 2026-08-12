# 当前状态

更新：2026-08-12 · M9 DexVM 启动并达成 pilot gate：Asphalt 5 以 schema v2
`dex_activity` 全程解释执行进入主界面，golden 与 v1 路线逐位一致

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM 已启动**（`WU-M9-001..019`，ADR-0017、`docs/design/dexvm/`）：
  阶段 0（AOSP 基线/测量/opcode 目录/dexasm）、阶段 1（解释器内核）、
  阶段 2（JNI 双向桥 + java.* P1）、阶段 3（android.* intrinsic +
  dex_activity + profile v2 + pilot 迁移）全部交付。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除全部 16 条
  `native_call` 与 33 条 `[[java.class]]` 映射（201 行 v1 → 25 行 v2），
  `asphalt5.title_flow` 三轮 + 迁移后复验 passed——468 帧固定预算、主界面
  PNG SHA-256 `9ee57323…` 与 v1 逐位一致、无 fault、clean shutdown。
  `System.loadLibrary`(<clinit>)、onCreate 副作用链、GetStaticMethodID 查
  真实 DEX 方法表、native→解释器第三路由均真实发生。
- M8 Asphalt 6 仍在 Profile native call 5 的 class reference 明确失败处，
  与 M9 互不阻塞（该问题在 dexvm 路线下按设计自动消解，待 A6 迁移评估）。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。macOS/arm64 full CTest 558/558。

## M9 交付摘要

- `third_party/aosp-dalvik`（android-4.4.4_r2 浅 submodule）+ 锚点哈希门禁；
  opcode 目录 218 项由 bytecode.txt 机器派生并三锚点比对；dexasm 确定性
  汇编器（golden 锁字节 + Python/C++ 双解析器回读锁结构）。
- `loader.dex_code`（指令流/try-catch/静态初始值受检读取）、`loader.arsc`
  （resid↔路径事实，SoundPool 的 0x7f040009+n 由机器事实取代人工 pattern）、
  Manifest launcher activity 事实。
- `runtime/dexvm`：类链接（层级/布局/vtable/懒预检）、JavaObjectModel
  （字符串/基元数组委托存量 JNI store——native 与解释器同对象）、GC-A 预算
  arena、tagged 帧解释器全 dex 035 家族、异常展开、`<clinit>` 状态机、
  java.* P1 intrinsic；一致性夹具 20+ 用例对照 AOSP mterp 语义。
- `DexVmGuestBridge`：出向 descriptor→A32 编组（RegisterNatives→Java_ 导出），
  入向解释类注册进 JniClassRegistry（233 槽 ABI 不变的第三路由）；
  `DexActivityLifecycle` 生命周期反转；Title Profile v2（Python/C++ 双校验）。

## 下一步（按优先级）

1. M8 继续：Asphalt 6 class reference 失败——评估直接按 dexvm 方法级接管
   （04 §1 gate 0）或修 v1 装配；Dungeon Hunter 13 个滞留 impl id 是
   方法级接管的现成素材。
2. dexvm 记账缺口按命中批次闭合：J/D 出向返回解码、string 资源、
   MediaPlayer 完成回调、`dexvm.stats/stack` Agent 查询面（04 §8）。
3. 阶段 4（线程/wait-notify/GC-B/java.* P2P3）在厚层 title（libGDX 类）
   立项时启动；05 §4 gate 3。
4. Windows/Linux 三平台 M9 严格出口复验（当前仅 macOS/arm64 本地全绿）。
5. 存量性能 backlog 不变（ADR-0019 呈现管线方向等）。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
