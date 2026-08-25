# 当前状态

更新：2026-08-25 · DVM-89 native 失败保真与 JNI 字段互通

## 当前阶段

- **EGL/GLES API19**：BND-16..24 已冻结 KTU84P ABI，完成 EGL 13 项、GLES1 145 core、
  GLES2 142 core concrete handler 与 PVZ exact ELF 导入复验。关闭 survey 的 bounded run 已
  越过 native load/JNI_OnLoad/OpenGL；该闭包不等于 PVZ 可玩验收。
- **Native Boundary**：BND-1..15 已闭合 metadata catalog、dense transport、typed A32 ABI、
  liblog、AOSP OpenSL ES 与共享 EGL/GLES/PCM 状态；callback/TLS/re-enqueue 有机器门禁。
- **APK Startup**：APS-1..9 已闭合 Manifest/ABI facts、rootless `AndroidGuestProcess`、
  process-lifetime loader/JNI_OnLoad、Application root 与 Profile legacy adapter。A5 exact
  Scenario 三轮确定、无 fault 且 clean shutdown；app ELF 只由 Java `System.load*` 追加。
- **M9 DexVM**：DVM-1..46、48..69 已交付；解释仍由 `VmExecutionLock` 串行。GC-B 已闭合
  全根、精确非移动 STW mark-sweep、复用和水位触发；线程/monitor、FastCode/threaded 双后端、
  类型化 intrinsic、诊断、稳定 identity、ClassLoader 与 bounded reflection foundation 已交付。
  DVM-47 仍受 A6 DT_SONAME identity 与 DH step 漂移阻断，`dexvm.gc` 和
  `dexvm.interpreter_threaded` 保持 `partial`，threaded 生产默认关闭。
- **API 19 能力栈**：DVM-78/79 发布统一集合与 I/O/ZIP runtime；DVM-80 收敛 core/Android
  intrinsic family TU 与 ownership；DVM-81 恢复 Context 类型链；DVM-82 以 `NioRuntime`
  统一 Java/JNI direct buffer；DVM-83 发布 bounded Java GLES façade；DVM-84 让 AudioTrack、
  legacy Java/JNI 与 OpenSL ES 共用唯一 PCM mixer；DVM-85 统一 Clock/Looper/Handler/Timer/
  AsyncTask 调度；DVM-86 发布值类、Parcel/Bundle 与有界系统服务；DVM-87 发布 util algorithm、
  regex、Future/串行 executor/atomic bounded core。
- **DVM-88**：新增 per-VM `NetworkRuntime`，Socket/datagram/TLS 只消费显式
  `NetworkPolicy`/`NetworkTransport`，默认离线且 host/TLS/datagram 分别受检；不直接访问
  宿主 socket、DNS 或证书库。ContentValues/Cursor/SQLiteDatabase/SQLiteOpenHelper 发布
  create/drop/insert/update/delete/query bounded 数据面，确定性内部格式只经 guest VFS
  持久化；完整 SQLite 文件格式、SQL 长尾、ContentProvider/Binder 均 deferred。阶段 catalog
  fixture 同时覆盖 NIO、GLES20、AudioTrack、Socket 与 SQLiteDatabase 链接闭包。
- **验收补强**：DVM-82 direct/heap view 保留 concrete class；DVM-86 Bundle object side-table
  强边进入 GC trace；DVM-88 补齐 SocketFactory 常用创建面、NetworkRuntime teardown close、
  SQLiteHelper schema version 与 `onCreate/onUpgrade` 虚派，并仅将 VFS `Stat` ENOENT 视作新库。
  liblog message/structured field 共用未移动 tag，消除 C++ 参数求值顺序造成的空 tag。
- **DVM-89**：DexVM 出向 native 先独立查询 RegisterNatives 映射，只有未命中才回退
  `Java_` export；已注册目标的 guest CPU/JNI 执行异常保留原始原因，不再被二次误报为
  `native method has no registered mapping or export`。DEX 自有 instance/static 字段发布到
  同一 JNI registry，`GetFieldID` 家族按 AOSP Dalvik 语义先初始化类；JNI primitive、wide、
  object/array 访问直接读写解释器对象槽/linker static storage，引用保持 `VmObjectRef`
  identity 并按调用线程发布 local reference。平台 HLE 字段仍由原 field store 拥有。
  DexVM identity 分配同时跳过已导入的同域 JNI identity，重复身份不再静默注册。
  后续在同一 WU 内按 API 19 AOSP 补齐 `Bitmap.Config` 的四个 enum 常量、
  name/ordinal/nativeInt、`sConfigs`、`$VALUES/values/valueOf` 与 native index 映射；
  `Context.getPackageResourcePath()` 返回只读 guest APK 路径，ContextWrapper 委托 base，
  不泄露 frontend 宿主路径。
  同 WU 的黑屏推进补齐 native 调用锁交接、API19 Bionic 16-bit process TID、timed futex、
  `/proc/meminfo`、长驻 native boundary watchdog、guest 逻辑 RWX/`ARM_cacheflush`、软件
  SurfaceHolder/Canvas 发布以及 AudioTrack output-rate/listener 基础状态。

## 验证基线

- `cmake --preset windows-msvc`：通过。
- `cmake --build --preset windows-msvc --config Debug`：全部目标通过。
- DVM-89 JNI registry/field store/guest field ABI、DexVM class/object/field bridge 定向测试
  10/10：通过；primitive/wide/reference、继承字段 ID、instance/static 双向可见均覆盖。
- 最新 Windows Debug 目标构建通过；graphics/AudioTrack/process native context/watchdog/
  futex/ARM private syscall/memory protection/managed surface 定向 9/9、192 assertions 通过。
  本轮按要求未执行完整测试。此前 994/994 仅为字段互通改动前历史基线。
- Release `ogplay` 定向构建通过。PVZ 原 survey 已越过
  `m_LoaderKeyboard:Lcom/ideaworks3d/marmalade/LoaderKeyboard;`，native 初始化继续并交付
  managed surface callback；补齐 `Bitmap.Config` 与 `getPackageResourcePath()` 后再次复跑，
  `RGB_565` 与 `surfaceChanged` 均已越过；软件 `doDraw()` 已发布首帧，后续 frontend
  循环保持 `presented=1`，原零提交黑屏路径已闭合。约 1.2 万 frontend frame 后出现新的
  S3E title module 低地址写 fault（PC `0x60462edc`、地址 `0x1f84`）；这是当前下一阻断，
  不是 platform mapping/export 二次错误。该 diagnostic run 不作为兼容性结论。

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
