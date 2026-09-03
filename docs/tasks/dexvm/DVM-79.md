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
- 第二批补齐 `canRead/canWrite/isFile`、四个 filter/listFiles 重载、`renameTo` 与两个
  `setWritable` 重载；filter 通过 guest virtual `accept` 回调，异常原样传播。
- `IoFileSystem` 的 `Stat` 透传 writable 事实并增加文件 rename 窄接口；rename 要求源和
  目标父目录存在、允许覆盖文件，目录子树继续明确失败。`setWritable` 在 VFS 尚无权限
  修改能力时只确认“已可写且请求设为可写”，不会伪造权限变更成功。
- 双解释器后端测试覆盖可访问性、文件类型、三类过滤、回调异常、文件覆盖 rename、
  缺失父目录/null 参数与 bounded writable 语义；metadata、临时文件、权限变更和
  canonical path 等剩余长尾继续 deferred。
- 最终检查区分“VFS 未装配”和“路径不存在”：前者由无 checked exception 的 `File`
  查询/变更 API 明确抛 `UnsupportedOperationException`，`createNewFile` 保持 `IOException`；
  后者继续遵循 Android 的 false/null 返回语义。
- `FilenameFilter`/`FileFilter` 回调期间以 execution-local 临时强根保活原始结果数组，
  `compareTo` 同样保活跨虚调用的左路径；显式 GC 回调回归锁定不会访问已清扫引用。
- `FileOutputStream` 补齐 API 19 的两个 append 构造器、`FileDescriptor` 构造器、
  `getFD` 与自有 `write(int)`/`write(byte[],int,int)` override；路径构造在构造时创建或
  截断文件，输出流与逻辑描述符共享底层输出状态，并区分拥有与借用描述符的关闭语义。
  `getChannel` 依赖完整 `java.nio.channels.FileChannel`，按既有边界继续 deferred。
- `OutputStream` 恢复 API 19 抽象基类、公共构造与 Closeable/Flushable 契约；默认
  `write(byte[])` 虚派发区间写入，默认区间写入逐字节虚派发子类 `write(int)`，基类
  `flush/close` 保持 no-op。ByteArray/Filter/Buffered/Data/File/Socket 输出子类因此可按
  own override 或父类默认实现工作，不再要求任意子类持有 `IoRuntime` 输出侧状态。
- `FileInputStream` 补齐 API 19 的 File/String/FileDescriptor 三个构造器，以及
  `available/close/getFD/read/read(byte[],int,int)/skip`；路径流拥有描述符，FD 流借用描述符，
  多个流通过同一逻辑 FD 共享读取位置。`getChannel` 按非 NIO 范围继续 deferred。
- `InputStream` 恢复抽象基类、公共构造与 Closeable 契约；默认 bulk read 与 skip 经 vtable
  调用子类 override，并保持部分读取后的异常抑制语义；默认 available/close/mark/
  markSupported/reset 与 API 19 对齐，自定义子类无需持有 `IoRuntime` 状态。
- 2026-09-03 后续在同一 Java IO family 增加 `ObjectOutputStream` 无参空壳；按 API 19
  继承 `OutputStream` 并实现 `ObjectOutput`、`ObjectStreamConstants`，其中
  `ObjectOutput` 继承 `DataOutput`、`AutoCloseable`。空壳只支持 protected 空构造；
  `DataOutput`、`ObjectOutput` 发布 API 19 完整 abstract 方法表，供类链接与自定义子类构造，
  不宣称 Java serialization 能力。
- 同日继续补齐 `DataInput/ObjectInput` 方法表与 `ObjectInputStream` API 19 层级；两个对象流
  的公开包装构造器经 `IoRuntime` 接管底层流，写入/校验 stream header，并以 block-data
  wire format 实现接口继承的字节、基本类型、modified UTF、available/skip/flush/close。
  `writeObject/readObject` 仅支持真实 wire token 的 `null` 与 `String` 往返；对象图、引用 handle、
  class descriptor 和 custom serialization hooks 仍明确不支持。
- PVZ 实跑暴露上述范围不足后，继续按 AOSP 4.4.4 wire format 补齐默认 `Serializable` 类描述符、
  显式 `serialVersionUID`、按 primitive-first/name 排序的字段、可序列化父类层级、循环/重复引用
  handle、枚举常量以及 `Date` custom data；输入端按本地字段名/类型恢复，未知字段可跳过，UID、
  类型和边界不匹配明确抛 `IOException`。对象 handle 由 `IoRuntime` 跟随流 owner trace/sweep。
  `FileDescriptor.sync()` 同步关联输出缓冲到 VFS，使 `ObjectOutputStream` 后的 durable-save 路径
  与 API 19 调用链闭合。数组、`Externalizable`、应用自定义 hooks 和默认 UID 计算继续 deferred。
- Asphalt 5 回归暴露 Android 资源桥仍直接实例化抽象 `InputStream`：core 恢复 API 19 默认
  bulk-read 虚派后，资源读取会落到抽象 `read()`，游戏随后解析空数据并在 native package
  初始化中崩溃。`AssetManager.open`/`Resources.openRawResource` 现统一返回具体
  `ByteArrayInputStream`，继续复用 `IoRuntime`；回归测试锁定非零 APK payload 的
  `available`、bulk read 与 EOF。

状态：完成。
