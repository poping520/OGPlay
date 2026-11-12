# 音频与视频

媒体子系统把 Android 播放协议、PCM 混音和编码媒体解码分开。
ROM Java 管理公开对象和事件协议，native 适配连接 audio/video，
session 拥有 Clock 驱动的消费与 HAL 输出，前端只提供设备。

## 媒体主路径

```text
AudioTrack native ──────┐
OpenSL BufferQueue ─────┼→ PCM mixer ─────────┐
SoundPool → 短音效解码 ─┤                   ├→ AudioOutputPump → HAL
MediaPlayer → 编码音乐 ─┘                   │
VideoView / MediaPlayer → FFmpeg → PCM ─────┘
                                  RGBA → WindowSystem
```

AudioTrack 和 OpenSL 复用 PCM 底座，但停止和清空语义不同。
SoundPool 适合缓存短音效；音乐使用有界分块解码。
视频解码器输出与位置时钟分离，由调用方决定播放位置。
不能把读取到 EOF 与暂时欠载混为一谈。

## 来源与寿命

EncodedAudioSource 标识资源、APK entry、VFS 路径或字节窗口。
实际读取经拥有型 EncodedAudioDataSource。
媒体模块不解析 APK，也不把 guest 路径反查成任意宿主文件。
VideoDataSource 使用同样的 Size/ReadAt 结构边界。

integration 捕获 FD 起点和长度，创建独立 VFS lease。
调用者关闭原 FD 或替换原路径，不改变已捕获来源。
可写来源在统一预算内快照，只读 backing 可以共享。
取消 token 向读取底层传递，不只在解码完成后丢弃结果。

音乐不保存完整解码 PCM。
读取和解码按块进行，短音效才允许有总预算的完整缓存。
实例、编码窗口、codec 工作区和输出队列分别有界。
这些上限由契约维护，不由某个游戏的文件大小决定。

## PCM 混音

OpenSlesPcmMixer 保存 player 与 queue 状态。
输入支持有界的 mono/stereo PCM8/PCM16。
重采样跨 buffer 保留分数相位，不能每块从零开始。
mono 复制到双声道，多音源用宽累加器相加后统一饱和。

增益由 stream、实例和通道控制组合。
mute 使输出为零，但继续消费来源和推进播放头。
pause 才冻结位置，不能用静音代替暂停。
播放头按已消费 source frame 计，保留 API19 无符号回绕语义。

mixer 控制与 render 共用内部锁。
回调在释放 module/mixer 锁后交付，避免回填 PCM 时锁重入。
诊断只复制计数，不触发混音、消费或 callback。
队列容量是内存护栏，正常延迟由构造 budget 与消费协议决定。

## AudioTrack

STREAM write 通过可中断 enqueue 施加背压。
未消费字节不能超过构造的缓冲预算。
播放、clear、destroy 和进程关闭会唤醒阻塞 writer。
参数与 guest 内存先检查，不能写入部分数据后才发现越界。

STATIC 保存样本，播放不会把数据从队列移除。
STREAM stop 复位 head 并保留队列；STATIC stop 回到样本起点。
flush 在非 playing 时丢弃 STREAM 队列，不伪造新的播放头。
loop、rate 和 stereo gain 共享受检 player 状态。

position callback 从消费事实产生，再交给 guest Handler 路径。
同步回填后暂停本轮过期 periodic 投递，余下事件留到后续安全点。
这样既避免一次 pump 内无限回调，也不丢跨过的周期。
生成、投递与延期分别计量，不能只用最终 callback 数猜测时序。

## OpenSL ES

Virtual libOpenSLES 发布 public ELF surface 和 IID 数据。
对象使用 guest vtable 与防陈旧句柄表。
Engine、OutputMix 与 PCM AudioPlayer 各自表达生命周期。
realize、destroy 和接口查询使用对应状态，不返回万能对象。

