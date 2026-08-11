# 子模块：runtime/integration

## 职责

装配 Bionic、JNI 与 Android native 边界，生成里程碑出口报告；本模块只协调已有能力，
不实现新的 syscall、JNI、框架或文件系统语义。

## 依赖

位于 runtime 顶层，可依赖 framework、jni、bionic、syscall、execution、vfs 及其下层模块。
任何下层模块均不得反向依赖 integration。

## 不变量

- 同一 guest EGL current context 下，GLES1 与 GLES2 入口共享同一个
  `GuestGlContext`；buffer binding、pack/unpack alignment 与 active texture 由
  `SharedGlState` 唯一拥有，library origin 只决定 API 语义。
- texture binding、delete semantics、level-zero base format 与 generate-mipmap metadata
  同样由 `SharedGlState` 唯一拥有；object name 只由同一个 ANGLE context 生成和删除。
- framebuffer/renderbuffer binding、viewport/scissor、clear state 与 GLES1/GLES2 共有
  capability 也只有一份 shared shadow；所有字段仅在对应 ANGLE mutation 成功后提交。
- GLES1 fixed draw 通过显式 native transaction 临时使用内部 program/buffer/attribute；
  成功和异常返回前均恢复 guest programmable state，internal object 不写入 shared state。

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- Android guest call session 只在通用 A32 slice 边界调用显式 observer；observer 由上层
  注入，不得让 integration 直接依赖窗口后端或消费输入。
- `AndroidBoundaryGles` 独占 buffer/texture/vertex/uniform/query/state/draw/readback 的调用
  准备与 transfer state；主 HLE 只传入当前 `AngleFrame`，组件不得拥有 EGL 生命周期、
  GPU 指标或窗口状态。
- `AndroidBoundaryHle` 从生成目录暴露完整 142 项 GLES2 Thumb trap 命名空间；只有显式
  handler 可以执行，未实现调用必须携带函数名失败。Looper/input 数据与 ANGLE readback
  跨线程传递必须受锁保护，未知地址或 SVC 不得吞掉。
- `AndroidBoundaryHle` 同时从隔离目录发布完整 145 项 `libGLESv1_CM.so` core Thumb
  trap 及固定 header 受检的 3 项 matrix-palette extension trap；core/extension 独立记账，
  未绑定固定管线调用明确失败，不得误用同名 GLES2 handler。
- guest transfer 失败必须保留原异常类别，并附带 `module!symbol`、r0-r3、SP、LR 与
  thread；client attribute staging 还须报告完整 descriptor 和 definition/enable LR。
- GLES1 `glViewport` / `glScissor` 直接转发当前 `AngleFrame`，与 GLES2 共用受检
  超采样坐标换算；没有当前 frame、换算溢出或 ANGLE 错误明确失败。
- GLES1 `glShadeModel` 只接受标准 `GL_FLAT` / `GL_SMOOTH`，写入独立、显式的
  fixed-pipeline context state；没有当前 frame 或非法枚举明确失败，managed/guest EGL
  context 终止时恢复 `GL_SMOOTH` 默认值。该状态供后续 fixed-pipeline draw 转换消费，
  不得以静默 no-op 代替。
- GLES1 `glClearColor` 逐位解码四个 guest `GLfloat` 参数并转发当前 `AngleFrame` 的
  clear-color state；它与 GLES2 共享真实 ANGLE context 语义，无当前 frame 或 ANGLE
  错误必须明确失败，不能仅在宿主侧缓存或吞掉调用。
- GLES1 `glClearDepthf` 逐位解码 guest `GLfloat` 并通过 `AngleFrame::ClearDepth`
  更新真实 ANGLE depth-clear state；无当前 frame 或 ANGLE 错误必须明确失败，不得
  以宿主缓存、固定默认值或 silent no-op 替代。
