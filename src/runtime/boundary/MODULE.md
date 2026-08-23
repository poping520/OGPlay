# 子模块：runtime/boundary

## 职责

- `BoundaryCatalog` 是 Virtual SO SONAME、active export、module-local id 与 dense thunk
  slot 的唯一冷路径事实来源；API seal 后只读。Bionic namespace 只从该目录识别已实现
  Virtual SO，未实现 SONAME 不得因历史 Profile 声明而伪装可用。
- module/export 的 `AndroidApiRange` 在 seal 时执行过滤；不适用项不进入 active catalog，
  不使整个 catalog 失败。local id 是 module metadata，可以非连续且不得依赖数组序号。
- synthetic Virtual SO 首次建立时发布该 module 的完整 active export 集，后续动态装载
  不得补写或扩展既有 dynsym。
- thunk arena 按实际 slot 数向上取整到多页并在写入后封为 RX；fast router 只做
  `PC → dense slot → {fn,self}`，live r0-r15 直接借用自 CPU hook，5 个以上参数只进行
  一次 guest stack bulk read。启用 guest-call slice observer 时上层不得安装 fast hook。
- fast handler 的 C++ 异常按 thread/PC 保存为 pending structured fault，退出 JIT 后由
  slow consumer 重抛原 exception identity；不得只留下 generic `host_call_fault`。
- 真实 guest libc override 与 Virtual SO 共用 dense hot transport，但每个 symbol 在 seal
  后拥有独立 `{export-specific fn, concrete module*}`；fast/slow 不得使用共享 mutable PC
  或统一参数个数推导当前 symbol。
- Android/EGL/GLES1/GLES2/log 以普通 `final` module type 实例化并在 seal 时一次 type
  erase；descriptor 只保留 module-local id 与签名冷数据。每个 active export 在 seal 时
  直接生成 `{export-specific fn, concrete module*}`，fast/slow transport 共用该 handler；
  调用期不再读取 SONAME/local id，不经过 module-level route、`HleRoute` 或全局 id。
  EGL/GLES1/GLES2 module 继续显式共享唯一 `GuestGlContext`，不复制 graphics state。

Android native 边界:`android_boundary_hle` session facade、GLES2/GLES1 边界组件、
boundary symbol 目录、跨 API 共享的 `GuestGlContext` 与 `A32CallFrame`。本模块把 guest
的 EGL/GLES/Looper/input 导入映射为显式 handler 并搬运 guest 数据,不拥有会话生命周期。

## 依赖

可依赖 gles 模块、loader、memory、cpu、core。不得依赖 `runtime/jni`、
`runtime/jni_guest`、`runtime/framework` 与 `runtime/integration`;上层通过显式接口
(`AngleFrame`、observer、options)注入运行期状态。

## 总则(适用于全部 GLES handler)

- 无当前 `AngleFrame`、非法枚举、非有限浮点或 ANGLE 原生错误必须明确失败;禁止静默
  no-op、部分回写或伪造结果。
- guest 输入(名称数组、像素、矩阵、字符串、二级指针)在任何 ANGLE 调用或状态变化前
  按强类型 guest 地址完整预检并搬运;guest 输出先整体预检,仅在 ANGLE 成功后一次提交。
- 宿主 shadow 状态仅在对应 ANGLE mutation 成功后提交;context reset/终止时恢复各自的
  GLES 规范默认值。

## 不变量

- 同一 guest EGL current context 下,GLES1 与 GLES2 入口共享同一个 `GuestGlContext`;
  buffer binding、pack/unpack alignment 与 active texture 由 `SharedGlState` 唯一拥有,
  GLES1 texture matrix 也直接以该 active texture 选择 unit,library origin 只决定 API 语义。
- texture binding、delete semantics、level-zero base format 与 generate-mipmap metadata
  同样由 `SharedGlState` 唯一拥有;binding 以 `(texture unit,target)` 区分 2D/cube-map,
  metadata 显式携带 object/target,删除 object 清除所有 unit/target 引用。shared state
  表达 GLES1/GLES2 能力并集,GLES1 handler 另行拒绝其 API 不支持的 target;object name
  只由同一个 ANGLE context 生成和删除。
