# BND-8 · OpenSL ES AOSP ABI 设计

## 目标

以 Android 4.4.4 AOSP Wilhelm 为事实来源，定义 `libOpenSLES.so` 的 public ELF surface、
guest C-vtable/object ABI、PCM playback、callback threading 与 OGPlay audio 接入方案。

## 交付与验收

- [x] 核对 `src/Android.mk`、`sl_entry.c`、`sl_iid.c`、`OpenSLES_IID.c`。
- [x] 核对 Object/Engine/OutputMix/AudioPlayer/Play/BufferQueue/Volume ABI 和 class exposure。
- [x] 区分 public function、public data、private vtable callable metadata。
- [x] 定义 OpenSL guest ABI arena、object lifetime、PCM mixer 与 callback thread。
- [x] 明确 OGPlay 范围外能力的规范失败，不引入 Android media server/Binder。
- [x] 发布设计文档 `docs/design/boundary/02-virtual-opensles-audio.md`。