- GLES1 `glClear` 将 guest `GLbitfield` mask 原样转发 `AngleFrame::Clear`，由同一
  ANGLE context 应用当前 color/depth/stencil clear state；无当前 frame 或非法 mask
  产生的 ANGLE 错误必须明确失败，不得静默过滤未知 bit。
- GLES1 scalar state 批次把 active texture、buffer/texture binding、blend/color/cull/depth、
  enable/disable、finish/front-face/error/hint/pixel-store 与 texture parameter 共 17 个入口
  直接交给当前 `AngleFrame`；GLboolean、GLint 与 GLfloat 分别按非零、位模式有符号值和
  浮点位型解码。buffer/pixel-store 同时事务更新独立 GLES1 transfer state；GLES1-only
  perspective/point/line/fog hint 进入受检可重置状态，mipmap hint 才转发 ANGLE。ANGLE
  共有 capability 转发原生 context，GLES1-only capability 进入显式 fixed-pipeline state，
  `GL_TEXTURE_2D` 按 active texture unit 隔离。ANGLE 失败时不提交；无 current frame 或
  非法枚举明确失败。
- GLES1 raster/depth/stencil 批次将 clear-stencil、depth-range、line-width、polygon-offset
  与三项 stencil state 直接转发真实 ANGLE；point size/min/max/fade-threshold 保存为受检
  fixed state，size/min/max 由顶点 shader 的 `gl_PointSize` 消费。非法浮点、枚举、无 current
  frame 或原生错误必须明确失败，context reset 恢复规范默认值。
- GLES1 `glGenTextures`/`glDeleteTextures` 复用 ANGLE texture name 生命周期；`GLsizei`
  必须非负，guest 名称数组在 ANGLE 状态变化前按线程完整预检，生成结果仅在成功后一次提交。
- GLES1 buffer resource 批次复用 ANGLE name 生命周期并实现整块/局部数据上传；count、offset、
  size 与 guest 输入在原生调用前完整受检，删除当前绑定对象后同步 array/element binding。
  `glReadPixels` 按当前 pack alignment 解析精确输出范围，ANGLE 成功后才一次提交 guest 内存。
- GLES1 `GL_GENERATE_MIPMAP` 按 texture object 保存，不得作为 texture parameter 转发；
  active unit/binding/delete/reset 必须同步。值只接受 `GL_FALSE`/`GL_TRUE`，四个 texture
  image/copy handler 在 level 0 成功后消费 true 状态并通过 ANGLE 实际生成 mipmap。
- GLES1 `glCompressedTexImage2D`、`glCopyTexImage2D`、`glTexImage2D` 与
  `glTexSubImage2D` 转发当前 ANGLE context；像素大小服从独立 unpack alignment，nullable
  规则、负 image size、guest 范围与传输上限必须在任何 ANGLE 调用前明确验证。ETC1 在
  ANGLE 未发布原生或 lossy decode 扩展时通过 gles 模块的规范解码器上传 RGBA8，guest
  texture base format 仍保持 RGB 事实。
- GLES1 `glGetString` 接受 vendor/renderer/version/extensions；混合链接 guest 经共享符号
  查询 shading-language 时也转发真实 ANGLE context。五类结果写入 GLES1 专属、分槽且
  只读的 guest region，任何稳定指针不得因另一 pname 查询被覆盖。
- GLES1 `glGetIntegerv` 对矩阵栈、shade model、active/client texture、texture/buffer binding、
  client-array descriptor、pixel alignment 与固定管线真实上限返回转换器拥有的 context 状态；
  颜色位数、深度位数、legacy blend alias 及设备尺寸等兼容查询转发真实 ANGLE context。
  `glGetBooleanv` 将 capability/native 查询转换为逐字节 `GLboolean`；查询形状必须显式受检，
  guest 输出先完整预检再一次提交，未知 pname、无 current frame 或 ANGLE 错误不得伪造结果。
  同时链接 GLES1/GLES2 的 guest 可经共享 `glGetIntegerv` 查询 GLES2 shader/texture/uniform/
  varying 上限以及 current program、framebuffer、renderbuffer binding，值仍来自真实 ANGLE。
