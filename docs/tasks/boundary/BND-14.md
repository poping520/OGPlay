# BND-14 · OpenSL ES callback 与 mute 验收加固

## 目标

用真实 guest callback 证明专用 A32 callback thread 和 callback 内 Fast Host Call 重入，并
修正 mute 错误暂停播放时间的问题，不扩大 OpenSL 对象或接口范围。

## 交付与验收

- [x] mute 只将 player gain 置零，仍推进 source position、消费 buffer 并产生 consumed event；
  pause/stopped 继续保持不推进。
- [x] process fixture 使用分离的 RW data 与 RX code `PT_LOAD`，不绕过 loader W^X gate。
- [x] guest BufferQueue callback 在专用 Dynarmic context 执行，TPIDRURO 精确读到
  `0x71a00000`，且不增加 JNI attached-thread 计数。
- [x] callback 通过普通 SVC #2 `$BufferQueue.Enqueue` 重入，第二个 PCM buffer 随后由同一
  process audio pump 输出。
- [x] OpenSL/SoundPool/process/preflight/late import/fault/architecture focused CTest 20/20 通过。

## 验证

最终 full CTest 按总任务要求在本 WU 提交后运行，结果记录于 BND-15。
