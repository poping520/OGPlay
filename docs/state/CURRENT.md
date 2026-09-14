# 当前状态

更新：2026-09-14。

## 最近进展

- Native EGL 已从固定 context=4/surface=3 升级为对象表：Context/Window/Pbuffer 使用独立
  identity，校验初始化、config、client version、对象类型和线程占用，支持 share-root 记账与
  current 对象延迟销毁；ChooseConfig/GetConfigAttrib 返回 RGBA8+D24S8、window+pbuffer、
  ES1/ES2 的真实闭集，pbuffer 查询尺寸与实际 ANGLE attachment 一致。宿主 GL execution lane
  仍串行，跨线程抢占明确返回 EGL_BAD_ACCESS。
- 完成 Android 4.4.4 GLES 静态完整性审计：ROM 动态符号覆盖 EGL 25/48、GLES1
  158/292、GLES2 142/367；核心入口闭集不等于规范语义完整。多 Context/共享、ES3、
  EGL 生命周期与部分 fixed pipeline 仍有缺口，发现 GLES1 版本字符串枚举误用；本轮未改代码或运行图形测试。
- Application `meta-data` 已对齐 API 19 `PackageParser`：`android:value` 资源引用经 ARSC
  解析后按 String/Boolean/Integer 写入 Bundle，`android:resource` 独立保留资源 ID；PvZ 的
  Nimble verification 字符串不再被误装为 Integer。
- DexVM 现提供 API 19 `java.home`、`java.io.tmpdir`、`user.dir` 初始 property；Android
  bridge 在 Java 执行前以 guest VFS working directory 覆盖 `user.dir`。真实 PvZ Terms/
  Restlet 启动已不再触发 `File.join` null receiver，并继续进入离线 HTTP 失败路径。
- Libcore `Posix.mkdir/remove/rename` 现保留 VFS 真实 errno，不再把父目录缺失误报为 EEXIST；
  API 19 `File.mkdirs()` 可按 ENOENT 递归，真实 PvZ 已越过 Nimble 必需目录创建。
- DexVM 已按 ADR-0060 从固定 64 MiB 预算切换为可增长堆：默认 64 MiB 初始目标、
  512 MiB growth limit、1 GiB maximum，按普通 GC→增长→before-OOM GC→OOM 执行。
  GC 后依 live set、75% 利用率与 2..8 MiB 空闲区间调整目标；intrinsic 在安全点之间可在
  growth limit 内增长。Profile 只接受 `[runtime.dexvm.heap]`，旧字段明确拒绝。
- 无 Profile 的真实 PvZ 已越过两份约 52 MiB 数组形成的约 104 MiB 峰值，不再触发固定预算
  OOM。API 19 `System.lineSeparator` 现从初始 property 冻结，后续 property 修改不改变返回值；
  真实 PvZ 已越过 `Properties.store`、切换到 PvZActivity、加载三份 native 库并进入主循环。
  人工停止时暴露既有独立 teardown 缺口：`JNI monitor thread is not a DexVM thread`。
- API 19 `Context.getObbDir(s)` 已按 `/sdcard/Android/obb/<package>` 接入 VFS overlay，
  ContextWrapper 仅委托 base；双后端覆盖路径、目录创建、稳定 File 身份及 unavailable null。
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

- 可增长堆的增长、目标利用率、硬上限 OOM 和新 Profile schema 已完成双后端定向验证。
- Title Profile 三个正式文件通过 schema 与独立校验器。
- `architecture.capabilities_monotonic`、`architecture.dexvm_intrinsic_layout` 已通过；其余相关
  门禁在本轮收尾复验。
- `data/android/19/framework/` 是本地生成产物，不纳入版本控制。