- GLES1 legacy fixed-state 批次显式绑定 alpha function、client active texture、current color
  与 texture environment；`glTexEnvf` 保留浮点参数语义；状态按 context/texture unit 隔离、
  验证、clamp 并随 reset 恢复默认。
  `glGetFloatv` 从对应状态返回矩阵、颜色、alpha 与 client texture，最大 anisotropy 必须
  查询真实 ANGLE 值；guest 输出及 `glTexEnvfv` 输入在任何状态变化前完整预检。legacy
  状态不可复制，高频 setter 必须依次完成参数验证、current-frame 验证和窄范围提交，禁止
  为事务语义复制全部 texture-environment 容器。
- GLES1 current normal 与六个 eye-space clip plane 属于可重置 legacy context state；
  `glMultMatrixf` 右乘当前 matrix，`glClipPlanef` 在提交时按 modelview 逆转置方程并拒绝奇异
  matrix。fixed shader 必须消费 current normal，并以六项 capability 控制真实 fragment
  clipping；不得只保存方程或伪造 `GL_MAX_CLIP_PLANES`。
- GLES1 client-array/draw 批次延迟保存 vertex/normal/color/texture-coordinate pointer；
  `glGetPointerv` 返回对应已保存 guest pointer，`glIsEnabled` 同时查询 server capability 与
  当前 client texture unit 的 array enable；context reset 必须恢复 GLES1 规定的 array
  size/type/stride/pointer/buffer/enable 默认值，供 descriptor 查询与保存/恢复闭环使用；draw
  才按实际 first/count 或 guest index 最大值完整预检 client 内存并上传内部 VBO/EBO。
  高频 client input、guest index 与顺序索引只复用宿主暂存高水位容量，每次 draw 仍重新
  预检并读取 guest 内容；pointer 更新先生成已验证候选，再在 current frame 成功后提交，
  禁止为事务语义复制整个 draw state。固定管线通过内部 GLES2 shader 消费
  modelview/projection/texture matrix、current/array color、
  light0、texture、fog 与 alpha-test 状态；guest buffer binding 在内部上传后必须恢复。
  `glDrawArrays` 以受检 `GLushort` 顺序索引等价执行，超过 65535 明确失败。当前 renderer
  从实际启用 `GL_TEXTURE_2D` 的单元选择 sampler、texture-coordinate array、texture
  environment 与 texture object base format；active/client active texture 只决定后续状态
  写入位置，不得误作 draw 输入。当前 renderer 支持最多两个启用纹理单元，按单元编号以
  各自 coordinate array、sampler、texture matrix、base format 和 environment 逐级应用
  MODULATE/REPLACE/ADD/COMBINE；`GL_PREVIOUS` 必须读取上一 stage 输出。超过两个单元、
  DECAL/BLEND、其他 texture environment 或 opaque EBO 配合 guest client array 必须明确
  失败。lighting 的 ambient/diffuse 只计算 RGB，输出 alpha 必须取 diffuse material alpha，
  不得累加 ambient/light alpha；light0 与 modelview 上三阶 normal matrix 保持现有
  partial。level-0 base format 按 texture object 保存并随 delete/reset 清理，未知格式不得
  猜测组合语义。
- 三个 `GL_OES_matrix_palette` 入口绑定在独立 extension dispatch：current palette index
  限定 0..31，matrix-index/weight pointer 延迟保存调用时 array-buffer binding，类型、size
  与 stride 受检且随 context reset。两类数组可由标准 client-state 入口启用；完整 skinning
  shader 尚未实现时 draw 必须明确失败，禁止忽略权重或伪装成功。
