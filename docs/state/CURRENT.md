# 当前状态

更新：MediaPlayer listener/lifecycle 窄方法与 tick 预算默认值调整

## 当前阶段

- **DVM-84 追加**：补齐 `MediaPlayer.setOnErrorListener/setOnPreparedListener/reset`
  窄方法（对照 AOSP API19 签名；listener 接受即返回、回调为记录 gap，`reset` 停止
  mixer 播放并保留 `create()` 的 resid 绑定）。移除 pvz 主菜单音效路径的
  "method cannot be resolved" 阻断；pvz 实测推进至 `f≈7101`，下一缺口确认为
  `Landroid/os/ParcelFileDescriptor;` 类缺失——FD 系数据源管道
  （`ParcelFileDescriptor/FileInputStream.getFD/AssetFileDescriptor/
  setDataSource(FileDescriptor,J,J)`）为下一独立工作单元。
- **tick 预算**：`maximum_ticks_per_call` 默认值 2×10⁸ → 1×10¹⁰（`title_profile.h`
  的 run-apk 生效路径与 session 级默认共三处）。依据：pvz 主菜单 alpha 预乘与
  reanim 编译段为合法重计算（原阈值被击穿），Gameloft profile 已用 1×10¹⁰；误杀
  成本高于迟检成本。看门狗语义不变（无边界调用连续计算计）。
- **BND-26**：GLES1 固定管线落地 `GL_OES_texture_cube_map`。绑定/参数/mipmap 接受
  0x8513 并按 unit 记 capability；上传 face target 经共享层归一到 cube 对象（顺带修复
  `SharedGlState` metadata 键未归一的隐藏缺陷）；fixed program 按 stage 采样目标选择
  `samplerCube`/vec3 texcoord 变体惰性编译，未启用 stage sampler 挂无绑定 unit（修复同
  unit 混用采样器类型的 `INVALID_OPERATION`）。追加轮：GLES1 `glGetString` 的
  VERSION/EXTENSIONS 合成 ES1 固定管线语义（不再透传 ES3 后端串）；guest 非法枚举经
  `SharedGlState` per-context 锁存转 `GL_INVALID_ENUM` 由 `glGetError` 消费，宿主契约
  破坏仍显式失败。KitKat libagl 无 cube，真机语义来自硬件 GLES1 驱动。
- **EGL/GLES API19**：BND-16..24 已冻结 KTU84P ABI，完成 EGL 13 项、GLES1 145 core、
  GLES2 142 core handler 与 exact ELF 导入；bounded run 已越过 OpenGL。
- **Native Boundary**：BND-1..15 已闭合 metadata catalog、dense transport、typed A32 ABI、
  liblog、AOSP OpenSL ES 与共享 EGL/GLES/PCM 状态；callback/TLS/re-enqueue 有机器门禁。
- **BND-25**：`libdl.so` 四入口已接 process ELF namespace/Virtual SO；保持 root scope、
  handle 引用计数、`RTLD_DEFAULT` 和逐线程错误，不访问宿主动态库或引入 title 分支。
  2026-08-26 追加：`dlopen(nullptr)` 返回 `RTLD_DEFAULT` 伪句柄（`dlclose` no-op），
  `dlsym(RTLD_DEFAULT)` 与 boundary 句柄 miss 均经 `LookupAny` 跨 sealed 模块解析
  （后者有意宽于 KitKat 单库语义），`libhgl.so` 归一化为 `libGLESv2.so` 边界 handle；
  pvz-tmp 问题 3 无可观测影响、问题 4 另行记账。
- **APK Startup**：APS-1..9 已闭合 Manifest/ABI facts、rootless process、process-lifetime
  loader/JNI_OnLoad 与 legacy adapter；app ELF 只由 Java `System.load*` 追加。
- **M9 DexVM**：DVM-1..46、48..69 已交付；解释仍由 `VmExecutionLock` 串行。GC-B、线程/
  monitor、双后端、typed intrinsic、identity、ClassLoader 与 bounded reflection 已交付。
  DVM-47 仍受 A6/DH 阻断，GC/threaded 保持 `partial`，threaded 生产默认关闭。
- **API 19 能力栈**：DVM-78..87 已闭合集合、I/O/ZIP、Context、NIO、Java GLES、统一 PCM、
  scheduler、值类/Parcel/Bundle、util/regex/Future/atomic 的 bounded core。
- **DVM-88**：新增 per-VM `NetworkRuntime`，Socket/datagram/TLS 只消费显式
  `NetworkPolicy`/`NetworkTransport`，默认离线且 host/TLS/datagram 分别受检；不直接访问
  宿主 socket、DNS 或证书库。ContentValues/Cursor/SQLiteDatabase/SQLiteOpenHelper 发布
  create/drop/insert/update/delete/query bounded 数据面，确定性内部格式只经 guest VFS
  持久化；完整 SQLite 文件格式、SQL 长尾、ContentProvider/Binder 均 deferred。阶段 catalog
  fixture 同时覆盖 NIO、GLES20、AudioTrack、Socket 与 SQLiteDatabase 链接闭包。
- **验收补强**：NIO view concrete class、Bundle GC strong edge、Network teardown、
  SQLiteHelper schema lifecycle 与 liblog tag 求值顺序问题均已闭合。
