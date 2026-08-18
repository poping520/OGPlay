# 03 · 目标进程架构

## 1. 顶层对象：`AndroidAppProcess`

本任务需要一个 session 级聚合边界，避免 frontend 同时持有 VFS、DexVM、JNI、
ELF namespace、Application 与 Activity 的半成品状态。

概念接口：

```cpp
class AndroidAppProcess {
public:
    static Result<std::unique_ptr<AndroidAppProcess>> Create(CreateRequest);

    Result<void> Prepare();
    Result<void> StartApplication();
    Result<void> StartLauncherActivity();
    Result<void> Stop();

    NativeLibraryLoader& NativeLibraries();
    DexVmGuestBridge& DexVm();
};
```

名称可按现有模块风格调整，但责任边界不得退回 frontend。

## 2. 所有权

`AndroidAppProcess` 生命周期覆盖完整一次 APK 运行，并持有或稳定引用：

```text
AndroidAppProcess
 ├─ ApkIdentity / ManifestModel
 ├─ selected GuestAbi
 ├─ ApkNativeLibraryInventory
 ├─ VFS / sandbox / Resources / Context
 ├─ AndroidGuestProcess (native process shell)
 │   ├─ guest address space
 │   ├─ Bionic / syscall
 │   ├─ Android/GLES/audio boundary
 │   ├─ JavaVM / JNI runtime
 │   └─ dynamic ELF namespace
 ├─ NativeLibraryLoader
 ├─ DexVmGuestBridge
 ├─ Application instance
 └─ Activity lifecycle / current Activity
```

资源释放必须按依赖反序进行；尤其 DexVM/JNI 回调能力不能在 native module
finalization 前销毁。

## 3. 两层 process 概念

### 3.1 `AndroidGuestProcess`

负责“一个 ARM Android 用户进程能执行 native 代码”的基础设施，不知道 launcher、
Application 或 title profile。

创建完成的最低条件：

- guest address space 可分配/映射；
- syscall dispatcher、Bionic 运行时基础状态已建立；
- boundary/HLE providers 可解析；
- JavaVM/JNIEnv 与 JNI invocation engine 可用；
- 线程 attach 与 guest call executor 可用；
- ELF namespace 支持后续追加 module。

这里允许装入运行时所需的 guest system libraries；**不允许装入 APK application
library 作为 root**。

### 3.2 `AndroidAppProcess`

在 guest process shell 上叠加 APK 语义：package/VFS/ABI/DexVM/Application/Activity/
native inventory/profile override。

## 4. 启动状态机

建议显式状态，而不是用“某指针非空”推断：

```text
Created
  ↓ PreparePackage
PackageReady
  ↓ PrepareNativeProcess
NativeProcessReady
  ↓ PrepareDexVm
DexVmReady
  ↓ StartApplication
ApplicationStarted
  ↓ StartLauncherActivity
ActivityResumed
  ↓ Stop
Stopped
```

非法跨阶段调用必须明确失败。例如 `StartLauncherActivity()` 在 `DexVmReady` 前调用
属于编程错误，不隐式补步骤。

## 5. `Prepare()` 的顺序

推荐：

```text
1. parse/finalize Manifest model
2. build APK native inventory
3. select process ABI
4. create VFS/sandbox + package Context/Resources
5. create native process shell
6. create JavaVM/JNI shared state
7. create DexVM and register application classes/intrinsics
8. wire DexVM ↔ JNI ↔ NativeLibraryLoader
9. mark DexVmReady
```

“DexVM 在入口 Activity 前准备好”的实现落点即步骤 7–8 在任何 Application/Activity
应用代码前完成。

## 6. native loader 的生命周期

`NativeLibraryLoader` 必须从 `DexVmReady` 前就存在并一直活到 process teardown。
原因是 load 可能发生于：

- Application `<clinit>`；
- Application constructor/`onCreate()`；
- Activity `<clinit>`；
- Activity constructor/`onCreate()`；
- renderer/view 初始化；
- 任意后续运行期类初始化。

它不是“启动阶段工具”，而是 process service。

## 7. stop 顺序

建议：

```text
pause/stop/destroy Activity as supported
  ↓
release Application references / Java roots
  ↓
stop app-driven guest threads at existing safe boundary
  ↓
finalize dynamically loaded modules in reverse linker order
  ↓
release DexVM/JNI shared state
  ↓
release native process shell / VFS
```

实现应复用现有 session stop/finalization 能力；本设计不要求新增 Android 完整
`Application.onTerminate()` 语义（真实设备进程也不以它作为常规退出保证）。