- GLES1 matrix state 批次把 modelview/projection 与按 active texture unit 隔离的 texture
  列主序矩阵栈保存，
  `load/push/pop/rotate/translate` 按 OpenGL 后乘语义更新。`glLoadMatrixf` 必须先通过
  `AddressSpace` 完整读取 16 个 little-endian `GLfloat` 并验证有限值；坏 guest 地址、
  非法 mode、栈上溢/下溢、非法旋转轴或无 current frame 均明确失败且不部分提交。
  context 终止时恢复默认 modelview mode 与全部 identity 栈；状态留给 fixed-pipeline
  draw 转换消费，不得将仅缓存矩阵解释为已完成渲染。
- GLES1 lighting/material/fog 状态批次显式绑定 7 个目标导入入口；所有浮点向量先从
  强类型 guest 地址完整搬运，再按 GLES1 pname、元素数、有限值及参数范围校验后事务提交。
  状态保存规范默认值并随 context reset；`glMaterial*` 默认严格要求
  `GL_FRONT_AND_BACK`，Profile 未显式启用兼容策略时不得接受 `GL_FRONT`。这些状态只供
  后续 fixed-pipeline shader/draw 转换消费，不代表已执行原生 GLES2 绘制。
- `AndroidBoundaryOptions::allow_gles1_material_single_face` 默认关闭；仅已验证 Profile
  quirk 可经 guest-session request 启用。启用时 `glMaterial*` 可独立保存 `GL_FRONT` 与
  `GL_BACK`，标准 `GL_FRONT_AND_BACK` 仍事务更新两面，其他非法 face 失败；context reset
  恢复两面默认值但不得丢失配置策略。
- shader/program handler 必须把 guest 二级源码数组、可选长度、查询输出和符号名完整
  预检后调用 ANGLE；active attribute/uniform 与 info-log 多输出先整体预检，再按 `bufSize`
  截断提交；编译/链接失败通过真实查询值表达，边界本身不得伪造成功。
- buffer/texture handler 必须复用生成目录与 transfer state 预检 guest 名称数组、数据长度、
  像素格式及对齐；状态只在 ANGLE 成功后提交，删除已绑定 buffer 必须同步解除搬运状态。
- GLES2 framebuffer/renderbuffer 批次复用生成目录和名称数组预检，真实转发生命周期、绑定、
  storage、两类 attachment、status 与 mipmap；第五个 `glFramebufferTexture2D` 参数必须从
  A32 guest 栈读取，坏 guest 输出或 ANGLE 错误不得产生部分回写或伪造完整状态。
- GLES2 blend/raster 状态补齐 blend color/equation、sample coverage 与 flush 四项真实转发；
  混合链接 guest 的 core `glSampleCoverage` 也必须在 GLES1 dispatch 转入相同 ANGLE context。
  flush 不得触发 managed/guest surface present。
- vertex attribute 延迟保存调用时 array-buffer binding；client array 在 draw 时按
  first/count 或 guest 索引最大值完整预检并上传内部 VBO/EBO，随后恢复 guest buffer
  binding。uniform 标量保持位模式，矩阵数据必须先按 IDL 长度完整搬运，任何坏地址都
  不得触达 ANGLE；vector/matrix4 批量入口同样复用 IDL 计数，constant attribute 的第五个
  float 从 A32 guest 栈读取。已绑定 element buffer 时不得为启用的 client attribute 猜测
  索引范围。
- draw renderer 由统一 Context 的 current program、fixed client-array 与 programmable
  attribute 事实共同决定；symbol 所属 library 不拥有 renderer 或 context state。
- Android boundary thunk catalog 必须保持从 `kBionicHleThunkBegin` 开始的 dense 4-byte
  slot；execution 只用 range/alignment/index decode，symbol provider 仅供链接与诊断。
- `glGetString` 只为样例使用的真实 ANGLE core 字符串建立有界只读 guest 槽；integer query、
  draw indices 与 readback 输出复用 transfer state，draw 成功后由主 HLE 更新指标。
