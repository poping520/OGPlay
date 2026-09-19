# 图形、音频与视频

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3](#adr-0003)
- [ADR-0019 · 桌面呈现管线与零拷贝方向](#adr-0019)
- [ADR-0021 · VideoView 真实播放与 FFmpeg 运行时加载](#adr-0021)
- [ADR-0027 · AudioTrack stream 按构造缓冲字节回压](#adr-0027)
- [ADR-0061 · EGL 对象 registry 与每 Context 图形状态](#adr-0061)
- [ADR-0069 · 音频 Java 协议、宿主执行与会话输出边界](#adr-0069)

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
