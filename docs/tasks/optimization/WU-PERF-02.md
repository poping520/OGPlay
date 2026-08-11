# WU-PERF-02 · Page-aware CStringLength

目标：把逐字节 guest C string 读取统一为 memory 层一次加锁、逐页验证和 host `memchr`。

验收：

- [x] `AddressSpace::CStringLength` 只验证扫描实际到达的页并保留精确 fault 元数据。
- [x] GLES cstring、Bionic strlen、HLE shader/name 与 JNI name/descriptor 共用该 API。
- [x] 成功扫描后按精确长度一次读取字符串内容，不再每字符 Read/lock/validate。
- [x] 测试覆盖页内 NUL、跨页 NUL、上限、权限 fault 和线程 id。
- [x] PERF 阶段 `windows-msvc` 构建与 full CTest 506/506。