- framebuffer/renderbuffer binding、viewport/scissor、clear state 与共有 capability 也只有
  一份 shared shadow;viewport/scissor 的 guest query 返回该 logical shadow,不泄露超采样
  后的 native 坐标。高频 setter 先验证、执行 ANGLE、再原位窄范围提交,禁止为事务语义
  复制含动态容器的整个 `SharedGlState` 或 texture-environment/draw state 容器。
- GLES1 fixed draw 通过显式 native transaction 临时使用内部 program/buffer/attribute;
  成功和异常返回前均恢复 guest programmable state,internal object 不写入 shared state。
- `AndroidBoundaryGles` 独占 buffer/texture/vertex/uniform/query/state/draw/readback 的
  调用准备与 transfer state;主 HLE 只传入当前 `AngleFrame`,组件不得拥有 EGL 生命周期、
  GPU 指标或窗口状态。
- `AndroidBoundaryHle` 从生成目录暴露完整 142 项 GLES2 Thumb trap 命名空间,并从隔离目录
  发布完整 145 项 `libGLESv1_CM.so` core Thumb trap 及固定 header 受检的 3 项
  matrix-palette extension trap;core/extension 独立记账,只有显式 handler 可以执行,
  未实现或未绑定调用必须携带函数名失败,不得误用同名 GLES2 handler。Looper/input 数据与
  ANGLE readback 跨线程传递必须受锁保护,未知地址或 SVC 不得吞掉。
- guest transfer 失败必须保留原异常类别,并附带 `module!symbol`、r0-r3、SP、LR 与
  thread;client attribute staging 还须报告完整 descriptor 和 definition/enable LR。
- GLES1 `glViewport`/`glScissor` 直接转发当前 `AngleFrame`,与 GLES2 共用受检超采样坐标
  换算;`glClearColor`/`glClearDepthf`/`glClear` 逐位解码 guest 参数并转发真实 ANGLE
  clear state,不得仅宿主缓存或静默过滤未知 bit;`glShadeModel` 只接受
  `GL_FLAT`/`GL_SMOOTH`,写入独立 fixed-pipeline context state 供后续 draw 转换消费。
- GLES1 scalar state 批次把 17 个无指针标量入口直接交给当前 `AngleFrame`;GLboolean、
  GLint、GLfloat 分别按非零、位模式有符号值和浮点位型解码。buffer/pixel-store 同时事务
  更新独立 GLES1 transfer state;GLES1-only hint 与 capability 进入受检可重置
  fixed-pipeline state,mipmap hint 与 ANGLE 共有 capability 才转发原生 context,
  `GL_TEXTURE_2D` 按 active texture unit 隔离。
- GLES1 raster/depth/stencil 批次直接转发 clear-stencil、depth-range、line-width、
  polygon-offset 与三项 stencil state;point size/min/max/fade-threshold 保存为受检
  fixed state,size/min/max 由顶点 shader 的 `gl_PointSize` 消费。
- GLES1/GLES2 texture/buffer name 生命周期复用 ANGLE name;`GLsizei` 必须非负,删除当前
  绑定对象后同步 array/element binding 与搬运状态。`glReadPixels` 按当前 pack alignment
  解析精确输出范围。
- GLES1 `GL_GENERATE_MIPMAP` 按 texture object 保存,不得作为 texture parameter 转发;
  active unit/binding/delete/reset 必须同步,值只接受 `GL_FALSE`/`GL_TRUE`。四个 texture
  image/copy 入口在 level 0 成功后消费 true 状态并经 ANGLE 实际生成 mipmap;像素大小
  服从独立 unpack alignment,nullable 规则、负 image size 与传输上限受检,ETC1 在 ANGLE
  未发布原生或 lossy decode 扩展时通过 gles 模块的规范解码器上传 RGBA8,guest texture
  base format 仍保持 RGB 事实。
