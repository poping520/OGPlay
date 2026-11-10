# 子模块：runtime/resources

API19 资源核心。`OGPlay::Resources` 依赖 `Loader` 的 ZIP/Deflate、[唯一 ARSC 表实现](../../loader/resources/MODULE.md)与 `Core`。公共接口在 `include/ogplay/runtime/resources/resources.h`。调用方提供字节跨度、保活对象、路径身份及可选的新鲜度检查。核心不打开宿主路径，不持有 VM、JNI、VFS 或 integration 对象。

- `sharedFramework` 在进程内初始化一次，失败可重试。`AssetPath` 保持 APK 只读映射及 ZIP 目录存活，framework 的 STORED `resources.arsc` 直接借用原字节。下层 `ResTableData` 索引 package/type/config chunk，条目偏移和值留在原表；entry 在 type chunk 首次访问时同步校验。应用表由各会话单独创建，压缩 ARSC 物化上限 32 MiB。
- `ResStringPool` 校验字符串和样式偏移。UTF-8/UTF-16 字符串按需解码，内部缓存加锁。样式 span 使用 UTF-16 下标。`ResXMLTree` 校验 chunk 和引用并拥有数据源，独立 parser 通过只读树遍历事件。`XmlAttributes` 为样式算法提供统一查询入口。XML 字符串池随树保活。
- `ResTable` 按 API19 的 `match`/`isBetterThan` 选择配置，支持密度覆盖、名称和区域查询、跨 package 引用及有序 bag 继承。bag 按配置缓存不可变快照，读取与配置失效共用锁。配置、路径列表、Theme、XML cursor 及 Asset 读取位置由调用方串行化。跨会话只共享不可变表、ZIP 索引和受锁保护的解码缓存。
- `Theme` 和自由函数提供 4.4 的 style 叠加、XML/default/theme 查找优先级、`@null` 遮蔽、引用解析、六槽值及 `outIndices`。`TypedValue.cookie` 标识 APK，XML 内联值用 `-1`。样式回退到 Theme 时保留值的实际来源 cookie，与 5.0 的修正一致。`getArrayStringInfo` 使用独立的零基 string block，不把 cookie 当数组下标。适配层负责句柄生命周期、Java 数组校验、复制和异常映射。
- `AssetManager` 分配 cookie，framework 为 1。每条路径建立一次只读 ZIP 名称索引。按从新到旧搜索 APK，目录结果按名称合并。STORED Asset 借用字节，`openFd` 返回只读 backing、偏移和长度，宿主/guest FD 由上层创建。DEFLATED Asset 共用 Loader 的流式解码器，每个流保留 32 KiB 历史及当前块状态，展开长度上限 256 MiB，向后读取从起点重放。Asset 计数为进程级。

支持 API19 dense ARSC、编译 XML 和单盘 ZIP32 的 STORED/DEFLATED。畸形结构在构造或首次访问所属 type chunk 时抛 `ResourceError`，ZIP 错误由 Loader 报告。缺资源、缺属性和非 bag 查询使用空结果，数组辅助接口要求有效 bag。仅支持注入的归档路径。overlay/idmap、裸目录 asset path、动态包 ID、较新 ARSC 格式、ObbScanner 和配置事件派发在支持范围外。跨表 `Theme::setTo` 只复制 framework 属性，沿用 4.4 的系统包限制。

算法参考 AOSP `android-4.4.4_r2.0.1` 的 `libs/androidfw/ResourceTypes.cpp`、`AssetManager.cpp`、`Asset.cpp` 和 `core/jni/android_util_{AssetManager,XmlBlock}.cpp`，保留 Apache-2.0 出处。宿主验证入口为 `ogplay_tests --test-case='resources core*'`。`ogplay_resources_benchmark` 独立测量映射、索引和 Theme 应用耗时。
