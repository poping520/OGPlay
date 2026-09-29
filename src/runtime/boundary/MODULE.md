# 子模块：runtime/boundary

## 职责与依赖

将 guest EGL/GLES、NDK Looper/input、log、OpenSL ES 与 libc/libdl 边界绑定到明确的
handler，负责 ABI 搬运和边界状态，不拥有会话生命周期。依赖 gles、loader、memory、cpu、
core 及显式注入的服务；不得依赖 JNI、jni_guest、framework、integration 或整个 session Impl。

| 目录 | 唯一职责 |
| --- | --- |
| `core/` | catalog、A32 call frame、dense thunk、direct binding、fast router、pending fault |
| `services/` | 共享 GuestGlContext/GraphicsBoundaryContext、搬运原语、FrameService 与窄内存接口 |
| `modules/<so>/` | concrete final module 的导出、handler 和私有状态；注册归 module_catalog |
| `facade/` | AndroidBoundaryHle 装配与冷入口；公共 ABI 为 include 下同名头文件 |

共享服务不反向依赖 concrete module。GLES1 fixed/draw 状态显式注入；EGL/GLES 共用唯一
GuestGlContext、ANGLE backing 与 shared shadow。FrameService 拥有帧回收、指标和 trace；
TryTrace 只短锁复制有界记录，busy 不等待 graphics 线程。跨线程 input/readback 必须受锁保护。

## 导出与调用契约

- BoundaryCatalog 是 SONAME、active export、module-local id、dense slot 的唯一事实源。
  AndroidApiRange 在 seal 时过滤，seal 后只读；local id 可不连续且不得充当数组序号。
  synthetic SO 首次发布完整 active dynsym，动态装载不得补写。无导出模块只可作为显式
  loader scaffold，不能分配伪 thunk；历史 Profile 不构成能力声明。
- thunk 从 kBionicHleThunkBegin 起按 4 字节 dense slot 排列；arena 按页分配并封为 RX。
  seal 一次生成 `{export-specific fn, concrete module*}`；fast/slow 共用 handler，热路径
  仅 `PC → slot → binding`，不再按 SONAME/local id/HleRoute 分发或使用共享 mutable PC。
  导出实现在 concrete module，不转发到 façade 的 Invoke*；libc override 同样遵守此约束。
- A32CallFrame 按精确参数数借用 r0-r3，剩余参数一次 bulk read；不得在 handler 逐字读栈。
  GuestPtr/GuestCString 保留 guest identity，禁止转 host 指针；复杂 callback/variadic ABI
  可显式编组。启用 guest-call slice observer 时不得安装 fast hook。
- fast callback 不向 JIT 抛 C++ 异常：按 thread/PC 保存 pending fault，slow consumer 重抛
  原异常。搬运失败保留类别并附 module!symbol、r0-r3、SP、LR、thread；attribute staging
  还报告 descriptor、definition/enable LR。未知地址/SVC 或未绑定函数必须明确失败。
- managed GLES/EGL 是按 API/name/参数数校验 catalog 的冷适配，直接使用同一 binding、
  registry、thread current 和 error；不重复定义 native export 或创建第二份图形状态。
- watchdog 仅将成功 eglSwapBuffers、OpenSL BufferQueue.Enqueue 归 advanced，其余保守
  为 idle；新进展类别必须进集中清单并受测，不能把任意 handled 调用当作续期。
- MSVC C4702 仅在 sealed if-constexpr 模板定义/实例化处局部关闭；全局 /W4 /WX 与明确失败保留。

## 图形共用不变量

- guest 输入在 ANGLE 调用或状态变化前完整预检、搬运；输出先整体预检，成功后一次提交。
  shadow 仅在 native mutation 成功后窄范围更新，不复制整个动态状态容器；reset 恢复规范默认。
- GL 参数错误携精确 GLenum 写入 per-context 首错锁存；glGetError 先取锁存再查 ANGLE。
  负 count/first/stride/imageSize 等为 INVALID_VALUE，不误归 INVALID_ENUM；内存、生命周期、
  内部逻辑错误继续硬失败，不能全局吞 invalid_argument。无当前 AngleFrame 明确失败。