- GLES1 `glGetString` 接受 vendor/renderer/version/extensions;混合链接 guest 经共享符号
  查询 shading-language 时也转发真实 ANGLE context。五类结果写入 GLES1 专属、分槽且只读
  的 guest region,任何稳定指针不得因另一 pname 查询被覆盖。
- GLES1 `glGetIntegerv`/`glGetBooleanv`/`glGetFloatv` 对矩阵栈、shade model、
  active/client texture、binding、client-array descriptor、alignment 与固定管线上限返回
  转换器拥有的 context 状态;位数、legacy blend alias、设备尺寸与最大 anisotropy 等兼容
  查询转发真实 ANGLE;查询形状显式受检,未知 pname 不得伪造。同时链接 GLES1/GLES2 的
  guest 可经共享 `glGetIntegerv` 查询 GLES2 上限与 current program、framebuffer、
  renderbuffer binding,值仍来自真实 ANGLE。
- GLES1 legacy fixed-state 批次显式绑定 alpha function、client active texture、current
  color、current normal、六个 eye-space clip plane 与 texture environment,状态按
  context/texture unit 隔离、clamp 并随 reset 恢复;`glMultMatrixf` 右乘当前 matrix,
  `glClipPlanef` 提交时按 modelview 逆转置方程并拒绝奇异 matrix;fixed shader 必须消费
  current normal 并以六项 capability 控制真实 fragment clipping,不得只保存方程或伪造
  `GL_MAX_CLIP_PLANES`。
- GLES1 client-array/draw 批次延迟保存 4 类 client pointer;`glGetPointerv`/`glIsEnabled`
  返回已保存 descriptor 与 server/client enable,context reset 恢复规范 array 默认值。
  draw 才按实际 first/count 或 guest index 最大值完整预检 client 内存并上传内部
  VBO/EBO,guest buffer binding 在内部上传后恢复;暂存只复用宿主高水位容量,每次 draw
  仍重新预检读取,pointer 更新以已验证候选在 current frame 成功后提交。固定管线通过
  内部 GLES2 shader 消费 modelview/projection/texture matrix、current/array color、
  light0、texture、fog 与 alpha-test 状态。`glDrawArrays` 以受检 `GLushort` 顺序索引
  等价执行,超过 65535 明确失败。当前 renderer 支持最多两个实际启用 `GL_TEXTURE_2D`
  的单元,按单元编号以各自 coordinate array、sampler、texture matrix、base format 和
  environment 逐级应用 MODULATE/REPLACE/ADD/COMBINE,`GL_PREVIOUS` 读取上一 stage
  输出;active/client active texture 只决定后续状态写入位置。超过两个单元、DECAL/BLEND、
  其他 environment 或 opaque EBO 配合 guest client array 必须明确失败。lighting 的
  ambient/diffuse 只计算 RGB,输出 alpha 取 diffuse material alpha;light0 与 modelview
  上三阶 normal matrix 保持现有 partial。level-0 base format 按 texture object 保存并随
  delete/reset 清理,未知格式不得猜测组合语义。
- 三个 `GL_OES_matrix_palette` 入口绑定在独立 extension dispatch:current palette index
  限定 0..31,matrix-index/weight pointer 延迟保存调用时 array-buffer binding,类型、
  size 与 stride 受检且随 context reset;完整 skinning shader 尚未实现时 draw 必须明确
  失败,禁止忽略权重或伪装成功。
- GLES1 matrix state 批次保存 modelview/projection 与按 active texture unit 隔离的
  texture 列主序矩阵栈,`load/push/pop/rotate/translate` 按 OpenGL 后乘语义更新;栈
  上溢/下溢、非法旋转轴不部分提交,状态留给 fixed-pipeline draw 转换消费,不得将仅
  缓存矩阵解释为已完成渲染。
- GLES1 lighting/material/fog 批次绑定 7 个目标导入入口,按 pname、元素数与参数范围
  校验后事务提交;`glMaterial*` 默认严格要求 `GL_FRONT_AND_BACK`。
  `AndroidBoundaryOptions::allow_gles1_material_single_face` 默认关闭,仅已验证 Profile
  quirk 可经 guest-session request 启用;启用时可独立保存 `GL_FRONT` 与 `GL_BACK`,
  标准 face 仍事务更新两面,reset 恢复两面默认值但不得丢失配置策略。
