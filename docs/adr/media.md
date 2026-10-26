# 图形、音频与视频

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0003 · ANGLE 图形、SDL3 窗口输入与 SDK 交付](#adr-0003)
- [ADR-0063 · EGL 对象独立所有权与受检扩展](#adr-0063)
- [ADR-0070 · 音频协议、宿主执行与会话输出](#adr-0070)
- [ADR-0136 · 裁剪版 FFmpeg 预编译共享库直接链接](#adr-0136)
- [ADR-0140 · 呈现、证据读回与 Surface 交付所有权](#adr-0140)

<a id="adr-0003"></a>

## ADR-0003 · ANGLE 图形、SDL3 窗口输入与 SDK 交付

- 状态：Accepted
- 日期：2026-08-03

### 背景

手写 GLES 到桌面 GL 转译容易产生语义和能力上报偏差，完整 ANGLE 构建链也不宜下放消费端。

### 决定

GLES 语义交给 ANGLE，guest 边界由声明式目录生成；窗口、输入与设备输出经 SDL3/HAL。
禁止恢复手写逐函数 GLES→桌面 GL 转译；软件设备仍通过 ANGLE 后端运行。
普通开发消费 third_party/angle-prebuilt 的固定浅 submodule，不拉取 ANGLE 源码。
SDK 含公共头、链接库、运行时、许可证、源 commit、GN 参数及逐文件大小/SHA-256。
CMake 按宿主平台/CPU 显式匹配并验证清单，共享头与平台包必须同 commit；运行库随发行物暂存。
缺包、错平台、清单或哈希不符明确失败，不静默转系统 EGL、源码构建或软件后端。
上游构建、打包与发布工具归独立预编译仓库，维护者工作区不进入消费依赖。

### 后果

桌面后端共享图形边界；硬件、软件及不同宿主的能力须分别验证，SDK 升级承担来源和工具链维护。

<a id="adr-0063"></a>

## ADR-0063 · EGL 对象独立所有权与受检扩展

- 状态：Accepted
- 日期：2026-09-14

### 背景

合法的多 Context/share/draw-read 关系不能用进程唯一图形对象或驱动扩展字符串代替。

### 决定

进程唯一 EGL registry 拥有 guest identity；每 Context 对应一个 native Context，
每 Surface 独立拥有真实存储，Display/config 由它们共享持有，initialize/terminate 成对计数。
Context 创建时建立 share group；对象内容共享，binding、固定管线、错误等状态按 Context 隔离。
current/bound API/EGL error 按线程保存，同 Context 不得跨线程抢占；失败绑定保留旧 currency。
pending destroy 到不再 current 才释放；最后 share member 退役时清理 guest map/sync。
Java EGL wrapper、native import 与 proc-address 共用 registry；稳定 thunk 按实际 current 版本路由。
GLES1/2/3 与扩展只发布 ABI、搬运、真实后端行为和错误路径闭合的集合；数量、字符串和查询一致。
KHR sync/image 与 OES image 使用受检 guest identity，不把 guest 值当 host pointer。
Pixmap/OpenVG、Android native-buffer/native-fence FD、presentation-time 和任意厂商扩展不发布。

### 后果

配置属性必须由真实 backing 支撑，不能改报颜色/深度位数；目录完整不代表 CTS/Khronos 认证。

<a id="adr-0070"></a>

## ADR-0070 · 音频协议、宿主执行与会话输出

- 状态：Accepted
- 日期：2026-09-20

### 背景

长音乐整段 PCM、无界排队和由图形帧消费音频会放大内存与延迟，也会使阻塞互等。

### 决定

普通 Java 协议由已准入 BootDex 执行；runtime 拥有受检资源/token/事件，audio 提供解码混音，
session 拥有音频消费，frontend 只注入 HAL 输出。设备/解码工作线程不得直接执行 guest。
短 SoundPool 音效可有界全量缓存；音乐以 OGG/MP3/WAV 增量窗口与每播放器独立游标解码，
不缓存整首 PCM。复用固定 stb_vorbis/minimp3/WAV parser；MP3 后退 seek 允许线性重解码。
每实例准备任务归 owner，reset/release/换源先取消并 join；资源经唯一 VFS lease，不反查 host 路径。
AudioTrack STREAM 以构造 buffer 字节作可中断回压，等待释放全部 VM 锁，恢复后复验 owner。
播放/clear/destroy/teardown 唤醒 writer；队列项上限只是内存护栏，不以重试或 sleep 伪造消费。
AudioTrack/MediaPlayer 按实例、SoundPool 按池保存 stream，stream gain 与实例 gain 相乘，
宽精度累加后最终限幅；静音仍消费，不把 MUSIC 增益乘到最终总输出。
实时消费独立于图形帧；手动步进按统一 Clock 驱动离线 sink，背压提前消费从后续额度扣回。
设备/mixer/callback 致命错误先中断 producer，再向主循环传播原错。

### 后果

内存和任务有界，但不承诺任意音乐实时无欠载；不引入 Android 音频服务、录音、DRM 或厂商 HAL。

<a id="adr-0136"></a>

## ADR-0136 · 裁剪版 FFmpeg 预编译共享库直接链接

- 状态：Accepted
- 日期：2026-10-15

### 背景

运行时猜测 FFmpeg 库名和镜像结构布局难以交付，实际 CG 仅需有限容器与编码。

### 决定

独立 FFmpeg 构建仓库固定上游版本，裁剪 mov demuxer、h264/mpeg4/aac 解码及其必要依赖，
不包含 protocol、encoder、muxer、filter、外部库或 GPL/nonfree 组件。
预编译产物为单一 ogplay_ffmpeg 共享库与公共头、许可证、来源/配置/逐文件哈希清单，
经 third_party/ffmpeg-prebuilt 固定浅 submodule 消费，CMake 校验后编译期直接链接。
不按库名运行时探测，不镜像 FFmpeg 结构体；所有输入经自定义 AVIO 与拥有型来源读取。
AUTO 在当前平台无包时构建无视频版本并给原因，ON 缺包则失败；启动校验版本与必要编码。
VideoPlayer 保持拉模型、调用方 Clock 和有界队列；内部 worker 不回调 guest，退出先 join。
未编入视频或音轨编码均明确抛错并按编码记账，不静默丢音轨、不伪造 completion。
新增编码按实际命中扩充独立组件清单并重新发布包。

### 后果

发行物随附 LGPL-2.1 与对应源码信息，允许替换共享库；编解码范围外的 CG 明确失败。

<a id="adr-0140"></a>

## ADR-0140 · 呈现、证据读回与 Surface 交付所有权

- 状态：Accepted
- 日期：2026-10-17

### 背景

屏幕呈现与 SurfaceTexture 消费都需要明确来源、Context、代际与交付寿命，不能用空帧模拟。

### 决定

窗口呈现经 SDL_Renderer 上传/缩放，当前 ANGLE pbuffer 读回仍是生产路径；不宣称已零拷贝。
证据 capture/golden 与 guest ReadPixels 保持同步语义。显式实时 Metal 路径仅在实际
PBO/map/fence 支持时使用两个有界私有槽与共享 collector Context，末帧不依赖下一次 swap。
collector 不调用 guest、不加入 guest registry，Stop 唤醒背压、有限等待并在所属线程释放/join。
Surface 强类型端点区分 managed display/holder 与 buffer-stream producer，不猜屏幕窗口。
进程拥有无 JNI/GL 的有界队列；integration 装配，boundary 经显式窄接口消费。
consumer、producer 引用、排他连接与 latched image lease 分工；身份携 generation/epoch/sequence、
尺寸/格式、源 PTS、crop/transform 和 backing 所有权。single-buffer 必须归还后才能再生产。
Surface.release 只释放 producer 引用，consumer release/GC abandon 队列；native 表不强根保活 owner。
updateTexImage 在合法 current Context acquire/upload/import/bind，成功后原子提交图像与矩阵/时间戳。
RGBA→EGLImage→EXTERNAL_OES 须有真实采样能力，不能以 2D target 或改 shader 冒充。
队列锁内不做 GL、guest 回调、解码或 join；退役撤销旧帧/事件，GPU lease 在合法 Context 清理。

### 后果

队列与 Java 合成帧通路已接入，MediaPlayer/NDK producer 与启动后端选择未接入；Metal
external 不可用。各后端单独验证，不引入 SurfaceFlinger、gralloc、CameraService 或 Binder。
