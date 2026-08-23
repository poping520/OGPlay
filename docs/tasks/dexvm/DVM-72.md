# DVM-72 · Guest 目录与 APK asset 闭包

## 目标（一句话）

发布稳定的 guest `/data` 与 app-private files directory，并以 APK metadata 闭合
logical AssetFileDescriptor 的 length/list/close 面，越过当前启动链阻断。

## 依赖

- DVM-71 的关闭-survey 第一故障
- AOSP `android-4.4.4_r2.0.1` `Environment.java` / `Context.java` / `ContextImpl.java`
  / `AssetManager` / `AssetFileDescriptor`
- 既有 package identity、`java.io.File` intrinsic、统一 guest VFS/sandbox 与
  loader strict `ApkArchive` central-directory metadata

## 语义边界

- `Environment.getDataDirectory()` 返回 session 内稳定 `java.io.File` identity，path
  为 API19 默认 `ANDROID_DATA=/data`；只发布 guest-visible path，不读取 host 环境变量，
  不暴露 sandbox host root，实际 `File` 查询和 I/O 继续只经过统一 VFS；
- `getFilesDir()` 声明在 `android.content.Context`，Activity 沿 class hierarchy 继承；
  path 精确为 `/data/data/<package>/files`，首次调用经 VFS 创建缺失目录层级，创建失败
  返回 null，不返回 host sandbox 路径；
- OGPlay 的 APK 是虚拟 archive，不拥有可交给 guest 的真实 POSIX descriptor；
  `openFd(String)` 表示 immutable logical asset extent，按精确 entry name 查询 sealed
  archive metadata，`getLength()` 返回 central directory 的 uncompressed size（支持
  64-bit），entry 缺失抛 `FileNotFoundException`；
- `list(String)` 从 sealed APK entry directory 推导 direct child basename，匹配区分
  大小写、排序且去重，不递归返回 descendant；空或不存在目录返回非 null 空 `String[]`；
- logical descriptor 不拥有 host/guest fd、stream、offset 或 lease，`close()` 是幂等的
  空资源释放且不改写 immutable length；不发布假的 FileDescriptor/offset/
  ParcelFileDescriptor 或未触达 stream API，也不解压整个 entry 只为读取长度。

## 验收

- [x] `Environment.getDataDirectory()` 返回 session 内稳定 `File("/data")` identity，
      不读取 host 环境变量，不暴露 sandbox host root；
- [x] Context 层发布 `/data/data/<package>/files`，API 声明在 `android.content.Context`
      沿继承解析，首次调用经 VFS 真实建立目录层级，创建失败返回 null；
- [x] `AssetManager.openFd(String)` 按精确 entry name 查询 sealed archive metadata，
      `getLength()` 返回 central directory uncompressed size 且支持 64-bit，缺失抛
      `FileNotFoundException`；
- [x] `AssetManager.list(String)` 返回 case-sensitive、排序去重的 direct-child
      `String[]`，missing path 返回非 null 空数组；
- [x] logical descriptor `close()` 幂等成功且不改写 immutable length；
- [x] focused Environment/Context/File/VFS/AssetManager tests 通过；
- [x] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [x] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

验收：focused 6/6、8/8、9/9、10/10、2/2。exact run 关闭 survey，frame limit 3、
wall limit 45 s，依次越过 data/files directory、`openFd→getLength`、asset listing
与 logical AFD close fault；新首 fault 为
`android.opengl.GLSurfaceView$EGLContextFactory` class hierarchy 缺失。结束后无
`ogplay` 进程。

状态：完成。
