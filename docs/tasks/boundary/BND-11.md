# BND-11 · OpenSL ES concrete module 与 guest object ABI

## 目标

实现 Android 4.4.4 `libOpenSLES.so` 的 3 个 public function、immutable guest vtable 和
Engine/OutputMix/PCM AudioPlayer concrete module 状态链。

## 交付与验收

- [x] 3 个 public function、51 个 public data 和 53 个 private callable 共用单一 catalog；
  private callable 不进入 dynsym，但各自直接 seal 为 `{fn, OpenSlesModule*}`。
- [x] 精确 AOSP Object、Engine、OutputMix、Play、BufferQueue/
  AndroidSimpleBufferQueue、Volume vtable 顺序与 `const vtable **` handle ABI。
- [x] bounded RW object arena、immutable vtable、destroy 清零与 stale handle 明确 fault。
- [x] Engine→OutputMix→PCM AudioPlayer 创建、同步 realize、interface request/query；范围外
  recorder/MIDI/3D/effect/decoder 构造器返回 `SL_RESULT_FEATURE_UNSUPPORTED`。
- [x] PCM source/sink 完整校验、queue full/clear/state、play state/position/mask/marker/update、
  output device、priority、volume/mute/pan 状态接入 BND-10 mixer。
- [x] A32 最大参数扩至 AOSP `CreateMidiPlayer` 的 11 words，仍一次 bulk-read guest stack。
- [x] metadata-only preflight、late import、fast fault 与 architecture focused gate 通过。

## 验证

相关 catalog/Bionic/preflight/boundary/OpenSL/architecture CTest：16/16 通过。callback guest
thread 与 process audio pump 接线属于下一 WU；最终全量 CTest 继续延后。
