# 02 · 当前路径与结构性问题

本文只记录实施前需要理解的责任边界，不要求实现者保持现有结构。

## 1. 当前 `run-apk` 启动形态

当前主路径可概括为：

```text
Parse APK / Manifest / APK native libs
  ↓
MatchApkTitleProfile(package + version + so_sha256 + abi)
  ↓
PrepareApkProfileLaunch(root native module + module inputs)
  ↓
AndroidGuestCallSession::Start(... root module ...)
  ↓
map ELF namespace / init Bionic / constructors
  ↓
create DexVmGuestBridge
  ↓
resolve dex_activity launcher
  ↓
frontend calls InitializeJniLibrary() for root module
  ↓
DexActivityLifecycle::Start()
```

这个顺序把三个本应独立的概念绑在了一起：

1. 创建一个 Android guest process；
2. 装入某个应用 native library；
3. 执行 Android Application/Activity 入口。

## 2. 现有组件可复用部分

以下资产不应重写：

| 组件 | 继续复用的能力 |
| --- | --- |
| APK/Manifest loader | APK 读取、二进制 Manifest 基础解析、DEX/native entry 读取 |
| VFS / sandbox | `/data/data/<package>`、`/sdcard`、profile data mount 与持久层 |
| Bionic/runtime | guest address space、syscall、Bionic guest libs、boundary/HLE libs |
| JNI runtime | JavaVM/JNIEnv、对象/方法/字段 identity、native↔Java 调用编组 |
| DexVM | 应用 DEX 解释、平台 intrinsic、类初始化、Java→native method bridge |
| DexActivityLifecycle | Activity 类初始化、构造、`onCreate/onStart/onResume` 与 surface/frame 驱动 |

本任务主要是**重新分配所有权和启动时机**，不是替换这些实现。

## 3. 必须拆开的现有耦合

### 3.1 Profile match 与“能否启动”耦合

`MatchApkTitleProfile` 当前承担启动 gate。目标状态中它只能回答：

> 是否存在一个可应用于这个 APK 的兼容性覆盖？

答案为“否”必须仍可继续通用启动。

### 3.2 process creation 与 root module load 耦合

`AndroidGuestCallSession::Start` 当前要求 root module / modules，并在启动时装完整
ELF namespace。目标状态需要允许：

```text
Create process shell
  ↓
(no application library loaded)
  ↓
DexVM starts executing Java
  ↓
NativeLibraryLoader appends application library later
```

因此 runtime API 必须支持“先有可执行 guest process，再动态增加 ELF module”。

### 3.3 frontend 与 `JNI_OnLoad` 耦合

`run_apk.cpp` 不应该知道哪个 `.so` 是“JNI root”。`JNI_OnLoad` 属于 VM native
library load 语义，必须下沉到 `NativeLibraryLoader`。

### 3.4 `System.loadLibrary` intrinsic 是空操作

现有 DexVM 平台 handler 已有 `System.loadLibrary` 注入点，但设计上依赖 session
预加载 library。目标状态中该 handler 必须调用真实 process 级 loader，并把成功/
失败按 Java `System/Runtime` 语义返回。

### 3.5 缺少 Application startup

当前 `DexActivityLifecycle` 从 launcher Activity 类初始化开始。自定义 Application
可能先建立 singleton、静态字段、native registration 等前置状态；直接跳 Activity
会留下新的 title-specific 兼容缺口。

## 4. 前端最终应瘦身到什么程度

`run_apk.cpp` 最终只负责 CLI/session 编排：

```text
parse args
read APK
build optional compatibility profile
create AndroidAppProcess
process.Prepare()
process.StartApplication()
process.StartLauncherActivity()
run frame loop
process.Stop()
```

它不应再：

- 选 root `.so`；
- 以 SHA-256 决定能否启动；
- 组织应用 ELF module graph；
- 显式调用某个 root module 的 `JNI_OnLoad`；
- 手工决定 Application 与 Activity 的 Java 初始化顺序。