- `glTexSubImage2D` 与 `glTexImage2D` 一样按 unpack/format 完整预检像素后再调用 ANGLE。
- 超采样倍率必须在创建任何 ANGLE 资源前完整验证；viewport 缩放溢出明确失败，guest
  `eglQuerySurface` 不得泄漏内部渲染尺寸，GPU 查询不得把逻辑尺寸伪装成真实 target。
- `NativeActivitySession` 只接受 API 19 ARMv7 当前入口，执行真实 Bionic 初始化、
  `ANativeActivity_onCreate`、glue child 与完整销毁回调；阶段可由可选 observer 查询。
- guest child 异常必须唤醒同步生命周期 waiter，并在 root 继续执行、帧或输入边界转为带
  原始原因的 `NativeActivityRunError`；禁止 SDL 主线程无限等待已死亡渲染线程的首帧。
- JNI guest modified UTF-8 访问族使用独立 64 KiB guest arena 保存带 NUL 的 copy；
  length/chars/release/region 必须解析统一 string store，`isCopy` 明确写 true，lease
  以 string identity + pointer + token 配对并 first-fit 回收。arena owner 不得在析构时
  反向访问可能已销毁的 string store；坏引用、region、输出、release 或容量明确失败。
- JNI guest class/object/instance family 只解析 class registry 已声明的精确名称与 instance
  method descriptor；三种 NewObject 仅接受 void `<init>` 并在失败时回滚 ref/object 映射。
  会话级 `JniGuestObjectRegistry` 让 guest 构造对象和 framework HLE 预注册的 host object
  共享精确 class identity；GetObjectClass/IsInstanceOf 与 30 个普通 instance Call/CallV/
  CallA 统一查询该 registry，并复用 invocation engine 的 assignability、argument/return 校验；
  未声明 class/method、伪 receiver 或返回类型不匹配必须明确失败。
- `PreflightAndroidGuestLink` 复用生产 Bionic namespace 与 Android boundary 完成映射和
  重定位但不执行 guest；报告 guest/boundary 模块及 relocation 数，任一缺失导入明确失败。
- `InitializeApi19GuestProcess` 事务映射统一 root TLS/thread-info/preinit、4 MiB 栈、
  `SVC #1` 返回 trap 与空 property area，并只向受检 libc 导出槽发布地址；固定布局冲突、
  非法线程/进程名或写入失败必须回滚新增映射和导出槽。
- `AndroidGuestCallSession` 组合真实 Bionic namespace、API 19 process、syscall/clone、
  guest JNI ABI/core bindings 与 Android HLE，执行 guest init/fini 并只接受通用 A32 frame；
  可选直接资源 implementation set 只安装通用 framework HLE 并拥有统一 JNI array store；
  通用 SoundPool handler 驱动同一会话拥有的 `JavaSoundPoolState`：除 destroy/init
  生命周期外，`stop_all_sounds` 清空全部 voice，`stop_all_pool` / `stop_all_big` 按类别
  清理并保留指定 resource；`is_sound_loaded*` 与 `unload_sound*` 使用同一分类 resource
  目录，查询按 legacy Java 契约返回 `0` / `-1`，卸载只删除精确键；`load_sound*` 记录
  pending request，`play_sound*` 执行 lazy request 且仅对真实 loaded resource 创建
  voice；pause/resume/stop、volume、pitch 与 reset handler 均驱动同一 voice 状态，目标
  不存在或参数无效时不伪造迁移；`pause_all_big`/`resume_all_big` 只批量迁移 big voice，
  不影响普通 pool；`play_sound_big_looping` 把 JNI boolean 同步提交给 state 与 mixer。
  注入编码资源 loader 后，load/lazy-play 只有在资源读取
  与 Ogg 解码成功后才原子提交 loaded，所有 voice 控制同步驱动离线 PCM mixer；失败继续
  保留 pending 与 mixer 错误事实。会话可向宿主拉取 stereo PCM16，但本层不打开设备；
  Profile phase、窗口和标题事实不得进入该会话。失败清理可显式调用
  `InterruptBlockingWaits`，使当前及之后的 guest futex wait 以 `-EINTR` 返回，避免
  finalizer 把宿主生命周期线程永久阻塞。
