# 01 · 目标、非目标与有界性

## 1. 要解决的两个结构性问题

**问题 A：生命周期副作用漏抄（正确性）。** profile 用人工逆向的 `native_call`
序列重放 Java 生命周期。Asphalt 6 的案例（`.local/docs/Asphalt6 推送初始化缺失分析.md`）
表明：游戏在 `onCreate` 里调用一串 `nativeInit`，唯一作用是让 native 侧缓存
`jclass`/`jmethodID`，之后的使用点没有空值保护；漏抄任何一次，故障在很远的
调用点爆发，且诊断只能靠反汇编回溯。这类问题在人工重放模式下**只能逐个补**，
无法穷尽。

**问题 B：profile 成本线性增长（可持续性）。** 现有三份 profile 各约 180–200 行，
其中约三分之二是 `[[java.class]]` 方法映射——本质是人工阅读 Java 方法体后，把
语义翻译为通用 impl id。这是在**人肉充当字节码解释器**。题库扩大时该成本不摊销。

两个问题的共同根因：HLE 边界画在"每个游戏自己的 Java 类"上。本方案把边界
下移到"平台类 API 面"，游戏类由解释器真实执行。

## 2. 目标

1. **正确性**：游戏自带 DEX 的生命周期、初始化副作用、胶水逻辑被真实执行，
   "漏抄"类故障结构性消失；未实现能力在**平台类边界**以记账 + 明确失败暴露，
   缺口列表机器可查询，直接驱动实现批次（延续 M8 的批次方法论）。
2. **可持续性**：新增一款同代游戏的边际成本收敛为
   "identity + 数据布局 + quirk"（预计 profile < 60 行）加上少量真实命中的
   平台类缺口；平台类实现被所有游戏共享。
3. **题库上限扩大**：libGDX、AndEngine、纯 Java 休闲游戏等 Java 厚层题目
   （roadmap 04 §7.1 判定为"需要 DEX"的类别）从结构上不可支持变为可支持。
4. **可观测性升级**：未捕获 Java 异常携带完整解释器栈回溯（类/方法/pc）；
   与现有结构化日志、capability ledger、Agent 查询接口同级接入。

## 3. 非目标（硬边界）

以下各项**不做**，且架构上不预留其实现路径。触碰任何一项须先立新 ADR：

| 非目标 | 说明 |
| --- | --- |
| framework/core 字节码执行 | 不加载 framework.jar / core-libart / 任何 ROM 侧 dex；平台类只有 intrinsic 一种形态 |
| JIT / AOT | 纯解释器。Java 层不是这代游戏的热点（roadmap 04 §7.3 结论） |
| odex / quickened / dex 038+ 新指令 | 只支持标准 dex 035 指令集；invoke-polymorphic 等新指令明确失败 |
| 多 ClassLoader 与动态加载 | 单一应用类命名空间；`DexClassLoader`/`loadDex` 记账失败 |
| JDWP / instrumentation / 注解运行时语义 | 注解结构解析可跳过，不提供运行时反射 |
| 完整反射 | 只提供显式枚举的最小反射面（见 03 §5）；`Method.invoke` 初期明确失败 |
| finalizer 语义 | `finalize()` 不被调用，记账可查询 |
| 完整 java.util/java.io 重实现 | 集合与 IO 是 intrinsic 最小集，按真实命中扩展，不预先照搬 JDK 面 |

ADR-0001"不实现 Binder、system_server、Zygote 或完整 ART/Dalvik"继续有效。
本方案与其兼容的判定方法保持一致：**游戏进程自己会执行这段字节码吗？**
游戏 DEX 里的类会——所以解释它在范围内；framework 的类不会以字节码形态
进入游戏进程边界——所以永远是 intrinsic。

## 4. 规模量级（用于投入判断）

| 组件 | 量级 | 依据 |
| --- | --- | --- |
| 指令集 | dex 035 已定义 opcode 约 218 项，按家族分批 | 官方 Dalvik opcode 表；目录 self-test 核对 |
| 类链接 | 目标游戏应用类数百个量级 | DEX L1 已输出应用类/方法规模报告 |
| java.* intrinsic 最小集 | 首批约 20 类（见 03 §3） | 对标 2010–2013 题目实际引用面 |
| android.* intrinsic | 复用现有 framework HLE 与 platform identity 的全部存量 | capabilities.toml 中 runtime.framework_* / android_legacy_* 条目 |
| GC | 分两期：预算 arena（无回收）→ 精确非移动 STW 标记清除 | 见 04 §5 |
| 已有可复用资产 | DEX L1 解析、JNI 全 ABI/对象身份/monitor/异常模型、A32 执行器、统一 Clock、能力账本、Scenario runner | 见 02 §2 复用表 |

**明确不重做的部分**：JNI 233 槽 ABI、guest ABI trap 分发、A32 调用执行器、
Bionic/syscall/VFS/GLES/audio 全部原样保留——dexvm 只是让"Java 侧"从 profile
声明变成真实执行，native 侧执行路径不变。

## 5. 成功标准

1. 选定的 pilot title（现有三款 Gameloft 之一）删除 profile 中全部
   `native_call` 与 `[[java.class]]` 段后，经 dexvm 生命周期通过与迁移前相同的
   exact Scenario 三轮 gate（帧数、golden、无 fault、clean shutdown 全部持平）。
2. Asphalt 6 推送初始化案例在 dexvm 路径下**不需要任何 profile 补丁**即自动
   闭合（`C2DMAndroidUtils.nativeInit` 由解释执行的 `onCreate` 链真实触发）。
3. 一款 Java 厚层 title（libGDX 或同代同类）通过启动 Scenario gate——这是
   profile 路线结构上给不出的结果。
4. 未实现指令与 intrinsic 缺口 100% 可经 `hle.unimplemented()` 同级机制查询，
   `null_pointer_calls = 0` 断言在 dexvm 路径同样成立。