- GLES2 shader/program handler 把 guest 二级源码数组、可选长度、查询输出和符号名完整
  预检后调用 ANGLE;active attribute/uniform 与 info-log 多输出按 `bufSize` 截断提交,
  编译/链接失败通过真实查询值表达。
- GLES2 framebuffer/renderbuffer 批次真实转发生命周期、绑定、storage、两类 attachment、
  status 与 mipmap;第五个 `glFramebufferTexture2D` 参数必须从 A32 guest 栈读取。
- GLES2 blend/raster 批次真实转发 blend color/equation、sample coverage 与 flush；混合
  链接 guest 的 core `glSampleCoverage` 与 `glFlush` 也必须在 GLES1 dispatch 转入相同
  ANGLE context，flush 不得触发 managed/guest surface present。
- vertex attribute 延迟保存调用时 array-buffer binding;client array 在 draw 时按
  first/count 或 guest 索引最大值完整预检并上传内部 VBO/EBO,随后恢复 guest buffer
  binding。uniform 标量保持位模式,矩阵/vector 批量入口复用 IDL 计数,constant
  attribute 的第五个 float 从 A32 guest 栈读取;已绑定 element buffer 时不得为启用的
  client attribute 猜测索引范围。draw renderer 由统一 Context 的 current program、fixed
  client-array 与 programmable attribute 事实共同决定,symbol 所属 library 不拥有
  renderer 或 context state。
- boundary thunk catalog 必须保持从 `kBionicHleThunkBegin` 开始的 dense 4-byte slot;
  正常执行链只以 dense descriptor 的 route/function id 路由,library/name 只服务于 ELF
  查询、诊断与 trace 渲染,禁止重新参与 handler 选择。
- `A32CallFrame` 按 descriptor 的精确 parameter count 固定存储 r0-r3,并以一次 guest
  bulk read 解码剩余栈参数;handler 不得自行逐字读取 guest 栈。普通指针/string 参数用
  `GuestPtr<T>`/`GuestCString` 保持 guest address identity，禁止转换为 host pointer；
  variadic/callback 等复杂 ABI 可保留显式 custom wrapper。
- `glGetString` 只为样例使用的真实 ANGLE core 字符串建立有界只读 guest 槽;integer
  query、draw indices 与 readback 输出复用 transfer state,draw 成功后由主 HLE 更新指标。
- 超采样倍率必须在创建任何 ANGLE 资源前完整验证;viewport 缩放溢出明确失败,guest
  `eglQuerySurface` 不得泄漏内部渲染尺寸,GPU 查询不得把逻辑尺寸伪装成真实 target。
- host-managed surface 明确表示 GLSurfaceView 等 Java lifecycle 拥有的 ANGLE pbuffer;
  open/present/close 必须严格配对,guest EGL 不得替换或终止该 surface,帧仍走统一
  resolve。宿主成功 present 后可归还布局完全匹配的拥有型帧;1x surface 复用其 RGBA8
  高水位存储,被新帧覆盖但未消费的同布局存储也可回收,帧内容和序号本身不得缓存或
  复用。超采样帧仍通过 resolve 独立产生逻辑尺寸输出。
- host-managed surface 创建并初始化默认状态后保持打开线程 GL currency；session 可在
  guest-owned render driver 启动前由该线程显式释放，随后首个 GL/present 调用绑定到
  新调用线程。同线程可再次显式释放后接力，跨线程抢夺必须明确失败。
- `PublishSoftwareFrame` 允许上层(如视频解码泵)注入一整帧逻辑尺寸 RGBA8 软件帧,
  复用统一帧存储与递增 sequence;尺寸与逻辑 surface 布局不符必须明确失败,不得部分
  发布。

## 测试

对应 `tests/runtime/android_boundary_hle_tests.cpp`(同时覆盖独立 GLES 分派组件)与
`tests/runtime/android_boundary_gles1_fixed_tests.cpp`。
