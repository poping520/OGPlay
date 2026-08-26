# 当前状态

更新：2026-08-26 · DVM-89 bounded KeyEvent dispatch

## 当前阶段

- **EGL/GLES API19**：BND-16..24 已冻结 KTU84P ABI，完成 EGL 13 项、GLES1 145 core、
  GLES2 142 core handler 与 exact ELF 导入；bounded run 已越过 OpenGL。
- **Native Boundary**：BND-1..15 已闭合 metadata catalog、dense transport、typed A32 ABI、
  liblog、AOSP OpenSL ES 与共享 EGL/GLES/PCM 状态；callback/TLS/re-enqueue 有机器门禁。
- **BND-25**：`libdl.so` 四入口已接 process ELF namespace/Virtual SO；保持 root scope、
  handle 引用计数、`RTLD_DEFAULT` 和逐线程错误，不访问宿主动态库或引入 title 分支。
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
- **DVM-89**：DexVM 出向 native 先独立查询 RegisterNatives 映射，只有未命中才回退
  `Java_` export；已注册目标的 guest CPU/JNI 执行异常保留原始原因，不再被二次误报为
  `native method has no registered mapping or export`。DEX 自有 instance/static 字段发布到
  同一 JNI registry，`GetFieldID` 家族按 AOSP Dalvik 语义先初始化类；JNI primitive、wide、
  object/array 访问直接读写解释器对象槽/linker static storage，引用保持 `VmObjectRef`
  identity 并按调用线程发布 local reference。平台 HLE 字段仍由原 field store 拥有。
  DexVM identity 分配同时跳过已导入的同域 JNI identity，重复身份不再静默注册。
  同 WU 还按 API 19 补齐 `Bitmap.Config` enum 与 native index 映射；
  `Context.getPackageResourcePath()` 返回只读 guest APK 路径，ContextWrapper 委托 base，
  不泄露宿主路径。`OnFocusChangeListener` 可正常链接分派；`ResultReceiver` 有限闭合本地
  同步/异步 send，Binder Parcel 明确拒绝。session 键输入传递有状态的非空 `KeyEvent`，
  `View.dispatchKeyEvent` 虚派 DOWN/UP/MULTIPLE；字符、监听与 tracking 仍 deferred。
  同 WU 的黑屏推进补齐 native 调用锁交接、API19 Bionic 16-bit process TID、timed futex、
  `/proc/meminfo`、长驻 native boundary watchdog、guest 逻辑 RWX/`ARM_cacheflush`、软件
  SurfaceHolder/Canvas 发布以及 AudioTrack output-rate/listener 基础状态。

## 验证基线

- `cmake --preset windows-msvc` 与历史 Debug 全目标构建：通过。
- DVM-89 JNI registry/field store/guest field ABI、DexVM class/object/field bridge 定向测试
  10/10：通过；primitive/wide/reference、继承字段 ID、instance/static 双向可见均覆盖。
- Windows Debug 定向构建通过；graphics/AudioTrack/process native context/watchdog/
  futex/ARM private syscall/memory protection/managed surface 定向 9/9、192 assertions 通过。
  本轮按要求未执行完整测试。此前 994/994 仅为字段互通改动前历史基线。
- `KeyEvent`/`View.dispatchKeyEvent` 定向源 20/20、567 assertions 通过；未执行完整测试。
- libdl process service 的 bridge/error、Bionic route/relocation 与 libc override 定向
  5/5、60 assertions 通过；Windows Release `ogplay` 与 Debug `ogplay_tests` 增量构建通过。
- Release `ogplay` 定向构建通过。PVZ 原 survey 已越过
  `m_LoaderKeyboard:Lcom/ideaworks3d/marmalade/LoaderKeyboard;`，native 初始化继续并交付
  managed surface callback；补齐 `Bitmap.Config` 与 `getPackageResourcePath()` 后再次复跑，
  `RGB_565` 与 `surfaceChanged` 均已越过；软件 `doDraw()` 已发布首帧，后续 frontend
  循环保持 `presented=1`。IDA 与 JNI callback 追踪把后续低地址写定位为 GLES 动态探测
  失败的次生结果：旧路径未调用 `LoaderThread.glInit(I)V`；补齐 libdl service 后约 f=2400
  已收到该回调，并持续到约 2 万 frame 未再出现 PC `0x60462edc`/地址 `0x1f84` fault。
  该 diagnostic run 仅证明本阻断推进，不作为完整兼容性结论。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前启动阻断，复验 DVM-47 和 threaded title gate。
2. 执行 Linux M9 严格出口复验；后续 framework 长尾仅按关闭 survey 的 reached gap 排序。

## 阻塞与边界

- A6 主界面/可玩 gate 尚未完成；DVM-47 的 A6/DH exact 与长运行门禁未全部成立。
- `dexvm.api19_capability_stack=complete` 仅表示设计文档定义的 bounded 阶段闭包，不表示完整
  Android framework、真实联网、完整 SQLite 或任何 title 的可玩性。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
