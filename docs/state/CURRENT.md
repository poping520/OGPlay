# 当前状态

更新：2026-09-14。

## 最近进展

- [DIAG-3](../tasks/diagnostics/DIAG-3.md) 已补齐 native fatal 终止链：
  `tgkill(SIGABRT)` 保存 signal/target/PC/LR 并按默认 action 终止进程组；fd 1/2 与
  `/dev/log/*` 的 `write/writev` 进入结构化 guest 日志；退出报告从 r11 有界展开 A32
  FP/LR 链。真实 APK 已直接显示 `io::IOException`、recursive terminate、SIGABRT 及
  6 层有效 guest 帧，证明此前 `exit_group(1)` 只是 abort 的后备终止。一般 signal handler
  投递仍不在本能力内。

- [DIAG-3](../tasks/diagnostics/DIAG-3.md) 已把 A32 native 调用中的 guest 退出从
  固定短句升级为可追溯现场：lifecycle 保留 host/exit/exit_group、requester、退出码和
  syscall PC/LR；即时错误同时输出 Java native 方法/context、调用 target/tick、最后 stop、
  核心寄存器及可读指令窗口。退出行为不变，生产代码无 title 分支。

- [BND-35](../tasks/boundary/BND-35.md) 已闭合 GLES 完整性复核中的可执行反例：ES3
  共用 vertex pointer 类型范围、Java GLES30 indexed String/direct Buffer/sync long/String[]
  桥接及 GLES20 shading-language string；GLES1 新增 GL_OES_framebuffer_object 全部 15 个
  独立 ABI/thunk/handler，并与扩展字符串一致。EGL native window/BufferQueue、设备 config
  集合和未选扩展仍受 ADR-0063 边界限制，不宣称完整 Android 设备 GLES。

- 当前源码与本地 AOSP 4.4.4 的[独立完整性复核](../design/boundary/06-gles-api19-completeness-review.md)
  确认核心名称集合 GLES1 145/145、GLES2 142/142、GLES3 delta 104/104、EGL 34/34。
  原审计发现的 ES3 共用 vertex pointer、Java GLES30 返回值/String[] 与 GLES1 OES FBO
  15 项 ABI/行为缺口已由 BND-35 修复。EGL 设备 config 全集、ANativeWindow/BufferQueue
  和 Android native-buffer/fence 属于 ADR-0063 排除范围，不再记为待修复缺陷。

- [BND-34](../tasks/boundary/BND-34.md) 已修复本轮 EGL/GLES 核心审计缺陷：唯一 native
  Context、独立 Surface、eager share，完整 GLES1 shadow/palette、VAO/整数属性恢复，
  ES3 PBO/row-skip、查询长度与 share-group map 生命周期。新增 KHR/OES image/sync
  及 texture pbuffer，窗口 swap 正确选取 draw 默认 FBO 并恢复 read binding。100 项相关
  图形回归与 7 项生成/架构 gates 通过；未运行全量测试。整体 GLES 完整性仍受
  [ADR-0063](../adr/media.md#adr-0063) 的 Android 系统对象/厂商扩展边界限制，未做 CTS
  或缺失 SO 压缩包的完整 ABI 核验，不能据此宣称“全部 Android GLES 功能已完成”。

- [DVM-154](../tasks/dexvm/DVM-154.md) 已把 File/FIS/FOS/FileReader/FileWriter/RAF、channel、
  FileChannelImpl/NioUtils、IoBridge/IoUtils/CloseGuard 普通方法迁入 1503 类 API 19 BootDex。
  Posix 文件子集经唯一 OpenFileDescription 接入 VFS；FileChannelImpl 仅保留 bounded
  file-to-file transfer 边界，通用 mmap/锁/socket 长尾明确失败。
- API 19 XML style、Terms UI、Context permission、隐式 Activity 切换、LogManager resource、
  Runtime shutdown、URI/InetAddress/builder BootDex 迁移均已越过对应 PvZ 首错；当前不宣称
  游戏完整启动或在线服务可用。

## 当前能力

- **发行与 VM**：API 19 使用固定 manifest、BootDex、guest ICU/OpenSSL/Bionic；普通 Java
  状态归字段/数组，JNI 使用 VM 真实类型关系。DexVM 为精确非移动 STW mark-sweep，堆目标
  可增长但仍受明确上限约束。
- **Java 与 IO**：常用集合、并发、IO、序列化、反射、日期、格式化及部分 JCA/ICU 已覆盖；
  Java IO 普通方法走 BootDex，Libcore Posix native 是 VFS 唯一文件入口。TLS、系统 CA、完整
  FileChannel mmap/lock、网络和 Java 平台长尾仍明确失败。
- **Android 边界**：覆盖当前 APK 的 Context/Intent/Activity/PackageManager、资源 XML、
  文件/VFS、SQLite、SharedPreferences、Locale、有限服务缺席语义及稳定沙盒身份。不运行
  Binder system_server、Play 服务、支付、跨包 resolver 或完整 Android 系统。
- **图形与 UI**：ANGLE GLES、SDL3 窗口输入、View hierarchy、常用布局、文本、drawable、
  Surface/Canvas 与有限媒体路径已接通。完整 framework 排版、Dialog/Web presentation、传感器
  和系统 UI 不在当前能力范围。

## 验证状态

- [GLES 最新完整性复核](../design/boundary/06-gles-api19-completeness-review.md)：核心名称
  齐全，原 ES3 共用入口、Java 方法桥接及 GLES1 OES FBO 缺口已由 BND-35 修复；
  catalog complete 仍不代表 CTS/Khronos 全规范认证。
  [此前审计](../design/boundary/05-egl-gles-current-audit.md)中的 Context/Surface 所有权、
  share 顺序及状态隔离问题已有 BND-34 修复记录，不能继续直接作为现存缺陷。
  最新复核仅静态审计，未重新运行构建、图形回归或 CTS。
- 可增长堆的增长、目标利用率、硬上限 OOM 和新 Profile schema 已完成双后端定向验证。
- Title Profile 三个正式文件通过 schema 与独立校验器。
- `architecture.capabilities_monotonic`、`architecture.dexvm_intrinsic_layout` 已通过；其余相关
  门禁在本轮收尾复验。
- `data/android/19/framework/` 是本地生成产物，不纳入版本控制。
