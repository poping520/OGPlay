# 当前状态

更新：2026-09-14。

## 最近进展

- 当前源码与本地 AOSP 4.4.4 的[独立完整性复核](../design/boundary/06-gles-api19-completeness-review.md)
  确认核心名称集合 GLES1 145/145、GLES2 142/142、GLES3 delta 104/104、EGL 34/34。
  仍发现 ES3 共用 vertex pointer 类型白名单、Java GLES30 返回值/String[] 桥接缺口；
  EGL 配置、窗口互操作及扩展仅为子集。GLES1 OES FBO 扩展族缺少 15 项 ABI 入口
  及行为桥接，会阻塞依赖这些强符号的应用 SO 加载；报告已列出完整修复与验收范围。
  仅静态审计，未构建、运行测试或变更能力状态。

- [BND-34](../tasks/boundary/BND-34.md) 已修复本轮 EGL/GLES 核心审计缺陷：唯一 native
  Context、独立 Surface、eager share，完整 GLES1 shadow/palette、VAO/整数属性恢复，
  ES3 PBO/row-skip、查询长度与 share-group map 生命周期。新增 KHR/OES image/sync
  及 texture pbuffer，窗口 swap 正确选取 draw 默认 FBO 并恢复 read binding。100 项相关
  图形回归与 7 项生成/架构 gates 通过；未运行全量测试。整体 GLES 完整性仍受
  [ADR-0063](../adr/media.md#adr-0063) 的 Android 系统对象/厂商扩展边界限制，未做 CTS
  或缺失 SO 压缩包的完整 ABI 核验，不能据此宣称“全部 Android GLES 功能已完成”。

- [BND-33](../tasks/boundary/BND-33.md) WU-4 已闭合：新增 API 19 GLES3 相对 GLES2 的
  104 项机器可核对 delta IDL，生成链识别 `GLint64`/`GLuint64`/`GLsync` 与二级指针；
  DexVM 从固定 AOSP 源发布 GLES30 类、常量和 overload surface。ADR-0062 冻结本 WU
  扩展清单为空，并要求 ES3 复用 `libGLESv2.so`、EGL registry 与唯一 Context 状态。
  Native 104 项 handler、A32 宽值/sync identity 及真实 draw/query/error 均已接通。
  后续批次已让 EGL config 宣告 ES3 bit、创建真实 client-version 3 Context，并把 104 项
  delta 发布到 `libGLESv2.so` 与稳定 proc 清单；其中 38 个标量、8 个 name lifecycle、
  29 个单指针 word-array handler，并继续闭合 indexed string、64 位 query、压缩 3D texture
  与 program binary，并接通 sync guest identity 及 buffer-offset draw/attribute。
  A32/Java 64 位实参已按 AAPCS 偶数字槽拆装；真实 VAO、`glGetStringi`、64 位 query 与
  fence create/wait/delete 回归通过。最后 11 项普通 texture3D、多指针 query、
  transform-feedback 字符串和 map-buffer 已闭合；真实 map round-trip 与 3D upload 回归通过。


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
  齐全，但 ES3 共用入口和 Java 方法桥接仍有代码级缺口，不代表全规范实现。
  [此前审计](../design/boundary/05-egl-gles-current-audit.md)中的 Context/Surface 所有权、
  share 顺序及状态隔离问题已有 BND-34 修复记录，不能继续直接作为现存缺陷。
  最新复核仅静态审计，未重新运行构建、图形回归或 CTS。
- 可增长堆的增长、目标利用率、硬上限 OOM 和新 Profile schema 已完成双后端定向验证。
- Title Profile 三个正式文件通过 schema 与独立校验器。
- `architecture.capabilities_monotonic`、`architecture.dexvm_intrinsic_layout` 已通过；其余相关
  门禁在本轮收尾复验。
- `data/android/19/framework/` 是本地生成产物，不纳入版本控制。
