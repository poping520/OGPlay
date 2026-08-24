# DVM-82 · java.nio 与 JNI direct buffer

## 目标（一句话）

建立 VM 级 `NioRuntime`，让 API 19 Buffer 家族、typed view、JNI direct-buffer 与后续
Java GLES 共用一套 backing、cursor、字节序和 guest 地址语义。

## 依赖

- DVM-80：`java_nio.cpp` family TU 与 Java ownership 已收回 core。
- DVM-81：API 19 类型体系已恢复。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase C。

## 范围

- 发布 `Buffer`、`ByteBuffer`、`ByteOrder`、六类 typed buffer、heap/direct/view concrete
  class 与四类 Buffer exception 的 API 19 首轮 shape。
- 实现 cursor/mark、allocate/wrap/direct、相对/绝对/批量 get/put、array、slice、duplicate、
  read-only、compact、字节序和全部 typed view。
- backing side table 由 GC trace/sweep/clone hook 管理；view 共享 storage、cursor 独立。
- direct memory 只经 integration 注入的强类型 guest-address 接口进入 `AddressSpace`；JNI
  `NewDirectByteBuffer`、`GetDirectBufferAddress`、`GetDirectBufferCapacity` 复用相同状态。
- 本 WU 延续阶段授权，不受通常单 WU 10 文件与 family TU 800 行限制。

## 不做

- 不实现 mmap/file channel、完整 charset codec 或宿主指针直出。
- 不进入 Java GLES、AudioTrack 或 SystemClock。
- 不运行全量 CTest；全量回归留到阶段最后一个 WU。

## 验收

- heap/direct/view 共享、cursor/mark、只读、大小端、direct 地址与非法 guest range 均有
  机器测试。
- Windows `windows-msvc` configure、Debug 全目标构建与 DVM-82/core catalog/JNI/
  architecture 定向回归通过。
- MODULE、设计基线、任务索引、CURRENT 与 `capabilities.toml` 同步。

## 结果

- 已建立 per-VM NIO side table 与 heap/direct/view 统一 backing；typed view 不复制数据。
- API 19 Buffer 首轮 surface 已进入 core catalog；direct arena 由 session `AddressSpace`
  分配、校验、读写和回收。
- JNI 三个 direct-buffer 槽已从 expected-unbound 移出生产路径，并与解释执行共享 identity
  和容量事实。
- 验收补强让 `slice`/`duplicate`/`asReadOnlyBuffer` 沿用 receiver concrete class；heap view
  保持 `HeapByteBuffer`，direct view 保持 `DirectByteBuffer`，并由 class identity 回归锁定。
- Windows Debug 全目标构建及定向回归通过；全量 CTest 按计划未运行。

状态：已完成。