- **DVM-89**：RegisterNatives 解析/执行失败保真、JNI↔DEX instance/static 字段统一存储与
  identity/class-init/local-ref 已闭合；同 WU 补齐 Bitmap.Config、guest APK resource path、
  bounded KeyEvent/ResultReceiver、软件 Surface/Canvas、native 锁/TID/futex/watchdog/RWX 及
  AudioTrack 启动面，平台 HLE 字段仍由原 store 拥有。
- **DVM-90**：对照 API 19 ViewGroup/SurfaceView，把 live UiTree attach 等价为 AttachInfo，
  为每个 holder 记录 active generation。动态 add 的 SurfaceView 子树仅在 parent live 时收到
  created→changed，remove/setContentView replacement 在 detach 前收到一次 destroyed；初始
  lifecycle 不再广播给 detached holder。callback 使用稳定快照并发布幂等 removeCallback；
  host window surface open/close 只归 Activity lifecycle，子树 detach 不改变该事实；
  `getHolder()` 只建稳定 identity，activation 在 live attach 时统一判断。visibility/format/size
  重建、独立合成层与完整 WindowManager 仍 deferred。
- **DVM-90 可诊断性**：已解析 RegisterNatives 目标失败携带 class.method descriptor 与
  guest process thread；DexVM child 失败携带 Java name+record id。A32 abnormal stop 统一输出
  具名+数值原因、固定宽度十六进制 PC/fault/寄存器与有界指令窗口；`run-apk` 外层标签不再
  把任意 guest fault 误称为 GLSurfaceView profile 失败。

## 验证基线

- 历史 Windows Debug 全目标构建通过；994/994 是字段互通前基线，不作为本轮结果。
- DVM-89 JNI field bridge 定向 10/10；Android/native support 定向 9/9、192 assertions；
  KeyEvent 定向源 20/20、567 assertions，均通过。
- DVM-90 Windows Debug `ogplay`/`ogplay_tests` 增量构建通过；SurfaceHolder/ViewGroup
  定向 4/4、183 assertions 与 architecture 6/6 通过，本轮按要求不执行完整测试。
- 报告所述 distinct remove→new/getHolder/add 已补精确回归：1/1、90 assertions 通过，旧
  holder destroyed、新 holder created→changed、host surface 始终 open；三代相同 lifecycle
  日志禁用自动限流后均可见。临时运行追踪确认第二 holder 与 EGL/getGL 实际成功，此前缺少
  第二组日志是限流假象。`getGL()` 返回后的 Thread-2 NULL AddRef 属独立问题，仅记录未处理。
- 本次 Windows Release `ogplay`/`ogplay_tests` 增量构建、SurfaceHolder 定向 3/3 与
  architecture 6/6 通过；按要求未执行全量测试。
- 可诊断性/排版定向 7/7、52 assertions、architecture 6/6，Debug tests 与 Release CLI 增量
  构建通过。PVZ 两次实跑均输出 `Thread-2 (id 2)`、`LoaderThread.runNative(...)V`、
  guest thread 16384、
  `pc=0x6045be18 thumb memory_fault(4) NULL read/unmapped`、寄存器及指令窗口；text 输出按
  Java→JNI→A32 stop/fault/thread/registers/code 分层对齐，并使用通用失败标签。该运行只
  验收诊断链，不宣告 title 已进入。
- libdl 定向 5/5、60 assertions 通过；本轮未执行全量测试。
- 2026-08-26 BND-25 追加两轮：Windows Debug/Release 构建通过；新增 libdl 全进程语义
  与 boundary 回退定向 3 用例 46 assertions 通过，`Bionic*` 14/14、GLES1 state 13/13、
  存量 libdl 无回归，按要求未执行全量测试。pvz 越过 `0x6045be18` NULL AddRef 崩溃点，
  剩余阻断为 GLES1 收到 `GL_TEXTURE_CUBE_MAP`（0x8513）触发仅支持 2D 的致命校验。
- 2026-08-27 BND-26：Windows Debug/Release 构建通过；新增 cube 采样边界级用例
  （62 assertions，含 VERSION/扩展串与错误锁存断言）与状态层 cube 断言，
  `*GLES1*/*GLES2*/*texture*/*boundary*` 46 用例 10542 assertions 无回归，10 处既有
  THROW 断言按锁存契约改写，按要求未执行全量测试。pvz 越过 0x8513 阻断，guest 日志
  到达 `LoadTask::MAIN_MENU`，新停止点为 `f≈4447` 的 `A32 guest call exhausted its
  tick budget`（consumed≈4.48e9，独立问题）。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前启动阻断，复验 DVM-47 和 threaded title gate。
2. 执行 Linux M9 严格出口复验；后续 framework 长尾仅按关闭 survey 的 reached gap 排序。
3. PVZ 下一阻断：`LoaderThread.runNative` 在主菜单加载期耗尽 A32 tick 预算
   （consumed≈4.48e9，pc 在 s3e 代码区），需判定长加载循环还是自旋等待；pvz-tmp
   问题 3 已判定无可观测影响，问题 4 为独立契约债。

## 阻塞与边界

- A6 主界面/可玩 gate 尚未完成；DVM-47 的 A6/DH exact 与长运行门禁未全部成立。
- `dexvm.api19_capability_stack=complete` 仅表示设计文档定义的 bounded 阶段闭包，不表示完整
  Android framework、真实联网、完整 SQLite 或任何 title 的可玩性。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
