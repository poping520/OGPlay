# ADR-0027 · AudioTrack stream 按构造缓冲字节回压

- 状态：Accepted
- 日期：2026-08-29
- 关联：[ADR-0023](0023-native-watchdog-observable-progress.md)、
  [ADR-0025](0025-teardown-cancellation-and-graphics-retirement.md)、
  [DVM-93](../tasks/dexvm/DVM-93.md)

## 背景

API 19 `AudioTrack.write` 明确规定 MODE_STREAM 会阻塞直到数据全部写入 audio sink；JNI
`writeToTrack` 同样调用 native `AudioTrack::write`。OGPlay 的 legacy 路径却把 mixer 的
255 项队列当作 Android 缓冲：满时抛宿主 C++ 异常；DexVM 路径则返回 0。前者可让异常越过
Java 边界并终结 native audio worker，后者也没有 Android 的阻塞语义。以 4096-byte write
为例，255 项还会积累约 1 MiB，而 guest 构造时请求的 35280-byte buffer 只有约 0.2 秒。

## 决定

共享 `OpenSlesPcmMixer` 提供可中断的 blocking enqueue，并以每个 AudioTrack 构造时的
`buffer_size` 作为未消费 PCM 字节上限；首 buffer 已播放的 frame 不再计入积压。原有 item
capacity 只保留为有界内存护栏，不再定义正常回压点。播放推进、clear 和 player destroy 都
唤醒 writer；不使用墙钟 sleep、轮询步进或固定重试预算。

legacy Java/JNI 与 DexVM MODE_STREAM 统一使用该原语。Legacy 在等待期间不持 media state
mutex；DexVM 在等待前释放全部 `VmExecutionLock` 深度，唤醒后恢复并重新验证 track/player
identity。MODE_STATIC 保持一次复制语义。release/销毁或 teardown 中断返回
`ERROR_INVALID_OPERATION`，队列饱和不再抛宿主异常或返回 0。

`BeginTeardown()` 与 process Stop 对 audio wait 发布粘性中断，再进行既有 futex/monitor
唤醒。阻塞发生在 JNI/HLE 边界内部，不消耗 guest tick；完成的 JNI 重入仍沿 ADR-0023 作为
既有 advanced 进展，不新增按轮询次数续期的类别。

## 后果

producer 由真实 mixer 消费速率节流，稳态未消费字节不超过构造 buffer，短音效不再排在数秒
PCM 后面；队满也不会杀死 guest audio worker。位置回调中的重入 write 仍可工作，但和真机
一样只有在播放已释放足够字节时才返回；测试不得依赖无限队列。共享 backend 暴露 queued
bytes 与 blocking-writer 数，legacy snapshot 另记录成功 write 次数、非零 write、sample peak
和当前积压，供机器验收与后续受控诊断投影复用。