- 同一 Context 的 GLES1/2 共用 buffer/texture、pack/unpack、active unit、framebuffer/
  renderbuffer、viewport/scissor、clear 与共有 capability；library origin 只决定 API 语义。
  texture 按 object/target 保存 base format、generate-mipmap，cube face 归一为 cube target；
  删除清除所有 unit/target 引用。对象名由 ANGLE 唯一生成/删除。
- query 返回真实 ANGLE 或对应逻辑 shadow，shape 明确受检；不得泄漏超采样坐标或猜未知 pname。
  GLES1 version 表示 ES-CM 1.1，GLES1/2 扩展串只发布完整可执行能力，不透传后端扩展全集。
  字符串位于分槽只读 guest 区，不因其他 pname 查询覆盖；GLES1 扩展串保留尾随分隔符。
- client array 保存定义时 buffer binding，在 draw 按 first/count 或实际最大索引预检并上传；
  staging 仅复用容量，每次重读，内部 VBO/EBO 上传后恢复 guest binding。opaque EBO 与
  guest client array 无法确定范围时失败。draw 由 current program/fixed/array 状态决定。
- fixed draw 在成功和异常路径均恢复 programmable program/buffer/VAO/attribute 常量；
  内部对象不写 shared shadow。2D/cube sampler 按实际 stage 生成，禁用 stage 不与其他类型冲突。
- 超采样只换算默认 framebuffer viewport/scissor，用户 FBO 保持 guest 尺寸；倍率创建前校验，
  查询/指标保持逻辑尺寸。readback 按 pack alignment 提交像素行，保留 padding。

## EGL 与 Surface

