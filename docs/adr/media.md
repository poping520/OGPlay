# 图形、音频与视频

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3](#adr-0003)
- [ADR-0019 · 桌面呈现管线与零拷贝方向](#adr-0019)
- [ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载](#adr-0021)
- [ADR-0027 · AudioTrack stream 按构造缓冲字节回压](#adr-0027)
- [ADR-0061 · EGL 对象 registry 与每 Context 图形状态](#adr-0061)
- [ADR-0069 · 音频 Java 协议、宿主执行与会话输出边界](#adr-0069)
- [ADR-0070 · 有界音乐增量解码与音源分流增益](#adr-0070)
- [ADR-0090 · EGL 配置由真实表面格式决定](#adr-0090)
- [ADR-0091 · ATC 纹理使用可移植解码回退](#adr-0091)
- [ADR-0094 · 本地 VideoView 的 Java 生命周期与真实视频事件](#adr-0094)
- [ADR-0099 · 实时视频解码与音频消费隔离](#adr-0099)
- [ADR-0100 · 实时窗口有界异步读回](#adr-0100)

<a id="adr-0003"></a>

## ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3

- 状态：Accepted
- 日期：2026-08-03

### 背景

DEMO 的手写 GLES→桌面 GL 转译产生能力上报、固定管线和跨平台语义问题。

### 决定

GLES 语义交给 ANGLE；guest 边界调用由 IDL 生成。窗口、键鼠和手柄统一使用 SDL3。

### 后果

Windows/Linux/macOS 共用实现，CI 可接软件后端；禁止迁移 DEMO 的 `gl_bridge.cpp`。

<a id="adr-0019"></a>

## ADR-0019 · 桌面呈现管线与零拷贝方向

- 状态：Accepted
- 日期：2026-08-11

### 背景

当前每帧路径是 ANGLE pbuffer 渲染 → `glReadPixels` 全帧回读 CPU → 窗口上传显示。
WU-PERF-04..06 之后的稳定期采样（Dungeon Hunter，macOS Release）显示主线程约 50%
耗在 `glReadPixels`（ANGLE Metal `waitUntilCompleted` + `getBytes` CPU 拷贝），
呈现上传约 10%，guest 执行仅 15%。回读是结构性瓶颈：它强制 CPU/GPU 每帧串行，
且随超采样倍率平方放大。

同时，MCP `frame_capture`、golden SHA-256 断言与 Scenario 证据链都依赖 CPU 像素，
任何零拷贝方案必须保留机器可判定的回读路径。

### 决定

1. 桌面窗口呈现统一经 SDL_Renderer 流式纹理上传与 GPU 缩放合成（WU-PERF-06 已
   落地）；禁止回到 CPU surface blit。
2. 长期方向：ANGLE EGL window surface 直接渲染，普通帧不再经过 CPU；回读收敛为
   按需路径（MCP capture、golden 断言、`--exit-after-frames` 证据），保持机器可
   判定测试不变。该工作量为里程碑级，需解决 SDL3 窗口与 EGL surface 的生命周期
   绑定、超采样 blit 与 resize，单独立项排期。
3. 在 2 落地前允许的中间优化：PBO/fence 异步回读（一帧延迟换取解除每帧
   `waitUntilCompleted` 串行）、按 present 需要降频回读。任何中间优化不得改变
   capture/golden 语义。

### 后果

- 呈现语义（黑边、等比缩放、present 计数）由 hal 契约固定，与实现路径解耦。
- 回读成为显式的证据路径而不是呈现路径的一部分，为窗口 surface 迁移铺平接口。
- 在 2 未完成期间，回读仍是帧率上限的主要因素，性能基准应分别记录
  「含回读」与未来「直接呈现」两组数据。

<a id="adr-0021"></a>

## ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载

- 状态：Accepted
- 日期：2026-08-12

### 背景

多款目标游戏在启动或过场时通过 `android.widget.VideoView` 播放本地视频文件
（已核实样本为 MPEG-4 Part 2 + AAC 的 mp4 容器）。当前 `android.videoview.*`
intrinsic 是记账桩：`setVideoPath` 丢弃路径，`start()` 立即回调 `onCompletion`，
表现为一段黑屏后直接进入下一个 Activity。VideoView 是游戏进程直接调用的能力，
落在 OGPlay 范围内（ADR-0001）；伪造完成违反“未实现能力必须诚实失败”的纪律，
但对该能力的正确前进方向是真实解码，而不是继续记账。

约束：

1. 编解码需覆盖 mp4v/H.264 + AAC，且跨 Windows/Linux/macOS 三平台。
2. 构建与 CI 不得引入新的编译期依赖；无解码器的机器上测试必须全绿。
3. 播放位置必须由统一 Clock 推进（ADR-0004），manual-step 与 Scenario 下可复现。
4. 线程模型保持一个 guest 线程对应一个宿主线程；解码内部线程不得回调 guest。

### 决定

1. 新建同层模块 `src/video`（与 audio/input 同层），定义拉模型 `VideoPlayer`
   接口：调用方持有位置时钟并轮询取帧/取 PCM，实现内部可以有工作线程但绝不
   向外回调。`onCompletion` 等 guest 回调由 guest 循环在轮询到结束事实后自行
   触发。
2. 真实解码后端选 **FFmpeg**（avformat/avcodec/swscale/swresample）,以
   **运行时动态加载**（`LoadLibrary`/`dlopen`）接入：构建期零依赖，符号在首次
   使用时解析，可用性可查询。LGPL 动态链接合规；共享库随发行版放在可执行文件
   旁，分发方式仿照 ANGLE 预编译产物（ADR-0014）。
3. 回退语义：FFmpeg 缺失、文件缺失或解码失败时，videoview 走结构化 warn +
   capability 记账 + 立即 `onCompletion`（即改造前行为）。不伪造播放进度，
   不静默吞错误。
4. 测试基线用确定性的 `FakeVideoPlayer`（合成帧与 PCM）承担全部行为测试；
   FFmpeg 后端只保留可用性探测测试与检测到共享库才运行的可选 smoke 测试。

### 后果

- 视频画面经 boundary 的软件帧发布进入既有帧存储与呈现管线（ADR-0019 的回读
  与 golden 语义不变），音频 PCM 混入既有音频输出;两条路径都不新增平台 API
  调用点。
- CI 与开发机不装 FFmpeg 也能验证全部行为逻辑；真实解码质量依赖本地放置的
  共享库,属于发行装配问题而不是构建问题。
- FFmpeg ABI 随大版本变化,加载器按平台常见的库名/版本号列表探测；不在探测
  列表内的版本表现为“不可用”并走回退,不做半兼容。

<a id="adr-0027"></a>

## ADR-0027 · AudioTrack stream 按构造缓冲字节回压

- 状态：Accepted
- 日期：2026-08-29
- 关联：[ADR-0023](diagnostics.md#adr-0023)、
  [ADR-0025](diagnostics.md#adr-0025)、
  [DVM-93](../tasks/dexvm/DVM-93.md)

### 背景

API 19 `AudioTrack.write` 明确规定 MODE_STREAM 会阻塞直到数据全部写入 audio sink；JNI
`writeToTrack` 同样调用 native `AudioTrack::write`。OGPlay 的 legacy 路径却把 mixer 的
255 项队列当作 Android 缓冲：满时抛宿主 C++ 异常；DexVM 路径则返回 0。前者可让异常越过
Java 边界并终结 native audio worker，后者也没有 Android 的阻塞语义。以 4096-byte write
为例，255 项还会积累约 1 MiB，而 guest 构造时请求的 35280-byte buffer 只有约 0.2 秒。

### 决定

共享 `OpenSlesPcmMixer` 提供可中断的 blocking enqueue，并以每个 AudioTrack 构造时的
`buffer_size` 作为未消费 PCM 字节上限；首 buffer 已播放的 frame 不再计入积压。原有 item
capacity 只保留为有界内存护栏，不再定义正常回压点。播放推进、clear 和 player destroy 都
唤醒 writer；不使用墙钟 sleep、轮询步进或固定重试预算。

legacy Java/JNI 与 DexVM MODE_STREAM 统一使用该原语。Legacy 在等待期间不持 media state
mutex；DexVM 在等待前释放全部 `VmExecutionLock` 深度，唤醒后恢复并重新验证 track/player
identity。MODE_STATIC 保持一次复制语义。release/销毁或 teardown 中断返回
`ERROR_INVALID_OPERATION`，队列饱和不再抛宿主异常或返回 0。

`BeginTeardown()` 与 process Stop 对 audio wait 发布粘性中断，再进行既有 futex/monitor
唤醒。阻塞发生在 JNI/HLE 边界内部，不消耗 guest tick；完成的 JNI 重入仍沿 ADR-0023 作为
既有 advanced 进展，不新增按轮询次数续期的类别。

### 后果

producer 由真实 mixer 消费速率节流，稳态未消费字节不超过构造 buffer，短音效不再排在数秒
PCM 后面；队满也不会杀死 guest audio worker。位置回调中的重入 write 仍可工作，但和真机
一样只有在播放已释放足够字节时才返回；测试不得依赖无限队列。共享 backend 暴露 queued
bytes 与 blocking-writer 数，legacy snapshot 另记录成功 write 次数、非零 write、sample peak
和当前积压，供机器验收与后续受控诊断投影复用。

<a id="adr-0061"></a>

## ADR-0061 · EGL 对象 registry 与每 Context 图形状态

- 状态：Accepted
- 日期：2026-09-14
- Supersedes：`src/runtime/boundary/MODULE.md` 中“进程唯一 GuestGlContext、唯一 ANGLE
  surface/context”设计；ADR-0003 的 ANGLE/SDL3 选型保持不变。

### 背景

Native EGL 虽已有 Context/Surface 句柄表，但所有句柄仍落到同一 `GuestGlContext` 与
`AngleFrame`。不共享 Context 因而错误地共享对象名和状态，share context 只保存元数据，
draw/read surface 也没有独立 backing。单一全局线程 owner 还会拒绝两个线程分别绑定不同
Context 的合法关系。

### 决定

进程只保留一个权威 EGL registry。registry 分别拥有 display/config、Context、Surface 和
share group；每个 Context 拥有独立 guest 状态与 ANGLE context，Surface 独立拥有 backing。
纹理、buffer、renderbuffer、shader/program 等对象由 ANGLE share context 共享，binding、
viewport、错误锁存、固定管线矩阵和 transfer state 均按 Context 隔离。current 关系按 guest
线程保存，同一 Context 同时只能属于一个线程；失败绑定不得改动调用线程原绑定。

Java EGL10/EGL14 只保存 Java wrapper 与 native registry handle 的映射，不建立第二套对象、
current 或 error 事实。直接 ELF import 保留 SONAME API family；proc-address 返回稳定
forwarder，调用时根据调用线程 current Context 的 client version 分派。

### 后果

宿主可继续串行执行 GL 命令，但不能再用进程级 owner 表示 EGL 合法性。managed surface 是
registry 中由 lifecycle 创建的 window 类对象；SDL presentation 与 FrameService ownership
不变。Context/Surface 的实际 ANGLE 引用直到 pending destroy 且不再 current 才释放；
Terminate 清理线程绑定并允许随后重新初始化。

<a id="adr-0062"></a>

## ADR-0062 · API 19 GLES3 边界与扩展发布规则

- 状态：Accepted
- 日期：2026-09-14
- Supersedes：[旧 EGL/GLES 设计](../design/boundary/03-egl-gles-api19-completion.md)
  中“不扩展到 GLES3”的范围；ADR-0003、ADR-0061 的 ANGLE 与 registry 决策保持不变。

### 背景

Android 4.4.4 已公开 GLES30，AOSP `GLES3/gl3.h` 相比 GLES2 增加 104 个 core 入口。当前
ANGLE lifecycle 可创建 ES3 Context，但 guest catalog、A32 宽参数、Native handler 和 Java
GLES30 尚未形成一致调用面。直接透传宿主版本或扩展字符串会声明 guest 无法执行的能力。

### 决定

`libGLESv2.so` 同时承载 GLES2 core 与 GLES3 新增 core，调用按当前 EGL Context 的 client
version 校验。104 项新增入口使用独立声明式 catalog，保留 `GLint64`、`GLuint64`、`GLsync`
和二级指针形状；A32 调用按 AAPCS 对齐重建 64 位值，sync/map 返回值必须使用受检 guest
identity，禁止暴露 host 指针。Java GLES30 复用同一 catalog、Native handler、EGL registry
和 `GuestGlContext`，只负责 Java 数组/NIO/long 的准确编组。

本 WU 的扩展发布清单冻结为空：API 19 GLES3 core 不依赖额外扩展即可闭合；现有 GLES1
的 OES 子集与 GLES2 的压缩纹理能力保持原清单。后续扩展必须逐项具备 guest thunk、编组、
真实行为与失败测试后另作 ADR 追加，ANGLE 导出符号或驱动字符串本身不构成发布依据。

### 后果

ES1/ES2 Context 继续只接受各自版本允许的调用和版本字符串。ES3 完成前能力保持 partial；
catalog 名称完整不能替代 Native/Java 行为验收。proc-address 可为已接通的 ES3 core 返回稳定
forwarder，并在调用时按当前线程 Context 版本路由。


<a id="adr-0063"></a>

## ADR-0063 · EGL 对象独立所有权与受检扩展

- 状态：Accepted
- 日期：2026-09-14
- Supersedes：ADR-0061 的 Context/Surface 组合 backing；ADR-0062 的新增扩展空清单。
- 依据：[BND-34](../tasks/boundary/BND-34.md) 与当前代码审计。

### 决定

一个 guest Context 对应一个 eager native Context，一个 Surface 对应一个独立 native pbuffer。
Display 资源被两者共享持有，native display 的 initialize/terminate 成对引用计数。MakeCurrent
只绑定现有对象的 draw/read surface；Context 共享在创建时建立，不依赖首次绑定顺序或共享源
句柄后续存活。固定管线和可编程 shadow 按 Context 保存，共享对象元数据按 share group 持有。
最后一个 Context 退役时回收该组 guest map/sync identity，释放 current 不等于销毁对象。

GLES2/3 的 extensions、indexed strings 和数量查询使用同一受检清单。新增 EGL KHR sync、
reusable sync、wait sync、GL texture/renderbuffer image 只在实际 ANGLE 后端支持时发布；
GL_OES_EGL_image 使用 guest image identity，禁止将 guest 指针转为 host image。
GLES1 matrix palette 补齐四个标准入口，CPU 只做有界数组搬运与加权顶点/法线变换，绘制仍由
现有 ANGLE shader 完成，不引入 GLES 到桌面 GL 转译。32 个 palette matrix 与最多 4 个权重
按 Context 保存，数组可来自 guest RAM 或共享 VBO。

pbuffer 的 EGL texture 绑定及 mipmap 属性调用真实 ANGLE；swap interval 传给 ANGLE。
窗口 swap 从 draw surface 默认 framebuffer 取帧，然后恢复原 read surface/FBO，交给现有
SDL 呈现链。此实现不声称控制桌面合成器的实际垂直同步时刻。

### 范围和后果

目标仍是游戏进程兼容层，不是完整 Android 图形系统。Pixmap/OpenVG client buffer 不属于
当前 EGL config 支持面，继续返回明确 EGL error。Android native-buffer、native-fence FD、
presentation-time 与厂商扩展全集没有可用的 guest 系统对象契约，本次不发布、不伪造成功；
后续必须按真实游戏调用和明确对象契约追加实现。Windows 当前 D3D11 ANGLE 实测没有
EGL_KHR_fence_sync，但有 EGL_KHR_reusable_sync；前者不能因入口存在就宣告可用。

接口目录完整与定向回归通过均不是 Android CTS/Khronos conformance 认证。完整性账本保留
整体 partial 状态；本任务只记录逐项实现和机器可验证的行为，未提供的 Android SO 包也不能
声称完成了 ELF dynsym/ABI 全量比对。

<a id="adr-0069"></a>

## ADR-0069 · 音频 Java 协议、宿主执行与会话输出边界

- 状态：Proposed（开发规划，未实施）
- 日期：2026-09-19
- 关联：[音频审计与规划](../design/dexvm/16-audio.md)、[DVM-189](../tasks/dexvm/DVM-189.md)
- Supersedes：实施后替代 ADR-0027 将所有 release/销毁中断统一返回
  ERROR_INVALID_OPERATION 的条款：分段 write 已接收部分数据时保留实际长度；
  未接收数据时按 API19 精确返回。保留构造字节预算、释放 VM 锁及 teardown 唤醒原则。

### 背景

现有宿主 PCM 与 SDL3 方向可复用，但 Java 音频入口含参数误读、状态副本及空操作；
编码音乐复用 SoundPool big-bank，实例和资源寿命混淆；CLI 拥有资源读取、视频音轨组合
和按图形帧补音频。重采样、回调退役、设备队列与 guest Clock 也需统一审查。

### 决定

1. AudioTrack、SoundPool 及必要值类采用固定 API19 BootDex，MediaPlayer 在真实 native
   播放器和依赖闭包闭合后迁入。普通字段/校验/Handler 归 Java；native 资源状态归宿主。
   AudioManager 保留有界会话 facade。准入类不代表支持其全部能力。
2. 音频模块提供共享 PCM/解码/混音能力，runtime 提供资源 lease、guest token 和事件边界；
   session 统一所有音源、输出策略和生命周期；frontend 仅创建/注入设备与启动会话。
3. 短音效缓存与音乐流式 source 分开，共享不可变数据但不共享播放器身份；统一宽精度
   累加后最终限幅。OpenSL 保留 A32 公共 ABI 和专用 guest 回调线程。
4. 实时消费不依赖图形帧；确定性模式由统一 Clock 决定帧数、写离线 sink，不由 SDL 水位
   推进 guest。事件按 owner generation 退役；设备线程不直接执行 guest 或读取 Java 侧表。
5. 保持唯一 VFS/Clock、SDL3 和受控线程模型，不引入 Android 音频服务、录音、DRM 或
   厂商 HAL。未支持的 native 行为按 API19 错误协议明确失败，不允许空成功。

### 后果

实施按 DVM-189 的有依赖批次进行；先建立独立消费再迁移可能阻塞的原版 Java 回调。
Java 迁移、native 状态修复、调度改变分别验收；实际契约改变时更新相应 MODULE 和能力入口。
Proposed 不代表已替代现行实现，也不表示性能、听感、CTS 或游戏兼容已经验收。

<a id="adr-0070"></a>

## ADR-0070 · 有界音乐增量解码与音源分流增益

- 状态：Accepted
- 日期：2026-09-20
- 关联：[DVM-189](../tasks/dexvm/DVM-189.md)
- Supersedes：ADR-0069 第 3 条音乐流式 source 的实施选择；不宣称该提案其余条款全部验收。

### 决定

复用固定 stb_vorbis/minimp3 和 PCM WAV parser，不增加第三方依赖。编码音乐保留有界
不可变输入窗口，每播放器独立 bitstream 游标与固定 PCM 块；不将完整解码 PCM 用作音乐
播放底座。OGG 使用 seek/read，MP3 先扫描帧头获取时长，播放时逐帧解码，WAV 直接按帧读。
MP3 后退 seek 重新解码并丢弃前序输出，保证与线性解码一致，暂不增加不精确的近似 seek。

音乐实例和输入总量在准入时限额，Vorbis 使用固定 codec arena。异步准备任务归实例所有，
最多一个任务；reset/release/重设源取消并 join，不使用 detached 线程。同步 prepare 也复用
此路径，任务仅生成 decoder，不访问 VM、mixer 总锁或 guest 回调。事件只在生命周期线程发布。

AudioTrack/MediaPlayer 按播放器保存 stream，SoundPool 按池保存 stream；AudioManager
将音量/静音下推 native mixer，与实例音量相乘。VideoView/OpenSL 默认 MUSIC，不把 MUSIC
音量乘到最终总输出。设备或 mixer/callback 致命异常统一先中断 producer，再交还主循环。

### 后果与边界

短音效仍可全量缓存；音乐 PCM 占用不随歌曲时长增长，但编码窗口仍在内存，不支持无限输入
或网络流。MP3 远距离 seek 为线性工作量，未声称实时延迟或无欠载；未做游戏听测。
MediaPlayer 阶段与本地资源状态归 native，BootDex 保留 Java wrapper/Handler 协议。
门禁覆盖跨块/随机 seek PCM 对照、超过旧 PCM 上限的音乐、任务取消/预算、分流静音和
错误唤醒；不把定向测试当作 title gate 或 CTS。

<a id="adr-0090"></a>

## ADR-0090 · EGL 配置由真实表面格式决定

- 状态：Accepted
- 日期：2026-10-02

### 背景

`eglChooseConfig` 颜色位数是最低要求，不能成为配置的实际属性。只提供 RGBA 的驱动
也不能仅改报 alpha=0 来满足 RGB chooser，否则默认 framebuffer、混合和读回语义不一致。

### 决定

保留有界的 RGBA 与 RGB888 配置身份；属性查询、选择、Context 和 Surface 共用 registry。
宿主直接提供 RGB config 时使用原生配置；Metal 的 RGB 存储由 HAL 管理 IOSurface，
经 ANGLE 客户端表面以 GL_RGB 导入。OS 资源只归 HAL，GLES 与 alpha 语义仍归 ANGLE，
不增加游戏分支、GLES 到桌面 GL 转译或硬件到软件的静默切换。原生错误保留，缺少实际
backing 时不发布 RGB。当前不发布 RGB565，不承诺完整 EGL/Android 窗口系统。

Metal 导入使用固定 SDK 已有的 RGBX 路径；ANGLE 自行初始化 alpha 为 1 并关闭 alpha 写入。
依据：[固定 ANGLE 源码](https://github.com/google/angle/blob/c24d9971269a878a221238a5923abdcc933fa2e9/src/libANGLE/renderer/metal/IOSurfaceSurfaceMtl.mm)。

### 后果

Java/native config 查询稳定且不依赖调用者的上一次筛选。Surface 独立持有客户端存储，
先销毁 ANGLE surface 再释放宿主存储；沿用 current/destroy/terminate 的既有生命周期。
RGB 客户端表面不发布 texture binding，非法组合返回 EGL_BAD_MATCH。跨宿主策略统一，
每个实际后端仍须分别验证，不以 macOS 测试代替 Windows/Linux 验收。

<a id="adr-0091"></a>

## ADR-0091 · ATC 纹理使用可移植解码回退

- 状态：Accepted
- 日期：2026-10-02

### 背景

旧 GLES 游戏可能固定加载 ATC 资源，宿主 ANGLE 后端不一定支持其格式。仅声明扩展
不能提供实际上传能力，压缩格式查询也不能忽略软件回退。

### 决定

固定 AMD Compressonator 的可移植 C 颜色解码源与许可证，以薄适配层处理三种 ATC
格式的 alpha、字节序、块布局、精确长度及有界 RGBA8 输出。只构建独立译单元，不引入
完整 SDK。ATC 始终经同一 CPU 路径交给 ANGLE，避免宿主支持差异；既有 ETC1/PVRTC
策略保持。原生格式列表与已实现软件格式去重合并，计数、查询长度及各标量查询共用。
GLES1/2 发布 ATC 扩展，PBO 通过现有受检 Buffer 回读，禁止作为 guest 指针访问。

### 后果

RGBA8 增加存储成本，单次解码受搬运预算约束。ATC 禁止的子图操作返回真实 GL error；
未知格式仍明确失败。上游源码/头文件/许可证固定哈希，算法差异只进入适配层。
参考向量、实际纹理采样和原 APK 首错复现分别记录；平台实跑与 title gate 独立验收。


<a id="adr-0094"></a>

## ADR-0094 · 本地 VideoView 的 Java 生命周期与真实视频事件

- 状态：Accepted
- 日期：2026-10-04
- Supersedes：ADR-0021 中解码失败时回退到 completion 的条款；FFmpeg pull 后端保持。

### 背景

仅增加监听器方法不能保证 prepared、播放目标、资源 URI 和回调播放器身份正确。
旧回退会把打不开的视频伪装成播放完成，掩盖资源或解码缺口。

### 决定

以 API19 VideoView 协议为依据，提供进程内 Java SurfaceView 子类，保存监听器、目标
状态及 generation。初始化的 MediaPlayer 子类把所需控制转发到同一视频后端；其空音频
实例按既有 MediaPlayer 生命周期释放。原始 MediaPlayer 音频路径不变。
仅支持 VFS 本地路径、file URI 及当前 APK 的 STORED android.resource URI；读取使用
既有不可变 range/read lease。每个进程至多八个视频实例，沿用解码器元数据及缓存预算。
prepared/error/completion 由 guest 主线程视频泵派发；替换、释放和 detach 取消旧代际，
Java 回调期间不持有视频锁。解码或打开失败交付 error，未处理错误明确失败并记账，
不伪造 completion。网络源、字幕、MediaController 和系统错误对话框不在范围。
画面按 UiTree 附着、可见性、位置、父裁剪、alpha 及 SurfaceView onTop 事实合成；
视频 PCM 与原有混音共享 Clock，按实例应用左右音量。监听器普通 Java 字段由 GC 追踪，
宿主播放器随 owner sweep 回收。

### 后果

不引入 Binder、系统媒体服务或宿主平台专属分支。真实 APK 首错复跑仅为 reached-fault；
完整游戏、影音同步及各宿主运行验收另行记录。

<a id="adr-0099"></a>

## ADR-0099 · 实时视频解码与音频消费隔离

- 状态：Accepted
- 日期：2026-10-06

### 背景

同步 VideoPlayer 在主帧和音频 worker 共享的视频锁内 demux/解码。视频积压背压会使
实时音频缺 PCM，固定步进时钟在帧耗时超标时慢放；被视频完全覆盖的 GPU 底图仍读回。

### 决定

沿用 FFmpeg 7 同步后端，为实时 frontend 注入有界 buffered wrapper；手动步进仍用同步
后端。内部 worker 独占 decoder/AVIO，拉取只复制已解 PCM/移动 RGBA。open/seek 预读
250 ms；PCM 水位 500 ms，额外 chunk ≤10 ms。RGBA ≤64 帧且 ≤64 MiB，积压时丢最旧
图片而不丢 PCM。无音轨或音轨 EOF 后按画面请求位置有限预读；错误经拉取向原 error
事件链传播。seek/析构先停止并 join，不留旧代际或借用来源。

实时视频位置仍来自统一 Android Clock；步间真实耗时补到 16..100 ms，扣除该步已显式
推进的 guest 时间，暂停恢复重置锚点。手动步进与无视频游戏维持原固定时钟。
全屏不透明视频由 session 按已解析 UiTree 及祖先裁剪认证，经显式 callback 允许 boundary
跳过仅用于 host present 的读回。回调 try-acquire VM 锁，脏布局/忙时不能跳过；不持有
FrameService 锁回调。图形逻辑继续执行；新视频帧发布基帧并在原 handoff 合成一次。

封存 APK 的 STORED 媒体在捕获来源时校验全部元数据及 CRC 一次，借用不可变 payload
区间读取，不再对每个 32 KiB AVIO 请求重新扫描完整视频。API 要求调用方保活且不修改
APK；Deflate 保留原窗口校验。损坏来源在打开阶段明确失败，禁止跳过首次校验。

### 后果

有限预读增加启动和 seek 成本；真实解码供给不足仍可产生临时 PCM 欠载，不能伪造成功。
视频滞后时跳过过期图片是实时同步策略，不计作重复 present。完整影音同步、硬件解码、
任意分辨率实时性能及跨宿主验收另行验证，不引入 title 分支或 Android 系统媒体服务。

<a id="adr-0100"></a>

## ADR-0100 · 实时窗口有界异步读回

- 状态：Accepted
- 日期：2026-10-06

### 背景

Metal 呈现读回在当前 GL 线程提交 PBO 后立即同步 map，GPU 完成前下一帧 guest CPU
不能推进。仅在下一次 swap 取旧帧会重复首帧，且 WHEN_DIRTY/暂停后的末帧可能不交付。

### 决定

显式实时呈现选项仅对具有实际 PBO/map/EGL KHR fence 能力的 Metal 后端启用；
默认窄接口、guest glReadPixels/ReadRgba8 及手动步进保持同步。首帧仍同步发布当前
像素，之后 producer 在原 guest GL 线程向两块私有 PBO 提交读取、fence 和 glFlush。
每槽 ≤64 MiB；两槽都在用时背压，禁止覆写未消费数据。

内部独立共享 context 的宿主 collector 等待 fence、只读 map/copy 并发布完整 RGBA，
不调用 guest、不更改其 context/state，不在完成等待时占用 producer 锁。它不是 guest
Context 的第二个 native Context，不加入 guest registry。独立收取让没有下一次 swap
的末帧也可交付；实时 frontend 在暂停/空闲时仍可取回已完成帧，且不推进 guest。

pack binding/alignment/row/skip/reverse 全部恢复；上下方向及 supersample resolve 保持原
算法。native context/surface/尺寸改变时重启管线；软件帧、覆盖省读回及关闭使来源代际
退役，旧完成不能覆盖新输出。Stop 唤醒背压、有限 fence 等待并 join，在 collector 所属
线程释放私有对象/context；错误在下一次提交或取帧传播，不伪造成功。

### 后果

增加最多两帧 PBO 存储及一个宿主共享 context，显示可延后约一帧。glFlush 不保证完成，
GPU 本身满载时仍有等待；CPU 拷贝/SDL 上传成本不消失。必须以实际成功 present 和等待
采样验证收益，不以 swap 请求、队列预热或重复旧帧计数证明 FPS。

<a id="adr-0106"></a>

## ADR-0106 · RGB565 真实颜色存储与默认 framebuffer 映射

- 状态：Accepted
- 日期：2026-10-08
- Extends：ADR-0090（其 RGB888 路径不变）

精确颜色 chooser 需要 5/6/5/0 配置，最低筛选不能将 RGBA/RGB888 改报为 RGB565。
优先使用宿主 native config；固定 ANGLE Metal 不发布 RGB565 config，但可通过
EGL_ANGLE_metal_texture_client_buffer 导入真实 B5G6R5 纹理并用 OES EGLImage 附着。
HAL 拥有 Metal 资源，ANGLE 拥有 GLES 操作；device 从 EGL 查询，绝不接收 guest 指针。

Metal fallback 使用 carrier pbuffer 建立 currency，以 imported RGB565 attachment 作为
唯一 guest 默认颜色存储。Surface 拥有 image/texture，Context 分别导入 draw/read，
不同 share group 也能看到同一存储。私有 framebuffer namespace 与 guest 隔离；查询
返回 guest 身份，默认 framebuffer 不接受附件修改。临时导入完全就绪后提交，失败
恢复原绑定/currency。真实 framebuffer bits、读回量化和 opaque alpha 决定发布能力。

fallback 限 GLES1 compatibility/GLES2，depth/stencil/samples=0，拒绝 texture binding、
mipmap 与 GLES3；其他 host native 路径按实际属性。缺少真实支持则不发布，不提供
临时 RGBA chooser fallback、游戏分支或隐式切换软件。完整 FBO/驱动矩阵仍需分宿主验收。

来源：[固定 ANGLE ImageMtl](https://github.com/google/angle/blob/c24d9971269a878a221238a5923abdcc933fa2e9/src/libANGLE/renderer/metal/ImageMtl.mm)，
[Metal texture client buffer 扩展](https://github.com/google/angle/blob/c24d9971269a878a221238a5923abdcc933fa2e9/extensions/EGL_ANGLE_metal_texture_client_buffer.txt)。

<a id="adr-0110"></a>

## ADR-0110 · Metal packed 表面的深度/模板存储归 Surface

- 状态：Accepted
- 日期：2026-10-09

在已有 B5G6R5 EGLImage 之外，Surface 独立拥有 HAL Metal depth/stencil texture 与
ANGLE EGLImage。使用 ANGLE 的 GL_DEPTH24_STENCIL8 图像导入/逻辑格式；实际硬件
纹理为 Depth32Float_Stencil8，查询深度/模板位数以 ANGLE 导入后真实 GL 结果为准。
只在临时 Context 的 FBO 完整且位数为 24/8 时发布 D24S8；失败仍保留已受检的 D0/S0
颜色支持。不能只修改配置查询，也不能把深度资源放进每个 Context 而丢失跨 Context
的 Surface 内容。非共享 Context 各自导入同一存储为私有 draw/read FBO 附件，名称隐藏。

Surface lifetime/current/deferred destroy 继承 EGL registry；深度/模板和颜色一起绑定，
失败仍保留旧 binding/currency。纹理绑定、MSAA 与 packed fallback GLES3 边界不扩充，
原生配置仍优先；不更改 ANGLE SDK 或引入系统窗口/图形服务。
