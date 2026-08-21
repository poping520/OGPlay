# 09 · AOSP 本地参考策略

本任务涉及 Android 启动与 Dalvik native load 语义，禁止凭记忆实现。实现者使用仓库
外的本地固定源码树作为语义参考。

## 1. 固定本地参考根

```text
.local/aosp/dalvik
.local/aosp/framework
.local/aosp/libcore
```

三棵树仅作参考：**不入库、不加入构建、不链接、不复制大段实现代码**。
目标版本应与项目 Android 4.4.4 / API 19 基线一致；若本地 checkout tag 与文档记录
不一致，WU 先记录事实再继续。

## 2. framework 参考

根：`.local/aosp/framework`

实现 Manifest/Application/Activity 启动语义前，至少核对：

```text
core/java/android/app/ActivityThread.java
core/java/android/app/LoadedApk.java
core/java/android/app/Application.java
core/java/android/app/Activity.java
core/java/android/content/pm/PackageParser.java
```

重点问题：

- launcher/ActivityInfo 最终使用的类名；
- `LoadedApk.makeApplication` / Application attach / onCreate 顺序；
- `performLaunchActivity` 中 classloader、Activity 实例化和生命周期顺序；
- Manifest component class-name 归一化；
- activity-alias 的解析关系。

OGPlay 只取**可观察语义与顺序**，不复制 `ActivityThread`、Instrumentation、Binder、
PackageManager 等系统结构。

## 3. libcore 参考

根：`.local/aosp/libcore`

至少核对对应 KitKat 的：

```text
luni/src/main/java/java/lang/System.java
luni/src/main/java/java/lang/Runtime.java
```

若具体 checkout 路径不同，用仓库内 `find/rg` 定位同名源码并在 WU 任务书记录实际路径。

重点问题：

- `System.load(path)` 如何转给 Runtime；
- `System.loadLibrary(name)` 如何带 caller ClassLoader；
- logical library name / `findLibrary` / mapped filename 的边界；
- Java 侧抛错类型和 message 来源。

## 4. Dalvik 参考

根：`.local/aosp/dalvik`

至少核对：

```text
vm/Native.cpp
```

并按代码跳转追踪 native library table / class loader identity / JNI_OnLoad 处理。
重点确认：

- 同库同 ClassLoader 重复加载；
- 同库不同 ClassLoader；
- `JNI_OnLoad` 触发时机；
- 加载失败状态；
- VM 如何区分 explicit native load 与 linker dependency。

若具体实现分散到其它 `vm/` 文件，任务书必须记录“文件 + 函数”锚点。

## 5. 三种使用模式

### A. 语义参考

用于回答“应该发生什么”。例如 Application onCreate 是否在 Activity 前、
JNI_OnLoad 何时执行。

### B. 测试 oracle

把参考语义转成 OGPlay 可控 fixture 的断言，不要求在测试中编译 AOSP。

### C. 结构排除

AOSP 中依赖 system_server/Binder/Zygote/PackageManager/Instrumentation 的结构明确不搬；
找到它们是为了知道哪些步骤由 OGPlay 用等价最小动作代替。

## 6. WU 参考记录纪律

APS-1、4、5、6、7 开始编码前必须在任务书“语义出处”段补：

```text
.local/aosp/<tree>/<file>
  - <function/method>: <本 WU 采用的语义结论>
```

如果实现与 AOSP 可观察语义不同，必须同时记录：

1. 差异；
2. 为什么 OGPlay 的有界目标允许差异；
3. 对应测试如何防止差异继续扩大。

“我记得 Android 是这样”不能作为评审依据。