对象细节见 [EGL](modules/egl/MODULE.md) 与 [ADR-0063](../../../docs/adr/media.md#adr-0063)。
Display/config 是进程事实，Context/Surface 使用单调句柄，保存版本、share、owner、交换间隔和
延迟销毁状态；Context 拥有 native Context，Surface 独立拥有存储，draw/read 可分别绑定。

- per-thread 保存 current、bound API、sticky error；所有入口验证 display/config/type/初始化。
  current 对象到解绑后才销毁；不同 host thread 可拥有不同 Context，跨线程抢占报 BAD_ACCESS。
  GLES fixed/client/VAO 状态按 Context 保存，纹理/VBO 内容按 share group 共享；最后 share
  退役经显式回调清理 map/sync，eglReleaseThread 不清除其他 Context 的记录。
- eglGetProcAddress 冷查 sealed callable，未知扩展返回 null，不改 hot table。查询无需 current；
  返回的稳定 GLES thunk 按调用线程 current client version 路由，无 current 返回零。
  直接 ELF import 仍由 SONAME 决定 API family。eglWaitGL/WaitClient 同步真实 ANGLE。
- KHR sync/image 与 OES image target 使用 guest identity；只发布后端支持的扩展。texture
  pbuffer 使用真实 native binding；swap 保存/恢复 read framebuffer，只发布 draw surface。
  pixmap、OpenVG、native-buffer/native-fence FD、presentation-time 与任意厂商扩展不在范围。
- managed surface 的 open/present/close 严格配对，guest EGL 不得替换/终止；pbuffer 仍有独立
  backing。创建线程保持 GL currency，显式释放后才可由新渲染线程接管，不允许跨线程抢夺。
- 帧经统一 resolve/sequence 发布；仅回收布局匹配的容量，不复用内容/序号。PublishSoftwareFrame
  只接受完整逻辑尺寸 RGBA8。RetireGuestGraphics 永久关闭 guest 图形：后续调用 idle/零返回，
  swap 返回 false 并锁存 BAD_NATIVE_WINDOW；此规则仅用于 teardown。

## GLES 支持边界

完整导出以生成 catalog 为准，core/extension 独立记账；不得误用同名其他 API handler。
细节见 [GLES1](modules/gles1/MODULE.md)、[GLES2](modules/gles2/MODULE.md)、
[GLES3](modules/gles3/MODULE.md)。

- GLES1 matrix 栈按 Context/unit 隔离，列主序后乘，fixed 用有符号 16.16；溢出、奇异矩阵和
  非法参数不得部分提交。current color/normal/texcoord、clip plane、lighting/material/fog、
  alpha-test 与 texture environment 必须被 shader 消费，不能只缓存状态。
- fixed renderer 支持最多两个启用纹理 stage，独立 coordinate/sampler/matrix/base format，
  支持 MODULATE/REPLACE/ADD/BLEND/DECAL/COMBINE，PREVIOUS 为上一 stage 输出。
  优先 stage 自有 array；仅单 stage 且全局唯一有效 array 可通用回退，多 stage 不借用坐标。
  超范围明确失败。light0..7、双面材质、spot/衰减、normalize/rescale-normal 保留实际语义。
- flat 展开以 primitive 最后顶点提供 provoking 属性；DrawArrays/展开不引入 16 位索引上限。
  point-size array 与 distance attenuation/min/max 由 shader 消费。matrix palette 支持受检
  RAM/VBO 加权变换，Context 隔离；范围以 GLES1 子契约为准。
- material 默认只接受 FRONT_AND_BACK；allow_gles1_material_single_face 默认关闭，仅已验证
  Profile quirk 可启用单面状态，reset 不丢策略。clip plane 提交用 modelview 逆转置并拒绝奇异矩阵。
- texture level 0 成功后按对象 GENERATE_MIPMAP 状态真正生成 mipmap；nullable/image-size/
  unpack alignment 受检，ETC1 无原生能力时规范解码为 RGBA8，guest base format 仍为 RGB。
  OES mapbuffer 仅 WRITE_ONLY、有效未映射对象；内容经 0x72000000 起的 32 MiB guest arena
  双向复制，耗尽/搬运/native 错误明确失败，reset 清理。
  Bounds wrapper 独立绑定，client-memory 按 count/size/type/stride 预检，VBO 保存 offset。
- GLES2/3 shader/program 源码、二级指针、名字与多输出先整体校验；编译/链接结果来自 ANGLE。
  active query/info-log 按 bufSize 截断；link 后按 active-uniform metadata 保存 location shape，
  relink/delete 清理。pointer query 返回 guest logical identity；不猜未绑定属性的索引范围。
  compressed/copy/subrange、object query、framebuffer、blend/depth/stencil 均复用同一 ANGLE
  与搬运服务；flush 不触发 present，shader binary 不支持以真实 GL error 表达。

## Android、动态链接、日志与音频

- [Android module](modules/android/MODULE.md) 自有 Looper/input；Activity/assets/queue 强类型
  identity 与 APK reader 显式注入。asset opaque token 随 owner 退役，单 asset 上限 64 MiB。
  window 每次创建新身份，acquire/release 可保留退役后的尺寸查询但不能重新激活；EGL
  create/bind/swap 拒绝失效窗口。只接受会话尺寸/RGBA8 geometry，其余 EINVAL。
  InputQueue 只接收当前 owner 的 SDL 输入，支持所属 Looper ident poll/get/finish/detach；
  callback 明确失败，managed Activity 失效句柄不得回退 standalone 行为。
- libdl 只处理 ABI、逐线程消费式 dlerror 和有界只读返回区；ELF namespace、handle、sealed
  symbol/exidx 由 BionicDynamicLinkHooks 注入。查找失败按 null/-1 表达，不转为 trap。
- liblog 使用固定 API19 surface 和 LogBoundaryContext；内存经 AddressSpace，event tag map
  经注入 VFS reader，tag 存只读页。输出结构化 guest.liblog/[guest]，不访问 host filesystem、
  伪造 logger device 或裸输出；message/guest_log_tag 从同一未移动值构造。
- [OpenSL ES](modules/opensles/MODULE.md) 以 immutable vtable thunk 与防陈旧句柄表实现
  Engine→OutputMix→PCM AudioPlayer；范围外 constructor 返回 FEATURE_UNSUPPORTED。
  唯一 OpenSlesPcmMixer 经 façade 提供 AudioTrack 适配，module 不依赖 integration/façade/HAL。

## 验证入口

`tests/runtime/boundary/{core,modules,integration}` 分别验证 transport、私有状态与跨模块行为；
Looper 另见 `tests/runtime/guest_looper_tests.cpp`。architecture.boundary_hot_path 递归检查
boundary 实现和 TryFastCall；运行证据、未闭合验收以 CURRENT、任务单及 capabilities 为准。
