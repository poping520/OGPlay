# 07 · AOSP 参考策略

从零凭记忆实现一个 Dalvik 语义正确的 VM 是本方案最大的工程风险。AOSP 的
Dalvik VM 完全开源（Apache-2.0），且 KitKat（android-4.4，API 19）恰好是
本项目 guest 目标的同代运行时——游戏实际面对的就是这套语义。本章定义
如何系统性地借力它，以及绝不越过的红线。

项目先例：CPU 不自写 JIT（dynarmic）、GLES 不自写转译（ANGLE）、PVRTC 不
自写解码（PowerVR SDK 原样 vendor）、GLES 目录逐项对齐固定 ANGLE 头文件
（`--verify-header`）。DexVM 对 AOSP Dalvik 采取同一姿态。

## 1. Vendor 决定

- 以浅 submodule 固定 vendor `platform/dalvik` 源码树（镜像
  `github.com/aosp-mirror/platform_dalvik`，网络白名单内），tag 锁定
  `android-4.4.4_r2`（KitKat 最终版，纯 Dalvik，无 ART 混杂）。
- 位置 `third_party/aosp-dalvik/`，遵守 ADR-0007（第三方一律 submodule）；
  `THIRD_PARTY_NOTICES.md` 增补 Apache-2.0 条目；CMake 校验脚本核对
  commit 固定（对齐 angle-prebuilt 的逐文件校验精神）。
- **默认不编译、不链接**。它是参考与机器校验的数据源，不是运行时依赖。
  唯一允许的编译形态见 §4 的 host 工具评估项。

## 2. 三种使用模式

**模式 A · 机器校验源（硬依赖，进 CTest）**。声明式目录不靠人抄，靠机器比对：

| 我方产物 | AOSP 校验锚点 | 校验内容 |
| --- | --- | --- |
| `data/dexvm/dalvik_opcodes.json` | `opcode-gen/bytecode.txt`（AOSP 自己的机器可读 opcode 定义源） | opcode 值、助记符、format、可抛/分支/返回标志逐项等价 |
| 同上 | `libdex/DexOpcodes.h` | 枚举值与名称表二次核对 |
| 生成的解码表 | `libdex/InstrUtils.cpp` 宽度/标志表 | 指令宽度与 format 归属 |

生成器提供 `--verify-aosp third_party/aosp-dalvik/...`，与
`gles.gles2_catalog` 的 `--verify-header` 完全同构。目录与 AOSP 表任何
分歧都是 CTest 失败。

**模式 B · 逐组件语义参考（实现纪律，进 WU 流程）**。实现每个组件前必须
研读对应 AOSP 实现，测试注释记录出处（文件 + 函数）。对照表：

