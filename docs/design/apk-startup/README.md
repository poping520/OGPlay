# APK Startup · Manifest 驱动的应用进程启动

本目录定义 OGPlay 从“Title Profile / root `.so` 驱动启动”迁移到
“APK 自描述驱动启动”的完整设计。文档写给两类读者：**架构评审者**和
**逐 WU 执行实现的 AI**。

Android 语义基线固定为 `android-4.4.4_r2.0.1` / API 19；OGPlay 仍然是
兼容层，不启动 Android 系统镜像，不执行 framework/core DEX。

## 效力声明

- 本目录负责 **APK process startup、Application 初始化、launcher Activity、
  APK native library 按需加载、Profile 降级为兼容覆盖**。
- `docs/design/dexvm/` 继续负责 DEX 解释器、对象模型、intrinsic、JNI/Dex 双向桥；
  本设计不重写 DexVM 内核。
- 与 `docs/design/entry-scope/` 中“Profile 决定入口/root native 模块”的旧假设
  冲突时，以本目录为准；选择性裁剪非目标商业外壳的能力仍可作为 quirk 保留。
- 与 `docs/design/dexvm/04-integration.md` 中“Title Profile identity 精确匹配永久作为
  启动安全边界”的旧假设冲突时，以本目录为准：**Profile 不再是启动资格门禁**。
- 项目元规范不变：WU 有界、机器可判定验收、明确失败、结构化日志、能力记账、
  `src/` 零游戏名分支。

## 一句话架构

> 先从 APK/Manifest 建立应用事实和单一进程 ABI；创建 native process shell、
> DexVM 与 JNI bridge；执行最小 Application 初始化；再从 Manifest 启动
> MAIN/LAUNCHER Activity；应用 Java 代码真实执行到 `System.load` /
> `System.loadLibrary` 时，才把对应 APK `.so` 动态装入同一进程并执行其
> ELF constructors 与 `JNI_OnLoad`。

目标启动链：

```text
APK parse
  ↓
Manifest / package / native inventory
  ↓
AndroidAppProcess
  ├─ VFS / sandbox / package context
  ├─ guest address space / Bionic / syscall / boundary
  ├─ process ABI
  ├─ JavaVM / JNI bridge
  └─ NativeLibraryLoader
  ↓
DexVM ready
  ├─ classes.dex
  ├─ platform intrinsics
  ├─ Resources / Context
  └─ DexVM ↔ JNI bridge
  ↓
Application startup
  ↓
MAIN/LAUNCHER Activity startup
  ↓
Java execution reaches System.load{,Library}
  ↓
dynamic ELF load + constructors + JNI_OnLoad
```

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [01 · 目标、非目标与成功标准](01-scope.md) | 任务边界与完成定义 |
| [02 · 当前路径与结构性问题](02-current-state.md) | 当前 `run-apk` 为什么难以通用化 |
| [03 · 目标进程架构](03-target-architecture.md) | `AndroidAppProcess`、process shell 与启动状态机 |
| [04 · Manifest、Application 与 Activity](04-manifest-and-lifecycle.md) | APK 入口事实与最小生命周期语义 |
| [05 · Native library 动态加载](05-native-loading.md) | `System.load*`、ELF、JNI_OnLoad、依赖与幂等 |
| [06 · ABI 与 Title Profile 迁移](06-profile-and-abi.md) | ABI 归属、Profile v3 与旧 profile 兼容 |
| [07 · 重入、失败语义与所有权](07-reentrancy-and-failure.md) | Java↔native 嵌套、锁纪律、失败恢复 |
| [08 · 验证体系](08-verification.md) | 单元、集成、exact-title gate 与 DoD |
| [09 · AOSP 本地参考策略](09-aosp-reference.md) | 三棵本地 AOSP 树和逐组件参考规则 |
| [10 · 实施阶段与 WU 映射](10-implementation-plan.md) | 分阶段落地顺序与任务索引 |

## WU 任务书

实施任务不写进本目录。任务书统一归档于：

```text
docs/tasks/apk-startup/
```

命名规则、归档规则、模板与当前 APS-1..9 见
[`docs/tasks/apk-startup/README.md`](../../tasks/apk-startup/README.md)。

## 固定术语

| 术语 | 含义 |
| --- | --- |
| AndroidAppProcess | APK 启动期的 session/process 级聚合对象；持有 package、ABI、VFS、DexVM、native runtime、loader、Application/Activity 状态 |
| process shell | 尚未装入任何应用 `.so`，但 guest address space、Bionic/syscall/boundary/JNI 等已可用的进程空壳 |
| application native library | APK `lib/<abi>/` 中由应用通过 `System.load*` 显式请求的 `.so` |
| explicit load | Java `System.load` / `System.loadLibrary` 触发的 VM 级 native load 操作 |
| linker dependency | ELF `DT_NEEDED` 递归依赖；不等价于一次 Java explicit load |
| Application startup | 本任务的最小 `Application` 初始化：类初始化、实例化、attach base Context、`onCreate()` |
| Profile override | APK 自身无法表达、且经验证确有必要的 title-specific 兼容覆盖；不是启动事实真源 |