- session stop 必须先快照全部 child、请求仍运行者退出并中断 futex，再逐个 join；单个
  child 的异常只能作为首错延迟上报，禁止跳过其余 join。所有 child 完成后才可执行 guest
  fini 或让 lifecycle/dispatcher/address-space 进入逆序析构。
- guest JNI library lifecycle 只从显式 root module 自身选择唯一 exported/defined function
  `JNI_OnLoad`，不误调用 ELF 依赖的同名导出；调用帧固定为统一 guest JavaVM、null reserved，
  保留 ARM/Thumb symbol state，返回只接受 JNI 1.1/1.2/1.4/1.6。
- `AndroidGuestCallSession::InitializeJniLibrary` 只可在运行中的会话调用一次；调用方必须在
  ELF constructors 完成并注册所需 Java class 后显式调用，成功或 root 无 OnLoad 时才发布
  library-ready。失败、重复或停止后调用不得伪造完成。
- `audio.load_movie` 必须把非空 Java `String` 解析为最多 4096 个 UTF-16 code unit 的
  线程安全、递增序号电影请求；会话只发布最新请求与累计次数，不宣称已启动宿主播放器。
  null、未知字符串对象或超限名称必须在状态变化前明确失败。
- legacy Java media 批次按声明式 method id 重现 APK Java 层的固定 true/false/zero/-1
  与 no-op 语义，并按方法线程安全累计调用；master/per-music volume 是受检可查询状态。
  编号 sound resource 必须通过注入 loader 读取真实非空字节并受 JNI size 上限约束，
  不存在、负编号、空内容或无 loader 明确失败；不得把 Java no-op 解释成宿主播放成功。
- legacy AudioTrack framework 批次实现 PCM16 mono/stereo 的受检 minimum-buffer、stream
  constructor、play/pause/stop/release 与 byte-array region write；每个 track 以 host object
  identity 隔离并发布 playing/paused/released/bytes-written 状态。非法 format、mode、range、
  重复构造或 release 后调用明确失败；PCM 输出混音尚未接线时能力保持 partial。
- `display.change_mode` 按 legacy Java 契约把 mode `1` 记录为允许屏幕休眠，其他值记录为
  保持唤醒；请求进入线程安全的通用 framework 状态。宿主防休眠尚未接入时不得宣称已
  改变平台窗口策略。
- `process.exit` 只向线程安全的会话状态发布可查询的退出请求；不得直接终止宿主进程，
  也不得以 no-op 吞掉。前端观察请求后仍须执行 Profile lifecycle 与 guest session 清理。
- `locale.detect_phone_language` 必须从 framework 的受检确定性 Locale 配置计算 legacy
  语言索引；不得读取宿主区域设置或把游戏身份写入 handler。
- 通用 platform Java handler 只发布请求显式注入的安装 id/版本和确定性
  离线运营商、Wi-Fi、网络、音频与固件事实；字节数组和 Java String 必须通过
  统一 store 发布为受检 local reference。unique code、background、fully-loaded、
  keyboard、managed-swap 与离线 tracking sink 必须进入线程安全可查询状态。
  legacy framework platform 以一个批次声明 Build/VERSION 全量 APK 引用字段、
  SystemProperties、Settings.Secure、Context/ContentResolver/Telephony、Activity、Bundle、
  ViewRoot 与 UUID；static field 走统一 field store，service/UUID 对象走统一 object registry，
  未知 service/property/settings key 保留 Android 空值语义且不读取宿主隐私。宿主未实现的浏览器、
  商店、付费、在线服务与 trophy 回调必须带 method descriptor 明确失败，
  禁止静默 no-op 或伪造成功。
