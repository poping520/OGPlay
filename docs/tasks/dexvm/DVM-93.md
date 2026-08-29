# DVM-93 · AudioTrack stream 字节回压

## 目标

让 legacy 与 DexVM AudioTrack MODE_STREAM 共享可中断的构造缓冲字节回压，消除饱和
异常、worker 终止和秒级 FIFO 延迟。

## 依赖

DVM-84、DVM-89、DVM-92、ADR-0027。

## AOSP 依据

- `.local/aosp/framework/base/media/java/android/media/AudioTrack.java`：MODE_STREAM
  `write(byte[], int, int)` 阻塞直到全部数据写入 audio sink。
- `.local/aosp/framework/base/core/jni/android_media_AudioTrack.cpp`：`writeToTrack()` 对
  stream 调用 native `AudioTrack::write`，只把 `WOULD_BLOCK` 兼容映射为 0。

## 验收

- [x] 未消费积压达到 `buffer_size` 后 write 阻塞，播放释放空间后完整返回，积压不越界。
- [x] legacy write 不持 media mutex park，连续成功 write 与非零/peak/queued-byte 快照可查询。
- [x] DexVM write park 时释放并恢复 `VmExecutionLock`，另一 guest 线程可调用 play 推进恢复。
- [x] release/player destroy 与 process teardown 可唤醒 writer；无消费者测试不依赖长超时。
- [x] 既有 OpenSL、legacy/DexVM AudioTrack callback 和 teardown 定向回归通过。
- [x] A6 exact 长运行无 guest fault，用户实听确认 BGM 与音效正常。

非目标：不引入完整 AudioFlinger、Binder 或宿主设备回读；255 项只作为内存护栏。

状态：已完成。
