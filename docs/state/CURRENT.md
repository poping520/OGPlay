# 当前状态

更新：补齐首批 java.io.File 路径/对象语义并修正 mkdir

## 当前阶段

- **DVM-79 File 首批续作已完成**：对照 API 19 libcore/core.jar，补齐
  `getName/getParent/getParentFile/isAbsolute/getAbsoluteFile/isHidden`、对象比较/哈希/排序、
  `listRoots/toURI` 与两个 filter interface shape；恢复 File 的接口、bridge、非 final 方法
  shape。相对绝对路径只用 guest VFS 工作目录，`mkdir` 改为单级创建，`mkdirs` 保持递归。
  未新建 WU，记录回原 [DVM-79](../tasks/dexvm/DVM-79.md)。

- **DVM-94～96 已完成**：linker metadata 改为追加地址稳定存储；调用解析按
  `InvokeKind` 隔离并消费链接期 `MethodShape`。Intrinsic declaration 只含 own members，
  `ContextWrapper` 用显式 override 委托 `mBase`，Application/Activity/Service 直接继承。
  Android 4.4.4 protected callbacks 已校准，真实 DEX protected override 受门禁保护。
  新增 `OwnedStateTable` 与 Android owner-state trace/sweep，删除 map-key 兼容根。
  双后端共用 invoke target 和参数/返回验证，默认后端与
  profile 不变。见 [DVM-94](../tasks/dexvm/DVM-94.md)、[DVM-95](../tasks/dexvm/DVM-95.md)、
  [DVM-96](../tasks/dexvm/DVM-96.md)、ADR-0028/0029。

- **A6 AudioTrack 声音根因已修复**：legacy 与 DexVM MODE_STREAM 统一按构造
  `buffer_size` 的未消费 PCM 字节阻塞，播放消费唤醒；legacy park 不持 media mutex，
  DexVM park 释放执行锁，release/Stop/BeginTeardown 可中断。255 项队列仅保留为内存护栏，
  饱和不再抛越过 Java 边界的 C++ 异常或返回 0。定向饱和/恢复、锁释放、teardown 与既有
  callback 回归通过，A6 用户实听确认 BGM 与音效正常。见 [DVM-93](../tasks/dexvm/DVM-93.md) 与
  [ADR-0027](../adr/0027-audiotrack-stream-byte-backpressure.md)。
- **A6 RGBA8 RenderTarget 首错已修复**：混合链接 guest 的 GLES1 扩展投影补入共享
  ANGLE context 真实支持的 `GL_OES_rgb8_rgba8`，并以尾随分隔符兼容旧 token 解析器；
  机器测试验证真实 RGBA8 renderbuffer/FBO。Release exact 手动步进点击“触摸继续”后进入
  主菜单，到 frame 10932、draw 64991 仍无 guest fault，越过原
  `libasphalt6.so+0x7f2c14`。shutdown 在 `teardown.guest_callbacks` 未完成，作为独立
  生命周期问题保留。见 [BND-26](../tasks/boundary/BND-26.md)。
- **Tales Context 首错已修复**：uniform reflection 接受 API19
  `GL_SAMPLER_3D_OES` 单值 shape；GLES1 extension 目录和 boundary 真实实现
  `GL_OES_mapbuffer` 三入口。Context 新增 `getPackageCodePath/getCacheDir/getApplicationInfo`；
  code/resource/sourceDir 共用只读 guest APK，cache 经 VFS 建目录，ApplicationInfo 为进程
  稳定身份且 wrapper 仅委托 base。Tales 关闭 survey 越过原首错并完成 Amazon JNI 加载，
  新首错为 `getContextClassLoader()`。见
  [DVM-47](../tasks/dexvm/DVM-47.md)、[WU-0231](../tasks/m5/WU-0231.md)。
- **DVM-92 已完成并通过 title 验收**：退出首个 guest 回调前单向退役 Java EGL、
  native/managed GLES 与 EGL swap；process `BeginTeardown` 发布独立取消并中断
  blocking wait，join 前再次中断覆盖回调中新建 futex。renewable JNI native frame
  在既有 slice/boundary 安全点失败展开，运行期 ADR-0023 预算与 non-renewable
  finalizer 不变。契约见 [DVM-92](../tasks/dexvm/DVM-92.md) 与
  [ADR-0025](../adr/0025-teardown-cancellation-and-graphics-retirement.md)。
- **近期兼容性闭合**：Android View fallback 已支持 reverse-Z、deepest-first 触摸路由；
  BND-27 修复 GLES1 coordinate array 来源；DVM-91 完成 FileDescriptor/PFD/AFD 媒体
  区间能力；DVM-90 完成动态 SurfaceView holder generation。
- **仍未闭合**：DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决；解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 最近验证

- 2026-08-30 macOS dev `ogplay_tests` 重建通过；File 首批声明、双后端路径/对象/URI、
  单级/递归建目录及既有 File 回归 12/12 通过。
- 2026-08-30 macOS dev `ogplay_tests` 重建通过；Context/PackageManager/VFS 与 architecture
  定向 12/12 通过。Tales 关闭 survey 实跑越过 package code path，下一缺口为
  `getContextClassLoader()`。
- 2026-08-29 macOS dev 受影响目标通过；GLES1 扩展 token 与真实 RGBA8 FBO 定向
  2/2 通过（119 assertions），相关 architecture 4/4 通过。A6 Release 手动步进进入
  主菜单并稳定到 frame 10932，无 guest fault。
- 2026-08-28 macOS `dev` 全量 CTest 1066/1066 通过（约 136 s，unit 1032 + tools 25 等）。
- DH Release 越过 license 轮询并稳定到主菜单，240 帧持续 presented，Ctrl-C 干净停止；
  PVZ Release 已进入标题画面、可输入并提交用户名。

## 下一步

1. 补 Tales `getContextClassLoader()` 能力；完成 DH 主菜单 Scenario gate。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate。
3. 首次出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 可推进确定性 uptime；worker 仅在 clock driver 已阻塞时补到
  deadline。生命周期内 guest 时间不再严格等于 16 ms×帧序，不宣称 wall-clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 Android KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 只表示 bounded 设计阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md) ·
[Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