BufferQueue 与 AndroidSimpleBufferQueue 共享已定义的播放器能力。
AndroidConfiguration 仅在明确请求时提供，realize 前可设置 stream type。
录音、MIDI、3D、effect 和 URI/FD 解码不由 PCM 路径承诺。
失败按 method/result/thread/LR 输出结构化诊断。

进程 callback sink 用专用 guest 线程、CPU、TLS 和栈运行回调。
回调内 Enqueue 仍通过普通边界，不能直接跳宿主函数绕过 ABI。
异常先保存到进程 owner，再由主循环恢复。
清理先禁止新回调并停止 worker，后退役对象与 arena。

## SoundPool 与编码音乐

SoundPool 的 池 identity、sample 与 voice 保持隔离。
load 真正完成后才发布 loaded 和 load-complete 回调。
play 消费真实样本，按 priority 和年龄进行 maxStreams 抢占。
同源不可变样本可共享，但卸载按池引用回收。

音效支持 loop、左右音量和受检 rate。
autoPause/autoResume 不恢复此前手动暂停的 voice。
release 清空所属池资源，不影响另一个池。
缺 loader 或损坏来源不能标记加载成功。

编码音乐按 OGG/MP3/WAV bitstream 增量消费。
prepare 任务由实例 jthread 拥有，reset/release 先取消并 join。
提交结果前核对任务身份，旧任务不能重新激活已重置对象。
后退 seek 可以重新解码，不承诺常数时间定位。

## MediaPlayer 与视频

ROM MediaPlayer 保存 Java 参数、Listener 与 Handler 协议。
native transport 有明确阶段，Preparing、Error、stop 和 reset 不能随意互换。
异步事件只对应真实完成或真实失败，reset 清理旧待发事件。
视频 Surface 来源必须是有效 ROM Surface 句柄。

真实视频解码使用编译期链接的裁剪版 ogplay_ffmpeg。
所有输入经自定义 AVIO，运行库不提供任意网络 protocol。
后端可用性来自启动自检，缺 codec 报带轨道类型的错误。
不支持时不能静默去掉音轨继续报告完整播放。

VideoPlayer 是拉模型：TakeFrame 接收位置，ReadPcm 消费音频。
seek 同步移动画面和音频游标。
buffered worker 在内部预读有界图片与 PCM，实时拉取不等待 I/O。
队列压力限制领先量，暂时空读保持欠载事实。
解码图片保活自己的底层 buffer，播放器退役不能使在途图片悬空。

最终 RGBA 由同一 WindowSystem 作为窗口图层合成。
视频不直接绕过窗口层覆盖宿主屏幕。
暂停、seek、完成和 Surface 释放各有独立状态。
字幕、网络源和完整系统媒体服务不在当前路径范围。

## Clock、设备输出与关闭

AudioOutputPump 由 session 拥有。
实时 worker 根据设备队列补充 PCM，离线路径按 Clock 差值换算帧数。
不足一帧的余数保留，不能每次 tick 截断后丢失。
媒体位置、已混音、已提交和估计已呈现是不同指标。

手动 step 期间，若主 Looper 的 write 被背压阻塞，独立消费者可先释放 writer。
提前消费量从后续 Clock 额度扣回，不能重复播放。
无许可 step 且无背压时不消费，也不提交设备。
这一路径已实现，完整端到端验收仍须独立记录。

设备 Submit 或水位查询失败后停止继续提交。
worker/mixer/callback 故障先中断 producer，再保存原错交还主循环。
协作关闭期间允许必要消费帮助生命周期结束，随后停止并 join。
不 detach 准备或输出线程，不用退出宿主进程代替资源回收。

## 契约、决定与操作

- [audio](../../src/audio/MODULE.md)、[video](../../src/video/MODULE.md)
- [OpenSL](../../src/runtime/boundary/modules/opensles/MODULE.md)、[session](../../src/session/MODULE.md)
- [媒体任务与预算](../adr/media.md#adr-0070)、[FFmpeg](../adr/media.md#adr-0136)
- [MCP 观察](../playbook/MCP-TESTING.md)、[音视频缺口](../state/KNOWN-ISSUES.md)
