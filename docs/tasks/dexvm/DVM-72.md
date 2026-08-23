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

## 验收

- [ ] `Environment.getDataDirectory()` 返回 session 内稳定 `File("/data")` identity，
      不读取 host 环境变量，不暴露 sandbox host root；
- [ ] Context 层发布 `/data/data/<package>/files`，API 声明在 `android.content.Context`
      沿继承解析，首次调用经 VFS 真实建立目录层级，创建失败返回 null；
- [ ] `AssetManager.openFd(String)` 按精确 entry name 查询 sealed archive metadata，
      `getLength()` 返回 central directory uncompressed size 且支持 64-bit，缺失抛
      `FileNotFoundException`；
- [ ] `AssetManager.list(String)` 返回 case-sensitive、排序去重的 direct-child
      `String[]`，missing path 返回非 null 空数组；
- [ ] logical descriptor `close()` 幂等成功且不改写 immutable length；
- [ ] focused Environment/Context/File/VFS/AssetManager tests 通过；
- [ ] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [ ] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

状态：设计完成，待实现。