- host-managed surface 明确表示 GLSurfaceView 等 Java lifecycle 拥有的 ANGLE pbuffer；
  open/present/close 必须严格配对，guest EGL 不得替换或终止该 surface，帧仍走统一 resolve。
  宿主成功 present 后可归还布局完全匹配的拥有型帧；1x surface 复用其 RGBA8 高水位存储，
  被新帧覆盖但未消费的同布局存储也可回收，帧内容和序号本身不得缓存或复用。超采样帧仍
  通过 resolve 独立产生逻辑尺寸输出。
- `GuestJniAbi` 把完整 233 槽 JNIEnv 与 8 槽 JavaVM 物化为 32 位 guest 函数表、对象和
  Thumb SVC trap；reserved 槽保持 null，其余槽均有可识别地址。表与对象只读、trap 页
  RX，映射冲突完整回滚，析构后不得残留 guest 映射。
- `JniGuestCallDispatcher` 只消费精确落入上述目录的 `SVC #3`，校验 JNIEnv/JavaVM
  receiver 与非零线程后发布寄存器/栈调用帧；slot 必须在执行前显式绑定并封口，未绑定
  项按名称记账并失败，未知 trap 地址不得吞掉。
- `BindJniGuestCoreSlots` 只绑定已有真实 M3 语义的 50 个 JNIEnv slot 与 4 个
  JavaVM slot；引用、异常和线程状态复用同一环境，guest 输出指针在 VM 状态变更前预检，
  `GetStaticMethodID` 只精确查询统一 class registry，`NewStringUTF` 使用受检 guest C
  string、M3 Modified UTF-8 解码与 string store 后发布 local reference；
  10 种 `CallStatic*Method` 返回类型的普通、`V`、`A` 共 30 个槽按 method descriptor
  分别解码 A32 variadic、对齐 `va_list` 与 8 字节步长 `jvalue[]`，再进入统一 invocation
  engine；小整数符号/零扩展、float/double/long 双字返回及 void 均遵循 A32 guest ABI，
  调用名与 descriptor 返回类型不符时明确失败；
  `GetArrayLength` 只解析统一环境中的 primitive array identity；
  `GetByteArrayRegion` 只接受统一 store 中的 byte array，按有符号 `jsize` 校验区间，
  从 A32 guest 栈读取第 5 个参数并一次性写入受检 guest 内存；错误 return kind、class、
  method、handler、array reference/type/region 或输出缓冲明确失败，
  成功查询只发布统一 Guest JNI ABI 地址。非空 attach arguments 在实现其结构前明确失败。
- `BindJniGuestStaticFieldSlots` 批量绑定 `GetStaticFieldID` 与 Object/Boolean/Byte/Char/
  Short/Int/Long/Float/Double 的 9 对 static field getter/setter；field ID 只精确查询统一
  class registry，读写复用统一 field store，槽类型必须匹配 descriptor。word、符号扩展、
  float bits 与 long/double 双字返回遵循 A32 soft-float ABI，setter 的 64 位第 4 参数从
  对齐 guest 栈读取；错误 class/reference/field/kind/type/栈地址均明确失败。
- `NativeActivityRunRequest::supersample_factor` 选择受检 1..4× 内部渲染倍率；guest 的
  EGL surface 和输出帧保持逻辑尺寸，ANGLE pbuffer、viewport 与 GPU render target 使用
  放大尺寸，swap 时通过 gles 确定性 resolve 还原。
- `NativeActivitySession` 实现 core GPU provider，快照只报告真实发生的 clear、默认 FBO、
  ANGLE 后端和最近 2048 条 EGL/GLES 调用；未查询到的扩展、限制和 guest FBO 不伪造。
- Android boundary 的正常执行链只以 dense descriptor 的 route/function id 路由；
  library/name 只服务于 ELF 查询、诊断与 trace 渲染，禁止重新参与 HLE/GLES handler 选择。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp` 与
`android_boundary_hle_tests.cpp`；后者通过主 HLE 覆盖独立 GLES 分派组件。
