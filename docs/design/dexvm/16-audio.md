# 音频专项审计与开发规划

日期：2026-09-19。状态：规划完成，代码未实施。
任务入口：[DVM-189](../../tasks/dexvm/DVM-189.md)；架构提案：
[ADR-0069](../../adr/media.md#adr-0069)。

本轮按用户确认只做源码审计与开发规划。下文是目标设计，不覆盖当前 MODULE 契约，
不将已识别问题标为已修复，不以旧测试通过证明完整 Android 音频兼容。

## 1. 范围与结论

保留宿主解码、PCM 执行和 SDL3 输出；原版 Java 负责普通协议、字段和消息分派，
native 边界负责真实资源、队列及播放状态。收回 CLI 内的资源读取、音源组合和调度策略。
先修既有公开入口的错误、隔离与生命周期，再迁移 BootDex 和输出调度。

纳入本专项：AudioTrack、SoundPool、音频用途的 MediaPlayer、现有 OpenSL PCM 路径、
AudioManager 会话音量、VideoView 音轨接入、编码资源读取、混音、Clock、GC/退出及诊断。
现有 PCM8/16 mono/stereo、OGG/MP3 必须保持；补入常见 RIFF/WAVE PCM8/16 文件读取，
不将 WAV 容器与裸 PCM 混为一谈。

不扩张为完整 Android 音频系统：不引入 Binder 服务、AudioFlinger、厂商 HAL、完整 ART、
录音、蓝牙设备路由、DRM、硬件 offload 或全量 audiofx。AAC/FLAC、OpenSL URI/FD 播放、
MediaCodec/Extractor、JetPlayer 等保留明确能力边界，有真实需求才扩充；不能静默成功。

## 2. 证据范围与当前处理链

证据分级：**C** 为直接可见的代码行为；**R** 为根据结构识别的风险或待 AOSP/实验裁决项。
本轮没有构建、测试运行、游戏复现、设备延迟或听测；C 也不表示已经复现某款游戏症状。
审计覆盖现有应用音频入口及共享执行链，不声称枚举所有 API19 音频 API 或发现全部缺陷。

主要证据入口（仓库相对路径，函数名用于防止行号漂移）：

| 代号 | 文件与定位 |
| --- | --- |
| E1 | [android_media.cpp](../../../src/runtime/integration/dexvm_android/android_media.cpp)：Declare_android_media_*、WriteBytes、PumpAndroidAudioTracks、MixOneVideoView |
| E2 | [open_sles_pcm_mixer.cpp](../../../src/audio/open_sles_pcm_mixer.cpp)：EnqueueBlocking、Clear、SetPlayState、PositionFrames、MixAdditiveStereoPcm16 |
| E3 | [java_sound_pool_mixer.cpp](../../../src/audio/java_sound_pool_mixer.cpp)：Load、Unload、Play、RenderStereoPcm16 |
| E4 | [opensles_module.cpp](../../../src/runtime/boundary/modules/opensles/opensles_module.cpp)：Mix、Allocate、ObjectDestroy、EngineCreateAudioPlayer、PlaySetMask、BufferQueueEnqueue |
| E5 | [android_guest_call_session.cpp](../../../src/runtime/integration/android_guest_call_session.cpp)：RenderStereoAudio、EnqueueOpenSlesCallback、StopOpenSlesCallbackThread、BeginTeardown |
| E6 | [run_apk.cpp](../../../src/frontend/cli/run_apk.cpp)：sound_loader、PumpAudio、driver.step 后补音频、视频 wall-time pacing |
| E7 | [dexvm_bridge.cpp](../../../src/runtime/integration/dexvm_bridge.cpp)：RegisterAndroidOwnerAttachedStateTable；[dexvm_android.h](../../../include/ogplay/runtime/integration/dexvm_android.h)：播放器侧表 |
| E8 | [sdl_audio_output.cpp](../../../src/hal/sdl_audio_output.cpp)：QueuedFrames、Stop、Submit；[video/MODULE.md](../../../src/video/MODULE.md)：拉模型、共享 demux 与背压 |
| E9 | [ogg_vorbis.cpp](../../../src/audio/ogg_vorbis.cpp)、[mp3.cpp](../../../src/audio/mp3.cpp)：解码与输入/输出限制 |

本地参考：`.local/aosp/framework/base/media/java/android/media/` 的 AudioTrack、SoundPool、
MediaPlayer、AudioManager 及值类；`.local/aosp/framework/base/core/jni/android_media_AudioTrack.cpp`。
实际解析 `.local/aosp/framework.jar` 的 classes.dex 核对声明及内部类；相关媒体定义在该 jar，
framework2.jar 未发现 android.media 类定义。当前 BootDex 配方尚未选入这些媒体类。
原版 native 服务完整语义不能只由 Java wrapper 推断；未具备的 frameworks/av/Wilhelm 对照
列为 AUD-01 的取证输入，不能用较新 Android 文档代替 API19 实现。

当前链：

- AudioTrack/OpenSL → OpenSlesPcmMixer。
- SoundPool/MediaPlayer → JavaSoundPoolMixer，全量解码 OGG/MP3。
- process 先混编码音源并限幅，再叠加 PCM 并限幅；CLI 再叠加 VideoView 音轨。
- CLI 在 driver.step 返回后，按 SDL 剩余队列填充 1024 帧块，目标 4096 帧、48 kHz。
  4096/48000 约 85.3 ms 是队列目标量，不是实测端到端延迟。

已具备并应保留：PCM8/16、mono/stereo、真实资源解码、统一 PCM backend、阻塞 write 释放
VM 执行锁、teardown 唤醒、AudioTrack owner 清扫、OpenSL 专用 guest 回调线程及锁外派发。
SoundPoolMixer 的 complete 条目仅证明历史有界 mixer 范围，不等于 Java SoundPool 完整。

## 3. 问题清单与归属

优先级：P1 为语义错误、资源/并发安全或可能挂起；P2 为兼容边界、质量及结构改进。
每项必须在所列批次完成修复验证，或以证据明确裁决不成立；不能因迁类或改名自动关闭。

| 编号 | 级别/证据 | 当前问题与影响 | 修复批次 |
| --- | --- | --- | --- |
| A01 | P1/C，E1 SoundPool.play | priority 被当成 loop；rightVolume、初始 rate 未使用，有限循环退化为布尔 | AUD-02 |
| A02 | P1/C，E1/E3/E7 | SoundPool 构造不建池、maxStreams/priority 无实际约束；soundID 等于 resid，stream 全局共享；release 停其他池，unload 总返回 true | AUD-02 |
| A03 | P1/C，E1/E7 | sound_streams 在 stop/自然结束后无清理；整数 stream ID 持续递增；缺少每池销毁/GC 资源归属 | AUD-02 |
| A04 | P1/C，E1 MediaPlayer | String source、seek、三个 listener setter 空实现或仅日志；start 忽略 Play 返回值；无资源也正常返回 | AUD-02 |
| A05 | P1/C，E1/E3 | MediaPlayer 同源固定 instance=0；release 卸载共享样本并停止其他实例；isPlaying 与自然结束脱节 | AUD-02 |
| A06 | P1/C，E1/E7 | reset 保留源；looping 播放中修改不传后端；音量仅左且播放前设置丢失；release 留 looping，GC 只删 media 表、不停止后端 voice | AUD-02 |
| A07 | P1/C，E1 AudioManager | volume/mute 空操作，音量恒 15、isMusicActive 恒 false；查询与会话播放事实不一致 | AUD-02 |
| A08 | P1/C，E1 WriteBytes、E2 | 零长度/大于 buffer 的 write 被拒绝；STREAM 需要分段阻塞及部分写返回；STATIC 容量处理也不同于 AOSP JNI | AUD-01 |
| A09 | P1/C+R，E1/E2 | STATIC 数据随播放被队列消费删除；flush 在 playing 也清空；stop 重置队首位置但不处理剩余队列/阻塞 writer。需分开 AudioTrack STREAM/STATIC 与 OpenSL 的 stop/clear 语义，不能共享一个粗略状态转换 | AUD-01 |
| A10 | P1/C，E2 MixAdditiveStereoPcm16 | 跨 buffer 将 frame_position 清零，丢弃重采样余数；插值也不跨边界，输出依赖输入分块 | AUD-01 |
| A11 | P2/C，E2/E3/E6 | 音源组逐级 PCM16 饱和，先削波再相加，结果依赖分组；VideoView 还逐 view 饱和 | AUD-01 |
| A12 | P1/C，E4 BufferQueueEnqueue | 按 guest size 先分配 vector、读内存，再到 mixer 检查 16 MiB 上限；可在拒绝前产生巨大分配 | AUD-01 |
| A13 | P1/C+R，E4/E5 | 待执行 OpenSL 回调只带 function/参数，无对象代际或撤销验证；destroy/clear/注销后排队事件仍可能执行；队列无显式数量上限 | AUD-01 |
| A14 | P2/C+R，E4 | Clear 不重置 play_index/事件基线；PlaySetMask 接受 0x1f 但只产生 0x1/2/4；channel mask 未校验。需对照 API19 支持面，不承诺不存在的事件 | AUD-01 |
| A15 | P2/C，E4 Allocate | object arena 单调分配，Destroy 不回收；反复创建销毁会耗尽，不能仅限制同时存活对象 | AUD-01 |
| A16 | P1/R，E4/E5 | Engine/OutputMix/Player 的父子寿命、初始化失败回滚、guest 输出地址预检、回调 join 与 VM/CPU 锁次序须验证；不能从锁外派发推断销毁安全 | AUD-01、AUD-03 |
| A17 | P1/C+R，E1/E2 | playback head 是预混入 SDL 的源帧量且到 UINT32_MAX 饱和，未按 AudioTrack 无符号 32 位回绕；timestamp/实际呈现进度缺少区分 | AUD-01 |
| A18 | P1/C+R，E1/E6 | AudioTrack listener 在 lifecycle 同步调用，未遵循创建 Looper/指定 Handler；回调内阻塞 write 可等待同一线程消费，现有补发合并只绕过一部分情形 | AUD-02、AUD-01 |
| A19 | P1/C+R，E3/E6/E9 | 在 mixer 锁内读取并全量解码；读完整 APK entry/VFS 文件后才检查解码上限；样本/voice/failure cache 无进程总预算 | AUD-01、AUD-02 |
| A20 | P2/C+R，E3/E9 | 非 OGG 一律尝试 MP3；无 WAV 分派；损坏尾部/截断可能返回已解码前缀，需区分允许标签、合法 EOF、解码失败，不将容错一律认定 bug | AUD-01、AUD-02 |
| A21 | P1/C+R，E1/E3 | 长音乐使用短样本全量模型；缺少流式 seek、duration、prepareAsync/完成事件闭环；只补 Java 类无法解决 | AUD-02 |
| A22 | P2/C，E6 | CLI 解析资源、窥探 video_views 并组装全音轨，其他入口不能只调用一个 session 接口得到完整输出 | AUD-01 |
| A23 | P1/C+R，E6/E8 | 音频消费随 driver.step；长帧可能欠载，手动步进产量取决于真实 SDL 水位；暂停不补充但已提交 PCM 仍消费 | AUD-01 |
| A24 | P1/R，E1/E6/E8 | 视频画面按 guest Clock、音频按设备需求拉取；seek/停止后已提交音频、音频欠载与视频队列背压可能失配；多 view 并发也需验证 | AUD-01 |
| A25 | P2/C+R，E2/E3/E8 | render 中 resize/分配、vector erase 队首及持锁工作，不适合直接搬到 SDL 实时回调；设备失败/停止/格式转换后的队列语义需诊断与验证 | AUD-01、AUD-03 |
| A26 | P2/C，E1、BootDex 配方 | 普通 Java 校验、状态和 listener shape 手写，存在常量/重载/访问标志缺失；AudioTrack 限定 STREAM_MUSIC 等限制需作为后端不支持返回，不能伪装 Java 参数非法 | AUD-02 |
| A27 | P1/C，E1 MixOneVideoView | 下采样时只读取本块最后一个被用到的源帧，不保留跨块应跳过的源帧数；192 kHz→48 kHz 每块 1 帧时读源 0、1，而应读 0、4，块边界导致时间漂移 | AUD-01 |

补充边界：48 kHz/S16/stereo 是当前输出选择，本身不是错误；线性重采样可保留为明确质量
边界，但输入分块不应改变时长/相位。OpenSL PCM 支持不包含 URI/FD，是已声明范围，不因
此次审计自动扩大。API19 的合法失败/不可用返回也不是伪成功，必须按具体接口裁决。

## 4. 目标职责和所有权

| 层 | 应拥有 | 不应拥有 |
| --- | --- | --- |
| frontend | 选择设备配置、创建 HAL 输出并注入，驱动会话入口 | 资源解析、检查 Java video_views、混音规则、播放器状态 |
| session | 音源组合、实时/离线运行策略、Clock 映射、输出生命周期及快照 | Java 字段镜像、APK 解码算法、SDL 实现细节 |
| runtime integration | API19 native 适配、VFS/资源 source lease、guest 线程/事件、token 清理 | 重采样算法、第二份播放状态 |
| audio | 播放器/池资源、解码器、PCM 队列、循环/seek、统一混音、背压 | guest 对象遍历、APK/VFS、直接 HAL 调用 |
| video | 现有 demux/视频解码及音轨 source | session 时钟、Java 回调、单独设备输出 |
| HAL | SDL 设备、队列提交/清理与可获得的设备事实 | 游戏音频语义、guest 调用 |

接口按职责建立窄服务（名称在实施时按现有布局确定，不预造大框架）：

- 资源读取返回拥有或借用关系明确的 source lease，可定位、读区间、查询有界长度；
  不在 audio 内重新打开宿主文件。setDataSource/load 接收后调用方关闭 FD，不使有效播放失效。
  VFS 同路径删除重建、替换或修改不能错误复用缓存；cache key 使用真实来源身份/版本/区间，
  不只用路径。复用 IoRuntime 的 OpenFileDescription，不将逻辑 FD 偷换为宿主 fd。
- 不可变 sample 可共享；PoolId、SampleId、VoiceId、PlayerId 必须独立、代际化且防溢出。
  release/GC 只释放自身 voice/lease，最后一个引用才回收 sample；取消未完成的 load。
- Java ordinary state 唯一保存在 BootDex 字段；native token 保存受审资源身份，不存宿主指针。
  API19 原版 int native context 字段使用受检 32 位 token 表，不随意改成 long 字段。
- OpenSL 继续通过真实 A32 guest ABI 接入公共 PCM 能力；不得为了共享引入 Java 依赖。
- 各音源向同一宽精度 accumulator 混音；最后统一舍入/饱和到设备格式。重采样保留跨块
  分数相位与必要前后样本；测试比较不同输入分块和输出块长下的同一信号。
- 压缩音乐采用有界增量解码；短音效保留预解码缓存。先评估现有 stb_vorbis/minimp3 的增量
  接口，优先复用；不因项目有视频 FFmpeg 就把所有短音效改为依赖动态 FFmpeg。
  SoundPool 的每样本限制需对照固定 API19 native；总预算和音乐缓冲另行管理。

## 5. Clock、事件和退出协议

### 实时与确定性模式

实时：采用会话拥有的音频消费 worker，与渲染帧解耦；它只操作线程安全的音频 source、
混音与输出接口，不直接执行 guest、不遍历 Java 侧表。SDL 回调若使用，只做有界传输，
不得等待 VM/解码或分配大缓冲。首版允许继续 SDL push，由 worker 按队列需求补充。

确定性：不以真实 SDL QueuedFrames 决定推进量。由统一 Clock 的差值算输出帧数，保留
不足一帧的余数，写离线 sink；无 step 许可不得推进 source、Clock 或 guest 音频回调。
可选监听是离线结果的副本，不反馈推进决策。块长不是时间来源。
可重复 hash 验收使用固定 PCM/source 及事件调度；不将任意游戏多线程调度宣称为确定性。

必须区分 decoded/source-consumed、mixed、submitted、estimated-presented 四类计数。
AudioTrack head 的 API19 语义由 AUD-01 对照裁决；AudioTimestamp 只有具有可靠映射时才
成功返回。SDL 队列量不是 DAC 精确时间，不能据它宣称采样级硬件 timestamp。

Android Activity.onPause 不等于自动暂停所有音轨；宿主显式会话 suspend 则冻结相应会话
推进，设备队列清理/保留和 resume 恢复按统一策略处理。单 player seek/stop 不得清空其他
player 的声音；允许已提交设备缓冲的有界尾音，并记录界限，禁止宣称立即物理静音。
会话总停止可以清空设备队列。

### 事件与锁

- Java native 事件携带 owner token/generation，经 VM 受控入口调用原版 postEventFromNative，
  由 Handler/Looper 决定 listener 线程；有界排队，release/reset 后取消旧事件。
- OpenSL 继续专用 guest 回调线程。销毁/清队列/重新注册与事件的先后按 API19 判定；
  generation 可撤销尚未开始的旧事件，已执行事件需在销毁协议中保护资源。
  不能在 callback 自身 Destroy 时等待自己退出，也不能 join 持有 VM/CPU 锁的线程。
- 混音锁内不调用资源 loader、解码器、guest 或等待队列；锁顺序在实施的 MODULE 中明确。
  分段 write 在等待期间释放 VM 锁，唤醒后重新验证 player 代际。stop/release 的部分写结果
  保留已接收长度；零接收时按 API19 返回，不把所有中断压为相同结果。
- teardown：阻止新工作 → 取消 load/write 并唤醒等待者 → 停止事件生产 → 退役待执行事件、
  等待非当前回调 → 停消费 worker → 清输出 → 清资源/guest token。与现有 process teardown
  合并为一个可重入协议；具体锁次序用并发测试证明，不按上述文字顺序盲目 join。

视频和音频不拆出竞争读取同一 demux 的线程；共享 VideoPlayer 由单一串行 owner 访问，
向混音器发布 PCM 队列与画面快照。seek 同时换代画面、PCM carry、解码游标和事件；
短时供给不足与 EOF 必须不同，不能把 ReadPcm 暂时无数据当作播放完成。

## 6. BootDex 选择与迁移条件

| 类族 | 本专项决定 | 必须接通/拒绝的边界 |
| --- | --- | --- |
| AudioFormat、AudioTimestamp | 随 AUD-02 迁入 | 常量/数据类型不代表多声道、浮点或 timestamp 能力 |
| AudioTrack + NativeEventHandlerDelegate/匿名 Handler/Listener | AUD-02 迁入 | 27 个原版 native 声明逐一分类；真实 PCM、session id、head、write、loop、rate、生命周期；effects 不支持时精确失败 |
| SoundPool + Delegate/Impl/Stub/EventHandler/Listener | AUD-02 迁入 | native 在 SoundPoolImpl；SystemProperties 不选择禁用媒体的伪成功 stub；真实 load 完成、池隔离、loop/priority/rate |
| MediaPlayer + 必要内部类/Listener | AUD-02 迁入 | media_jni native_init/setup、prepare/seek/state、TimeProvider/Handler、必要 subtitle 类型闭包；不用不完整 Java 替身绕构造 |
| MediaFormat | 随媒体闭包加入 | 普通 Map/ByteBuffer 原版行为，无宿主字段镜像 |
| MediaSyncEvent | 按实际闭包加入 | 只提供值语义，不因此实现录音同步 |
| AudioManager | 保持有界平台 facade | 会话音量/静音、活动状态、真实输出属性和范围内 focus；不加载 IAudioService 服务树 |
| MediaCodec.BufferInfo、codec capability 值类 | 未被闭包/游戏需要则不添加 | 独立值类可选入；MediaCodecInfo 外层依赖 MediaCodecList，不能仅因无 native 就认作纯值类 |
| MediaCodec/Extractor/List、audiofx、ToneGenerator、JetPlayer、录音类 | 延后 | 实际后端能力出现后再准入；原版类可链接不等于功能支持 |

迁移输入以固定 framework.jar 的 DEX 为准，源码辅助解释。每批先核对依赖、native、
overlay 和类唯一归属；原版类进入后删除对应 class/普通方法 intrinsic，不叠两套字段状态。
AudioTrack/SoundPool 原版 native 可由现有 host intrinsic 机制绑定，不必为了 Java native
调用再绕行 ARM 动态库。显式 loadLibrary("soundpool"/"media_jni") 需限定平台库加载身份并
真实完成边界初始化，不能给任意库名 load 成功；保留游戏普通 JNI 和现有 OpenSL ELF 路由。

MediaPlayer 依赖比 AudioTrack 大：构造即创建 TimeProvider 和字幕数组，prepare/播放状态
主要仍在 native。AUD-02 必须完成实例化、资源/FD prepare、start、seek、reset/release、
真实事件的闭包验收。若发现超出音频范围的强制系统服务依赖，记录准确方法及原因并更新
ADR/范围；不得通过空 overlay 宣称迁移完成，也不得把该批从交付中静默删去。

## 7. 最小工作单元与快速交付（全部待实施）

按用户要求，将原 12 个批次合并为 **2 个实现单元 + 1 个集中验收单元**，只维护 DVM-189
一个任务单。不按类、方法、单条问题、文档同步或测试类型另建 WU；A01..A27 全部保留。
合并的是交付边界，不减少正确性要求，也不先设计大框架再补实际调用。

| WU | 前置 | 一次交付范围 | 关闭条件 |
| --- | --- | --- | --- |
| AUD-01 | 无 | 共享音频底座与会话输出：来源/预算、PCM 写入和 STATIC、重采样/混音、OpenSL 生命周期、实时/离线消费、VideoView 音轨及 CLI 下沉 | 下列底座验收通过；所有音源已有同一 session 输出，独立消费及退出可用 |
| AUD-02 | AUD-01 | 应用音频语义与 BootDex：SoundPool/MediaPlayer 隔离及真实状态、流式音乐、AudioManager、三类播放器原版 Java/native 和回调 | 下列 API/BootDex 验收通过；迁移类不保留普通方法/字段双实现 |
| AUD-03 | AUD-02 | 集中交叉验收、过渡代码清理、诊断与真实运行证据、必要文档收尾 | A01..A27 全部有修复证据或不成立裁决，剩余范围外能力如实登记 |

执行顺序固定为 **AUD-01 → AUD-02 → AUD-03**。实现单元内按下述依赖连续推进，不为内部
步骤设置独立任务、重复交接或每步整套构建。遇到真正阻塞才记录新的依赖；不为控制文件数
拆单，不以工作单元数量衡量完成度。

### AUD-01：一次收敛底座与输出

内部顺序：固定 API19 native 行为证据和范围 → 来源 lease/预算 → PCM 写入/STATIC 与
跨块重采样 → OpenSL 预检/对象/事件寿命 → 独立消费与 Clock → VideoView 接入、CLI 下沉。
复用现有解码器、mixer 和 process 回调线程；给当前 Java 入口保留必要窄适配，保证这一
单元可以独立运行，不能先依赖 AUD-02 尚未实现的播放器或 BootDex 字段。

验收合并为直接相关的底座回归：

- resid/APK/FD 区间一致；关闭原 FD、路径替换、空/超限/截断、WAV PCM；读取大文件的小
  区间不得先分配完整文件，所有缓存/队列有总预算。
- 零写、大于 buffer 的分段写、部分返回、多 writer、stop/release/teardown 唤醒、
  STATIC 重播/loop/rate 和 head 回绕；复用双解释器/JNI 回压测试。
- 同一 ramp/sine 按 1/17/1024 帧与整块供给，保持相位/时长及约定舍入容差；
  44.1↔48、8↔48、192→48 kHz；正负抵消不提前削波，VideoView 跨块进度正确。
- OpenSL 超大 size 在分配前拒绝；ABI/IID/vtable 不变；真实 A32 callback re-enqueue；
  destroy/clear/注销/self-destroy、反复创建回收、事件队列上限与错误映射。
- 假 sink 可控消费；长渲染帧不阻塞 PCM；固定 source/事件调度下，相同 step 的离线
  hash/帧数不受真实等待影响；暂停/恢复、设备失败和写入阻塞退出有界。
- 不经 CLI 的 session 也输出全音源；视频 seek/暂停/EOF、无音轨、多 view；
  CLI 不检查 video_views、不实现 source 切片；共享 demux 无竞争读取。

### AUD-02：一次完成应用协议与 Java 迁移

内部顺序：池/播放器资源身份和真实状态 → 增量音乐解码/控制 → AudioManager 会话事实
→ AudioTrack、SoundPool、MediaPlayer 原版闭包与 native 适配 → 统一原版事件分派。
每类后端和 Java 迁移连续完成；不先补一套完整 C++ Java 模拟再删除重写。AUD-01 已提供
独立消费，原版 Handler 回调中的阻塞 write 不再等待同一个消费线程。

验收合并为 API/native 与 BootDex 回归：

- SoundPool priority=1/loop=0 不循环；loop=-1/0/2、双声道音量、rate、maxStreams/抢占；
  双池同源隔离、四个 load 重载、OnLoadComplete、unload/GC/失败取消、自然结束回收。
- 两个 MediaPlayer 同源独立；完整合法/非法状态转换表；prepareAsync 成功/失败；
  完成一次、reset 重设源、播放前/中音量和 loop；长音乐内存有界，随机 seek 对照线性解码。
- AudioManager 音量/静音/活动状态/输出属性一致；静音仍推进；focus 仅回答已实现的会话
  政策。网络、subtitle、DRM、effects 等未支持行为遵循明确失败边界。
- 三类原版构造/控制/释放、创建 Looper/指定 Handler、回调重入、GC 后无旧事件；
  原版 int native token、平台 loadLibrary、普通 JNI 与 OpenSL 路由无冲突。
- 依赖/native/overlay 静态核对按迁移增量执行，稳定后统一 BootDex build/check、全类链接、
  双解释器及直接相关 guest JNI 回归；只在新增变化或失败时重跑。

### AUD-03：集中收尾，不重新扩大实现范围

收掉 AUD-01 的过渡适配和旧 media_* 等重复状态，完成锁/GC/预算、诊断和跨入口核对。
复用前两单元已经通过的证据，不重复跑全套；仅对收尾改动和交叉风险运行定向回归。
真实游戏按相同触发路径做 reached-fault 和必要实听；只有正式 title gate 才三轮复跑。
发现本清单范围内遗漏必须修复或裁决后才能关闭；独立范围外新能力另记缺口，不顺带扩建。
实际能力/契约变化才更新 MODULE、capabilities、CURRENT；不把文档同步另拆工作单元。

## 8. 验证、诊断与文档收尾

优先复用 tests/audio/open_sles_pcm_mixer_tests.cpp、java_sound_pool_mixer_tests.cpp、
tests/dexvm/audio_track_tests.cpp、file_vfs_tests.cpp 的媒体区间用例、tests/video/、
tests/hal/audio_output_tests.cpp、runtime boundary 与 android_guest_call_session 测试。
新增测试关注上述风险，不逐方法机械增用例；音频输出应有帧数、sample、事件序列、内存上限
和超时判据，不能仅检查“没有抛异常”。不要求跨平台浮点逐位一致，容差与参考算法明确。

诊断至少记录：source/voice/player 数、缓存/队列字节、阻塞 writer、待事件数、欠载次数、
mixed/submitted/presented 估计、设备错误与取消状态；走结构化日志/现有快照，guest 地址和
token 强类型化。静音不得使播放位置与完成事件停止。性能评价区分解码、混音、队列和设备，
不把约 85 ms 的当前水位当实测延迟或最终目标。

只构建受影响目标；Windows 使用 windows-msvc。Java/native 行为变更覆盖 switch/threaded
和直接相关 guest JNI/OpenSL；不运行全量 CTest。修运行首错同路径复现一次并记录下一首错；
实际无可用设备/制品时明确缺口，不以离线测试冒充听测或 title 可玩。

能力范围/验证入口变化才更新 capabilities.toml，历史 complete 不任意降级；可以添加本专项
精确定义的新条目并保留旧范围说明，不能用总括 complete 掩盖未闭合项。MODULE 在该批真实
契约改变时更新；CURRENT 只在运行状态/阻塞/里程碑变化时最小更新。制品事实仍以 manifest
为准。规划阶段只做 UTF-8、链接和差异静态检查，不构建、不改生产能力状态。
