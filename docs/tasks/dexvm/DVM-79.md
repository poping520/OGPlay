# DVM-79 · Java IO 核心化与统一流运行时

## 目标（一句话）

把 `java.io`/`java.util.zip` 类声明、流与 ZIP 状态、文件语义从 Android integration
下沉到 DexVM core，按语义家族合并 handle TU，并仅通过注入的 guest VFS 暴露文件事实。

## 依赖

- DVM-49：具名 intrinsic side-table sweep hooks。
- DVM-78：core family TU 与 per-VM runtime state 模式。
- ADR-0020：Java/native I/O 共用唯一 VFS/sandbox 世界。
- pinned libcore：`.local/aosp/libcore/luni/src/main/java/java/io/`。

## 范围

- `IoRuntime` 统一拥有 input/output bytes、cursor、close 与 wrapper adoption 状态。
- stream/reader/data/byte-array/filter/buffer handle 聚合到 `java_io_streams.cpp`。
- File/FileInputStream/FileOutputStream/FileReader/FileWriter 聚合到
  `java_io_files.cpp`。
- File 系列只调用 core `IoFileSystem` 窄接口；Android session 通过 VFS adapter 注入。
- `ZipInputStream` 改为通过 `IoRuntime` 接管源流，不读取 Android context 流表。
- `ZipEntry`/`ZipInputStream` 聚合到 core `java_util_zip.cpp`，ZIP archive/entry/cursor
  状态由 per-VM `ZipRuntime` 持有并参与 GC sweep。
- 删除 `DexVmAndroidContext::streams/output_streams`、旧 session root 和分散 handle TU。

## 不做

- 完整 Java serialization、charset/locale、FileDescriptor/RandomAccessFile/NIO。
- 宿主文件系统直通或 Android Binder/storage service。
- 借迁移扩大已有 bounded API surface；未实现能力继续明确失败。

## 验收

- isolated CoreIntrinsicCatalog 可使用 byte-array/data/buffer 流，不需要 Android catalog。
- 注入 VFS 后 File I/O 与 native VFS 仍共享同一内容；无 VFS 明确失败。
- core-only ZipInputStream 经 `IoRuntime` 读取源流；GC sweep 不保活死亡 ZIP owner。
- Windows Debug 构建、双后端 focused、file/VFS/ZIP 回归与 architecture gate 通过。

## 结果

- 20 个分散 Android `java_io_*.cpp` 已删除，15 个 stream handle 与 5 个 file handle
  分别收敛到两个 core family TU，API shape 保持不变。
- `IoRuntime` 接管 per-VM input/output 状态、VFS 文件事实与 GC sweep；Android context
  不再保存 streams/output_streams，ZipInputStream 只通过该 runtime 接管输入。
- core-only 双后端 data stream、文件/VFS、ZIP adoption、双 close、catalog ownership
  与 GC 清扫均有机器测试。
- `ZipEntry`/`ZipInputStream` 已从 Android catalog 删除并收敛到 core family TU；
  `DexVmAndroidContext::zip_streams` 与 integration GC root 已删除，严格 loader ZIP
  parser/inflate 由 per-VM `ZipRuntime` 封装。

## 后续补充（2026-08-30）

- 对照 pinned AOSP libcore 与 `.local/aosp/core.jar`，补齐首批 `File` 路径/对象语义：
  `getName/getParent/getParentFile/isAbsolute/getAbsoluteFile/isHidden`、
  `equals/hashCode/compareTo/toString/listRoots/toURI`。
- `File` 恢复 `Serializable`/`Comparable` 接口、非 final 公共方法、私有 `path` 字段和
  compiler bridge 标志；增加 `FilenameFilter`/`FileFilter` 的 API 形状。
- `IoFileSystem` 窄接口透传 guest 工作目录与单级建目录：相对 `getAbsolutePath()` 只消费
  VFS 工作目录，`mkdir()` 要求父目录已存在，`mkdirs()` 继续递归创建。
- `file_vfs_tests` 覆盖双后端路径/对象/URI 语义、声明 shape 及 `mkdir`/`mkdirs` 差异；
  其余权限、metadata、过滤列表和 rename 等 File 长尾仍按后续批次明确缺失。

状态：完成。
