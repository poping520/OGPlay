# 音频与视频

媒体子系统把 Android 播放协议、编码解码、PCM 混音与设备消费分开。
ROM Java 保存公开对象、监听器和 Handler 协议，runtime 适配 native 资源与事件，
audio/video 提供解码混音，session 按统一 Clock 消费并向 HAL 输出。
PCM 是解码后的采样数据，编码媒体则是 OGG、MP3、WAV 或视频容器中的原始内容。

这样的分工让图形停顿不必同时停止音频，也让无设备测试能够验证消费与位置。
[媒体协议决定](../adr/media.md#adr-0070)选择独立消费和有界解码，
代价是要明确准备任务、队列背压、播放头和关闭顺序，不能用一个“正在播放”标志代替全部阶段。

## 一条媒体路径由哪些部分组成

```text
AudioTrack native ──────┐
OpenSL BufferQueue ─────┼→ PCM mixer ─────────┐
SoundPool → 短音效解码 ─┤                   ├→ AudioOutputPump → HAL
MediaPlayer → 编码音乐 ─┘                   │
VideoView / MediaPlayer → FFmpeg → PCM ─────┘
                                  RGBA → WindowSystem
```

AudioTrack 与 OpenSL 可以直接提交 PCM，SoundPool 先加载短音效，
MediaPlayer 则持有自己的编码来源与解码游标。它们最终参与同一输出消费，
各自的 stop、flush、回调和释放语义仍由对应协议决定。
视频图像交给 WindowSystem，声音进入音频路径，两种结果都受调用方位置与生命周期控制。

例如应用打开一个 APK 中的音乐资源，先取得带起点和长度的来源窗口，
MediaPlayer 准备任务读取元数据，播放时按块解码，mixer 按实际消费推进来源位置，
AudioOutputPump 将混合 PCM 提交设备。应用关闭原资源 FD 后，
已捕获窗口仍继续保活自己的来源，不依赖调用者保留临时描述符。

## 编码来源为什么是拥有型窗口

EncodedAudioSource 描述资源 ID、APK 条目、VFS 路径或字节区间；
EncodedAudioDataSource 是解码器使用的拥有型读取接口，只提供 Size 与 ReadAt。
VideoDataSource 使用相同的窄接口形状。来源解析留在上层，
媒体模块无需知道 ZIP、guest 路径或宿主目录结构。

integration 从 FD 原子捕获起点、长度和来源，生成 VFS lease。
lease 是固定版本与区间并携带保活对象的读取租约，独立于原 FD 的游标、关闭、unlink 或 rename。
只读 backing 可共享，可写窗口则在统一预算内快照。
这样能保留“设置来源之后继续播放原内容”的语义，代价是可写捕获需要显式快照内存。

[安装与资源决定](../adr/session.md#adr-0020)要求媒体共用唯一文件身份，
而不是反查宿主路径重新打开。取消 token 继续传到底层块读取和解压，
避免 reset 后仍无界消耗资源；普通同步宿主读本身不因此获得任意时刻可抢占保证。
具体来源锁与版本规则见[存储设计](storage-security.md)。

## 长音乐与短音效为什么不同

短 SoundPool 样本允许在总预算内完整解码，播放时可低成本创建多个 voice。
voice 是一次独立播放实例，同一 sample 可以被多个 voice 使用。
长音乐则按编码块推进，不保存整首 PCM，实例、窗口、codec 工作区和队列分别有界。
这避免长文件按解码后体积占满内存，也避免每个消费者再复制完整编码来源。

增量解码下，MP3 后退 seek 需要重置解码器并从头丢弃到目标位置，
所以精确位置不等于常数时间定位。SoundPool 的缓存、音乐的增量路径和视频的预读
分别适合不同负载，三者不能被一个无差别 ReadAll 接口替代。
预算和格式范围见 [audio 契约](../../src/audio/MODULE.md)。

编码音乐的元数据准备归每实例 jthread，reset、release 或换源先取消并 join。
join 表示等待任务真正结束；提交前还检查任务身份，旧结果不会重新激活新一代播放器。
把任务归 owner，也就是负责其停止和释放的对象，可以明确析构边界，
相应地，取消与等待需要进入每个可能阻塞的来源路径。

## PCM 混音怎样保持连续性

OpenSlesPcmMixer 保存 player、queue 与消费状态，接收有限的 mono/stereo PCM8/PCM16。
mono 复制到双声道，重采样跨 buffer 保留分数相位，多音源用宽累加器相加后统一饱和。
假设两个相邻 PCM buffer 组成连续声音，来源采样率与输出设备不同：
mixer 消费第一个 buffer 末尾时保留分数相位，继续从下一个 buffer 插值。
若每块重新从零计算相位，连续样本会在边界发生重复或遗漏，
所以 buffer 边界不应成为声音时间轴的边界。

```text
source buffer A → source buffer B → 重采样 / 混音 → 设备队列 → 播放
       └──────── source frame 位置 ────────┘           ↑
                                              设备呈现估计
```

播放头按已消费 source frame 计数并保留 API19 无符号回绕。
完成回调对应真实 source buffer 消费，而设备已提交或已呈现的帧数属于后续阶段，
设备已经播放多少与源数据消费多少不混为一个指标。

stream、实例和左右声道增益共同影响该音源。mute 只把增益变为零，
来源位置、队列消费与完成回调继续推进；pause 才冻结位置，恢复后从原位置继续。
两者短时间内都“听不到声音”，状态机含义却不同：
如果把静音实现为暂停，音乐、动画和回调会逐渐错位。

mixer 的控制与 render 共用内部锁，回调在锁外交付。
这样回调可以再次 Enqueue 或调用 Java，而不递归等待同一 mixer 锁。
为此交付前需要固定必要状态，诊断也只能复制计数，
不能为了查询而触发混音或消费。

## AudioTrack 的背压与回调

MODE_STREAM write 以构造时的 buffer 字节预算施加可中断背压。
队列未消费量不足以容纳本次写入时，writer 释放 VM 执行锁等待消费者，
恢复后复验 owner。play、clear、destroy 和 teardown 会唤醒相应等待。
这是用真实消费限制延迟与内存，而不是通过 sleep 或重试制造写入成功。

MODE_STATIC 保留完整样本，播放不从队列移除数据。
STREAM stop 复位 head 但保留队列，STATIC stop 回到样本起点；
flush 只在非 playing 时丢弃 STREAM 队列，pause 保留位置。
这些差异来自接口本身，复用 PCM 底座不代表可合并控制状态。

位置通知根据真实消费产生，再交给 guest Handler。
若 periodic callback 同步回填 PCM，本轮 pump 暂停补发已过期通知，
剩余跨越周期保留到后续安全点。这样既避免一次 pump 无限回调，
也不丢失回填所需的通知；生成、投递与延期分别计量。

## OpenSL：vtable 与回调线程

Virtual libOpenSLES 提供 public ELF 导出和 IID 数据，
对象以 guest vtable 调用并通过防陈旧句柄表识别生命周期。
vtable 是 guest 中的方法地址表；Engine、OutputMix 和 PCM 播放器
分别管理 realize、接口查询与 destroy，而非返回一个万能成功对象。

SL_IID_BUFFERQUEUE 与 SL_IID_ANDROIDSIMPLEBUFFERQUEUE 使用同一已定义播放器能力，
AndroidConfiguration 在明确请求时提供，并允许 realize 前设置 stream type。
当前 PCM 路径不承诺录音、MIDI、3D、effect 或 URI/FD 解码，
这些接口查询仍按自身能力边界回答。

消费后的 guest callback 交给进程 callback sink，
由专用 guest 线程、CPU、TLS 和栈执行，回调中的 Enqueue 再走普通 ABI。
设备线程不会直接跳进某个宿主函数绕过 guest 调用协议。
关闭时因此要先禁止新回调并停止 worker，再退役对象与 arena；
回调故障保存到进程 owner，由主循环恢复。

## SoundPool 的池、样本和 voice

每个 SoundPool 有独立池身份，sample 与 voice 均按池隔离。
load 在真实解码完成后发布 loaded 和完成事件，play 按左右音量、loop、rate 与 priority 创建 voice。
达到并发播放上限时按优先级和年龄抢占，避免任意覆盖一个正在发声的实例。

同源不可变样本可共享解码结果，但 unload 与 release 按池引用回收。
autoPause/autoResume 只恢复自动暂停的 voice，之前手动暂停的保持原状态。
这使缓存复用不会扩大控制范围，池引用与播放实例则需要分别建账。
缺 loader 或损坏来源保留真实失败，不能提前标记为 loaded。

## MediaPlayer 与视频解码

ROM MediaPlayer 保存参数、Listener 与 Handler，native transport 管理阶段和资源。
准备中、错误、stop 与 reset 的含义不同，异步事件只在真实完成或失败后产生，
reset 清理旧待发事件。setDisplay/setSurface 绑定受检 ROM Surface，
而不是让视频直接覆盖宿主屏幕。

视频使用编译期链接的裁剪版 ogplay_ffmpeg。
所有输入走自定义 AVIO，即把 FFmpeg 的读取操作接到 Size/ReadAt 来源上；
运行库不提供任意网络 protocol。缺 codec 报告具体轨道错误，
不会静默去掉音轨后宣称完整播放。

[FFmpeg 决定](../adr/media.md#adr-0136)避免运行时猜测库名和镜像内部结构，
代价是组件扩充与 SDK 升级要经过独立预编译交付。
后端自检与真正媒体解码是不同验证，具体格式和预算见 [video 契约](../../src/video/MODULE.md)。

## 视频拉模型与预读

VideoPlayer 使用拉模型：调用方传位置给 TakeFrame，ReadPcm 消费音频，
SeekTo 同步移动两个游标。调用方拥有位置，decoder 不决定会话时间，
也不会为了追画面而丢弃音频。

buffered worker 有界预读图像和 PCM，预读同时受时间与字节约束。
高分辨率图片与低分辨率图片的内存成本差异很大，仅限制帧数不能保证内存有界；
仅限制字节又可能让低分辨率队列领先当前位置太多，拖慢 seek、浪费转换并扩大音画游标差距。
因此预读按实际图片 backing、队列容量与 PTS 间隔控制，
PTS 是媒体中标记图片呈现时刻的时间戳。队列限制只是资源与领先量护栏，
低容量或较慢后端可能产生欠载，实现不承诺任意媒体的实时无欠载性能。

实时拉取只使用已准备内容，暂时空读保留欠载事实，不当作 EOF。
EOF 表示来源确已结束，欠载只表示当前还没有数据：播放器可能正在准备下一块，
或由于预读受背压暂时不能继续 demux。若用空读直接宣布完成，应用会提前收到播放结束事件。

解码图片持有自己的 buffer 引用，seek 或播放器退役不会使在途图片悬空。
到期图像转换为 RGBA 后进入同一 WindowSystem 合成，
窗口裁剪、层级和可见性继续生效。媒体位置与窗口发布因此需要协同，
当前完整影音同步和长期听测仍有验证缺口。

## Clock 与设备消费怎样配合

AudioOutputPump 归 session。实时 worker 按设备队列水位补 PCM，
离线路径把 Clock 差值换算为帧数并保留不足一帧的余数。
媒体位置、已混音、已提交和估计已呈现各自反映不同阶段，
设备排队并不意味着听众已经听到对应采样。

手动 step 时存在一个反向等待：主 Looper 被 write 背压阻塞，而 step 又等待主队列完成才消费。
实现允许在许可 step 正执行且 writer 阻塞时，由独立消费者先释放它，
提前消费量再从后续 Clock 额度扣回。
这避免互等与重复播放，代价是额外维护消费额度；无 step 或无背压时不擅自消费设备。

## 关闭与验收边界

设备提交、水位查询、mixer 或 callback 故障先中断 producer，再保存原错交还主循环。
健康协作关闭期间保留必要音频消费帮助生命周期结束，随后停止并 join，
资源回收不会靠 detach 或退出宿主进程完成。
原因见[有界退出决定](../adr/diagnostics.md#adr-0025)。

验证要看实际输出。播放器状态进入播放阶段说明协议转换发生，
消费计数增加说明来源被推进，设备提交说明数据交给 HAL，
实际 PCM 或图像对照才说明内容在该条件下正确，这些证据不能由同一个状态位代替。
因此一个短音测试不关闭长时漂移或影音同步问题，视频有画面也不代表音轨已完整解码。
保持分阶段指标和真实失败原因，才能明确当前到达哪一层，而不是把所有无声都归因于设备。

当前手动背压路径已实现，但端到端证据、raw-resource 全链、长期播放与媒体主时钟仍有缺口。
具体范围见[音视频已知问题](../state/KNOWN-ISSUES.md#window-media)。

## 契约、决定与操作

- [audio](../../src/audio/MODULE.md)、[video](../../src/video/MODULE.md)
- [OpenSL](../../src/runtime/boundary/modules/opensles/MODULE.md)、[session](../../src/session/MODULE.md)
- [媒体任务与预算](../adr/media.md#adr-0070)、[FFmpeg](../adr/media.md#adr-0136)
- [MCP 观察](../playbook/MCP-TESTING.md)、[音视频缺口](../state/KNOWN-ISSUES.md)
