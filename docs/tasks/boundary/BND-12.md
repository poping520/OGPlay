# BND-12 · OpenSL ES audio pump 与 guest callbacks

## 目标

把 OpenSL PCM mixer 加性接入进程唯一 stereo output，并通过专用 guest thread 执行 Object、
Play 与 BufferQueue callback。

## 交付与验收

- [x] `AndroidGuestProcess::RenderStereoAudio` 保持 SoundPool zero-fill/mix，再调用 OpenSL
  additive mix，仍由上层向 HAL 只提交一次。
- [x] module 只依赖窄化 `OpenSlesCallbackSink`，离开 object/mixer lock 后发布固定参数事件；
  不依赖 process、facade、JNI 或 HAL。
- [x] async Object callback、每个 consumed buffer queue callback、Play head-at-end/marker/
  periodic-position callback 携带 AOSP caller/context/event 参数。
- [x] process 预留独立 callback TLS/thread-info/1 MiB stack，使用独立 Dynarmic CPU 与 guest
  thread id；不占 JNI attached-thread 计数，callback 内可经普通 SVC #2 再次 Enqueue。
- [x] callback failure 留作 structured exception，在下一 process invoke/audio pump 恢复；
  shutdown 先唤醒阻塞 callback 再释放 context。
- [x] 修正地址规划：既有 JNI array lease 实际覆盖到 `0x717fffff`，OpenSL static/object/
  callback arena 移入 `0x71800000..0x71bfffff` 并在 loader 前保留。
- [x] OpenSL private-callable fast/slow guest memory fault identity、queue-full-before-copy、callback
  argument、process lifecycle 与 SoundPool coexistence focused tests 通过。

## 验证

OpenSL、SoundPool、Android process/session、preflight/late-load、fault 与 architecture focused
CTest：19/19 通过。能力保持 `partial`，待总任务要求的最终 full CTest 后再晋升。