| DexVM 组件 | AOSP 参考 | 参考什么 |
| --- | --- | --- |
| 指令语义（逐 opcode） | `vm/mterp/c/OP_*.cpp`（每 opcode 一个独立 C 文件）、汇总态 `vm/mterp/out/InterpC-portable.cpp` | 每条指令的精确语义：NaN 偏置、溢出、移位掩码、边界检查顺序、抛错时机 |
| 规范原文 | `docs/`（dalvik-bytecode、instruction-formats、dex-format 随源码同 tag 交付） | 语义仲裁的最终依据（与 mterp 冲突时以规范 + 夹具实测为准） |
| DEX 结构读取 | `libdex/DexFile.h`、`DexClass.cpp`、`DexCatch.h`、`DexProto.h`、`Leb128.h` | 结构布局、uleb128/sleb128、try/catch 迭代、签名遍历 |
| 严格结构校验 | `libdex/DexSwapVerify.cpp` | 逐节校验规则清单（我方 `loader.dex_code` 的反例集直接对标） |
| 链接预检 | `vm/analysis/CodeVerify.cpp` | 寄存器/分支/wide 对校验规则（我方只取其结构性子集，见 02 §5） |
| 类初始化 | `vm/oo/Class.cpp`（`dvmInitClass`、vtable 构建） | 初始化状态机、触发点、vtable/iftable 合并规则 |
| 常量池解析 | `vm/oo/Resolve.cpp` | class/method/field 解析与缓存时机 |
| 类型判定 | `vm/oo/TypeCheck.cpp` | instanceof/checkcast/数组协变的可赋值性规则 |
| 异常展开 | `vm/Exception.cpp`（`dvmFindCatchBlock`） | catch 匹配顺序、栈回溯构建 |
| monitor 与 wait/notify | `vm/Sync.cpp`（`waitMonitor`/`notifyMonitor`） | wait-set 状态机、中断投递时序、IllegalMonitorState 判定（薄锁/胖锁优化**不**参考） |
| GC-B | `vm/alloc/MarkSweep.cpp`、`Heap.cpp` | 精确根枚举口径、标记遍历、清扫时序（并发/分代特性**不**参考） |
| JNI 语义 | `vm/Jni.cpp` | 引用表、pending exception gate、参数检查顺序（我方 ABI 层保持 OGPlay 存量） |
| 线程附着 | `vm/Thread.cpp` | attach/detach 状态迁移与安全点交互 |
| java.* intrinsic 行为 | `vm/native/java_lang_*.cpp`（System.arraycopy、Class、Object、Runtime、Thread 等 native 半边） | 核心库 native 部分的精确行为（如 arraycopy 的重叠/类型检查顺序） |

**模式 C · 测试素材源**。`libdex` 与 `dexdump` 的解析行为作为 dexasm 产物
的独立裁判（见 §4）；AOSP 在树的 dex 测试数据可选择性纳入解析回归。

## 3. 红线（明确不做的借用）

| 不借用 | 原因 |
| --- | --- |
| 整体移植 dvm（对象模型/GC/线程/JNI 实现体） | 与 OGPlay 统一对象模型、1:1 宿主线程、统一 Clock、能力记账深度冲突；移植量 ≈ 重写量 |
| mterp 汇编快速路径、JIT（`vm/compiler/`） | 无 JIT 是 ADR-0017 硬边界；参考仅限 C 版语义 |
| odex/dexopt/quickened 路径 | 非目标 |
| 薄锁/偏向锁等同步优化 | 我方 monitor table 语义已验收；只参考 wait/notify 语义面 |
| `dx`/`dexmerger`（Java 工具链） | 离线不可用 Java 工具链；夹具走 dexasm（05 §1） |
| libcore（java.* 的 Java 侧实现，独立 repo） | 不 vendor；集合等 intrinsic 语义以 JLS/类库文档 + 夹具断言为准，`vm/native/*` 覆盖 native 半边 |

借用的判定准则：**只取语义，不取结构**。AOSP 代码里的平台耦合（bionic、
liblog、信号处理、内存布局假设）一概不进入 OGPlay。

## 4. Host 工具评估项（阶段 0 决定，非必需路径）

评估把 `libdex` + `dexdump` 编译为 host-only 测试工具（不进运行时、不进
发布物）：dexasm 每个夹具经 dexdump 反汇编回读，与 IR 预期比对，形成
"我方汇编 ↔ AOSP 解析"的交叉裁判。若 KitKat 代码在现代三平台工具链
（warnings-as-errors）下修补成本过高，则降级为模式 A/B，结论记入阶段 0
WU 文档——这是可选增强，不是阻塞项。

## 5. 分歧仲裁顺序

实现与参考不一致时按此顺序裁决，并把结论写进对应测试的注释：

1. `docs/` 规范原文（dalvik-bytecode / dex-format）；
2. AOSP 实现代码（mterp C / libdex）；
3. 真实 APK 上的实测行为（exact fixture）。

规范与实现罕见冲突时，以真实设备时代行为（即 AOSP 实现）为准——游戏是
对着实现写的，不是对着规范写的。
