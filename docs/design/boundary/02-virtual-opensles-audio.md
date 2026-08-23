# Virtual libOpenSLES.so 与 PCM Audio Boundary

状态：Accepted（2026-08-24）  
实现范围：Android 4.4.4 / API 19 A32 guest，AOSP `framework/wilhelm`

## 1. 目标

OGPlay 为真实 guest `libOpenSLES.so` import 提供 ABI 完整的 synthetic Virtual SO，并把
Android 游戏最常用的 OpenSL ES PCM buffer-queue playback 接入现有 stereo PCM16 audio
pump。实现必须保持既有 Fast Host Call：所有直接导出和 guest vtable method 都在 seal 后
绑定独立 `{fn,self}`，不得增加运行期 method-id switch、SONAME lookup 或 facade dispatch。

这里的“完整”分为两层：

1. 完整发布 Android 4.4.4 `libOpenSLES.so` 的 ELF surface：3 个函数和 `sl_iid.c` 中全部
   `SL_IID_*` data symbol，名称、对象类型与 IID 值一致；
2. 完整实现 OGPlay 游戏进程范围内的 engine/output-mix/PCM audio-player 对象链，包含
   Object、Engine、OutputMix、Play、BufferQueue/AndroidSimpleBufferQueue、Volume 接口。
   AOSP 可见但超出 OGPlay 范围的 recorder、MIDI、3D、effect、URI/FD decoder 等创建请求
   返回规范错误，不伪造对象或成功。

## 2. AOSP 事实

- `src/Android.mk` 表明 target `libOpenSLES` 由 `sl_entry.c`、`sl_iid.c`、`assert.c`
  构成并转发到 `libwilhelm`。
- `sl_entry.c` 只导出 `slCreateEngine`、`slQueryNumSupportedEngineInterfaces`、
  `slQuerySupportedEngineInterfaces`。
- `sl_iid.c` 还导出 OpenSL ES 1.0.1、Wilhelm output-mix extension、Android API 9/12 的
  全部 `SL_IID_*` pointer variables。变量值指向 `OpenSLES_IID.c` 的 16-byte GUID。
- `OpenSLES.h` 的 handle ABI 是 `const vtable **`。例如 `SLObjectItf` 指向一个 guest slot，
  slot 再指向 10-entry Object vtable；interface method 的第一个参数仍是该 handle。
- `classes.c` 将 Object/Engine 设为 engine implicit interface，将 Object/OutputMix 设为
  output-mix implicit interface，并将 Play 设为 audio-player implicit interface；BufferQueue、
  Volume 和 AndroidConfiguration 等按请求显式暴露。
- Android PCM playback 接受 BufferQueue 或 AndroidSimpleBufferQueue source、PCM format 和
  OutputMix sink；AOSP buffer queue 在满时返回 `SL_RESULT_BUFFER_INSUFFICIENT`，消费后递增
  play index 并回调。

## 3. ELF symbol 与地址模型

现有 `BionicHleSymbol` 扩展为 function/data 两种 kind，并携带 size。synthetic link module
据此生成 `STT_FUNC` 或 `STT_OBJECT`；data symbol 不进入 hot table，也不占 dense SVC slot。

边界地址划分保持现有全局规划：

- `0x70000000..0x70ffffff`：SVC #2 dense callable thunk；
- `0x71000000..0x714fffff`：既有 JNI ABI/lease；
- `0x71500000..0x717fffff`：OpenSL guest ABI arena；
- `0x71800000..0x71ffffff`：保留。

OpenSL arena 包含：

- exported IID pointer variables（每个 4 bytes）；
- exact 16-byte IID records；
- immutable interface vtables；
- bounded object/interface handle records。

静态 metadata/vtable 区写完后封为 read-only；object handle/state slot 位于独立 read-write
页。对象宿主状态以 generation-safe handle table 为事实来源，不将 guest 地址解释为 host
pointer。销毁对象立即从 active table 移除并清空其 guest interface slots。

## 4. Callable metadata

`BoundaryExportDefinition` 增加 visibility/kind，区分：

- public function：进入 synthetic ELF 且分配 callable slot；
- public data：进入 synthetic ELF，地址来自 OpenSL ABI arena，不分配 callable slot；
- private callable：仅供 guest vtable 使用，分配 callable slot但不进入 dynsym。

