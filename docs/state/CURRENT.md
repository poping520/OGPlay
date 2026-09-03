# 当前状态

更新：运行入口自动使用内置 Android 4.4.4/API 19 guest 系统库。

## 当前阶段

- **运行与 GUI**：`run-apk` 按 Profile API 自动选择内置系统库，不再接受
  `--system-dir`；GUI-17/18 双栏界面支持选择、启动和删除。缺 Profile 条目仍可尝试
  generic 启动但不宣称兼容。Profile frame 日志已降为 `debug`，周期从 60 帧调至 600 帧。
- **API 19 系统库**：`data/android/19/lib` 内置从 AOSP
  `android-4.4.4_r2.0.1` clean tag 构建的 libc/libm/libdl/libstdc++/libz，随包携带来源、
  哈希、ELF 身份、构建记录和 NOTICE；构建、安装及 macOS bundle 均统一 staging。
  API 22/23 尚未纳入。
- **Java core**：File streams 公共 API（NIO/FileChannel 除外）、Resources XML 有界 pull、
  SimpleDateFormat API 19 最小层级以及 Observer/Observable 已闭合。Observable 复用
  `ArrayList`，按 changed flag 门控并在 receiver monitor 外按快照虚派发 `update`；覆盖
  回调删除、异常传播和 nested GC 强根保活。Object streams 已闭合 API 19 接口继承、
  primitive/block-data、默认 `Serializable` 字段图、引用、枚举及 `Date` 往返；数组、
  Externalizable/custom hooks 与默认 UID 计算仍 deferred。格式化/解析及完整 XmlPullParser
  长尾仍 deferred。
- **Android Context 文件流**：`openFileInput/openFileOutput` 已接入 app files 目录；
  `MODE_PRIVATE` 覆盖、`MODE_APPEND` 追加，Activity/ContextWrapper 继续委托进程 base Context，
  文件状态只存在于 core `IoRuntime` 与统一 VFS。
- **基础架构**：DVM-92 teardown 及 DVM-94～96 的稳定 linker metadata、`MethodShape`、
  own-member intrinsic 与 owner-state trace/sweep 已完成；Dalvik access flag 与 Java reflection
  modifier mask 已集中到共享头，core 与平台 intrinsics 不再复制直接量。解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。
- **Title 进展**：Tales 已越过 uniform sampler、`GL_OES_mapbuffer`、Context 路径和 thread
  context loader 缺口，两个 native 库完成 JNI 初始化；新首错为
  `android.location.LocationListener`。PvZ 已越过持久化对象图读写，新首错为
  `java.net.URLDecoder`缺失。
  A6/DH exact、长运行 gate 与 threaded 默认裁决尚未闭合。见
  [DVM-47](../tasks/dexvm/DVM-47.md) 和 [WU-0231](../tasks/m5/WU-0231.md)。

## 最近验证

- 2026-09-03 Windows `windows-msvc` Release：默认 Serializable 图、循环/重复引用、枚举、Date、
  FileDescriptor.sync 双后端定向回归通过；PvZ 首次启动写出 690-byte 配置，第二次成功读回，
  对象流异常消失，下一首错推进到 `java.net.URLDecoder` 缺失。
- 2026-09-03 Windows `windows-msvc` Release：Object input/output streams 的 API 19 层级、
  完整继承方法表、双后端 primitive/UTF/header/block-data 与 `null`/`String` 往返定向回归通过；
  后续对象图能力见上一条验证。
- 2026-09-03 Windows `windows-msvc` Release：`ogplay_tests` 构建通过；Context 私有文件流
  覆盖/追加/读取及异常、ContextWrapper 委托、Android catalog 定向回归通过。
- 2026-09-03 Windows `windows-msvc` Release：core 与 Android 平台 intrinsic 中 164 个
  DVM-80 迁移命名空间和 180 个同名转发函数已移除，`ogplay_runtime` 构建通过；按要求未跑测试。
- 2026-09-03 Windows `windows-msvc` Release：共享 access flag 重构后 `ogplay_tests`
  构建通过；builder/reflection/各受影响 Java family 两组定向回归分别 52/52、32/32，
  intrinsic layout 架构检查 1/1 通过。
- 2026-09-03 Windows `windows-msvc` Release：`ogplay_tests` 构建通过；
  Observer/Observable 4/4、相邻 DVM-87/core catalog/collections 8/8、能力账本与架构静态
  检查 4/4 通过；`java_util` 集合族已移除无用途的 DVM-80 转发命名空间。
- 2026-09-03 API 19 系统库、Profile/GUI/CLI、安装 staging、payload 和 scenario runner
  定向验证通过；五库的哈希、ELF、来源及 NOTICE 完整。帧日志降噪按用户要求未另跑测试，
  仅同步既有定向测试契约。

## 下一步

1. 补 PvZ `URLDecoder` 与 Tales `LocationListener`，完成 DH 主菜单 Scenario gate。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate。
3. 出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 可推进确定性 uptime；worker 仅在 clock driver 阻塞时补到 deadline，
  不宣称 guest 时间与 wall clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 仅表示 bounded 设计闭包，不代表完整 Android、联网、
  SQLite 或任意 title 全流程可玩；缺失能力继续记账并明确失败。
- 长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
