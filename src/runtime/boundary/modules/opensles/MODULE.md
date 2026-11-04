# 子模块：runtime/boundary/modules/opensles

## 职责

- 按 Android 4.4.4 AOSP Wilhelm 定义 `libOpenSLES.so` 的 public ELF surface 与 guest ABI。
- 拥有 OpenSL ES concrete final module、guest object/vtable arena 与对象生命周期；通过构造
  注入 audio/callback service，不反向访问 `AndroidBoundaryHle::Impl`。
- 发布 API19 public function、`SL_IID_*` pointer global/只读 UUID record 与
  AOSP 顺序的 private vtable callable；public data 不进入 callable hot table，private
  method 不进入 dynsym。Engine/OutputMix/PCM AudioPlayer concrete handler 直接拥有对象语义。

## 依赖与边界

可依赖 boundary core、memory 及窄化 audio service。不得依赖 facade、JNI、Android media
server、Binder 或 host filesystem；guest address 必须使用强类型并完整预检。范围外 recorder、
MIDI、3D、effect 与 URI/FD decoding 必须明确失败。

## 不变量

- AOSP IID 名称、pointer symbol、16-byte value 及 `const vtable **` 布局保持 API 19 ABI。
- PCM AudioPlayer 的 AndroidConfiguration 必须显式请求，可在 Realize 前取得；仅支持
  `androidPlaybackStreamType`，默认 MEDIA(3)，接受 API19 的 0..5，配置接入同一 mixer 的
  stream gain。Realize 后禁止更改；Get 支持四字节尺寸查询/不足缓冲报告，未知 key、
  非法参数及其他 Android 配置仍明确失败，不提供系统路由、录音或性能模式。
- OpenSL 返回失败时按 method/result/guest thread/LR 写结构化警告；必需接口缺失另报 IID。
- EGL/GLES hot path、SVC transport、JNI 与 libc override 语义不因本模块改变。
- PCM playback 只经 facade 的窄化 additive mix 接入会话唯一 audio output。Object/Play/
  BufferQueue callback 在释放 module/mixer lock 后发布到注入的 `OpenSlesCallbackSink`；
  process 使用专用 guest thread/CPU/TLS/stack 执行，callback 内 Enqueue 仍走普通 SVC #2，
  C++ exception 不跨 CPU callback并在 process 边界恢复。

## 测试

IID ELF/data ABI 覆盖于 `tests/runtime/bionic_profile_tests.cpp` 与
`tests/runtime/boundary/integration/android_boundary_opensles_tests.cpp`；模块函数和音频状态测试
位于 `tests/audio/open_sles_pcm_mixer_tests.cpp`，process callback thread、TPIDRURO 与 callback
内 SVC #2 re-enqueue 端到端覆盖于 `tests/runtime/android_guest_call_session_tests.cpp`。