所有 private callable 仍有冷路径 diagnostic name，并在 seal 时直接绑定 export-specific
handler。正常路径保持：

```
guest vtable entry -> SVC #2 -> dense slot -> {fn, OpenSlesModule*}
                   -> concrete method handler -> continue JIT
```

## 5. 对象与接口语义

对象类型：Engine、OutputMix、AudioPlayer。共同 Object interface 实现 Realize、Resume、
GetState、GetInterface、RegisterCallback、AbortAsyncOperation、Destroy、priority getters/setters
和 loss-of-control validation。同步 Realize 完整支持；async 请求以同一次状态提交完成并把
回调排入 guest callback queue，避免 C++/guest callback 穿过 Dynarmic host callback。

Engine interface：

- CreateOutputMix；
- CreateAudioPlayer；
- object/interface query；
- unsupported object constructors 和 extension query 返回 AOSP 对应的
  `FEATURE_UNSUPPORTED`/zero-result，不产生对象。

AudioPlayer 只接受以下受检 source/sink：

- source locator：`SL_DATALOCATOR_BUFFERQUEUE` 或
  `SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE`；capacity 1..255；
- format：PCM，mono/stereo，8/16-bit container，little endian，正 sample rate；
- sink locator：已创建且未销毁的 OutputMix。

Play 完整保存 stopped/paused/playing、position、callback/mask、marker 和 update period。
BufferQueue 实现 Enqueue/Clear/GetState/RegisterCallback；Enqueue 在 guest memory 完整预检后
复制拥有型 PCM 数据，queue 满不部分提交。Volume 实现 millibel level、mute 和 stereo
position；render 时转换为线性 gain/pan。未请求或对象状态不允许的接口返回
`FEATURE_UNSUPPORTED` 或 `PRECONDITIONS_VIOLATED`。

## 6. Mixer 与 callback

`OpenSlesPcmMixer` 位于 audio module，拥有线程安全 player/queue/render state。输出统一为
stereo PCM16，支持 mono duplication、8-bit unsigned/16-bit signed little-endian decode、
线性重采样、volume/mute/pan、64-bit accumulation 和 saturation。它不直接调用 SDL/CoreAudio。

现有 process audio pump 先让 SoundPool zero-fill/mix，再调用 OpenSL mixer additive mix，
最终仍只向 `hal::AudioOutput` 提交一次。每个被完整消费的 queue item产生一个 callback
event；render 在锁外通过注入的 guest callback executor 调用：

```
buffer queue callback(caller_itf, context)
play callback(caller_itf, context, event)
object async callback(caller, context, event, result, state, interface)
```

AndroidGuestProcess 为这些 native audio callback 准备独立 A32 guest thread context。callback
不得在 audio mixer 锁内执行，不得重入当前 Dynarmic host callback；callback 内再次 Enqueue
走普通 SVC #2 direct path。

## 7. 并发与失败

- module object table、mixer queue 和 callback queue 分锁；锁顺序固定为 object -> mixer，
  callback 永不持锁调用 guest。
- fast/slow path 复用相同 handler，guest memory fault 保留原 identity。
- 参数错误返回 `SLresult`；只有 OGPlay contract violation、损坏的 synthetic handle 或 memory
  transport fault 抛 C++ exception。
- object/queue/player 数量、queue capacity、单 buffer bytes、总 queued bytes 设硬上限；失败前
  不修改状态。
- preflight 只生成 OpenSL function/data/private-callable metadata，不构造 mixer、对象或 callback
  thread。

## 8. 验收

- AOSP 4.4.4 `libOpenSLES.so` public function/data export 集精确匹配；
- IID variables 可由 guest relocation 读取，值指向 exact GUID；
- engine -> output mix -> PCM player -> realize -> interface query -> enqueue -> play 可端到端输出；
- queue state、clear/full、play state、volume/mute/pan、destroy/stale handle 均有测试；
- mono/stereo、8/16-bit、resample 和多 player 饱和混音有离线测试；
- buffer callback 从独立 guest callback thread 执行并允许 callback 内 Enqueue；
- fast/slow memory fault equivalence、late dlopen、metadata-only preflight、architecture gate 通过；
- 最终全量 CTest 通过后，`runtime.opensles_virtual_so` 才可从 stub 前进。

