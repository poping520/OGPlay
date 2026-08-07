# 子模块：runtime/integration

## 职责

装配 Bionic、JNI 与 Android native 边界，生成里程碑出口报告；本模块只协调已有能力，
不实现新的 syscall、JNI、框架或文件系统语义。

## 依赖

位于 runtime 顶层，可依赖 framework、jni、bionic、syscall、execution、vfs 及其下层模块。
任何下层模块均不得反向依赖 integration。

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- `AndroidBoundaryGles` 独占 buffer/texture/vertex/uniform/query/state/draw/readback 的调用
  准备与 transfer state；主 HLE 只传入当前 `AngleFrame`，组件不得拥有 EGL 生命周期、
  GPU 指标或窗口状态。
- `AndroidBoundaryHle` 从生成目录暴露完整 142 项 GLES2 Thumb trap 命名空间；只有显式
  handler 可以执行，未实现调用必须携带函数名失败。Looper/input 数据与 ANGLE readback
  跨线程传递必须受锁保护，未知地址或 SVC 不得吞掉。
- `AndroidBoundaryHle` 同时从隔离目录发布完整 145 项 `libGLESv1_CM.so` core Thumb
  trap 及固定 header 受检的 3 项 matrix-palette extension trap；core/extension 独立记账，
  未绑定固定管线调用明确失败，不得误用同名 GLES2 handler。
- GLES1 `glViewport` / `glScissor` 作为首批实际触达的固定管线 handler 直接转发当前
  `AngleFrame`，与 GLES2 共用受检超采样坐标换算；没有当前 frame、换算溢出或 ANGLE
  错误明确失败。
- shader/program handler 必须把 guest 二级源码数组、可选长度、查询输出和符号名完整
  预检后调用 ANGLE；编译/链接失败通过真实查询值表达，边界本身不得伪造成功。
- buffer/texture handler 必须复用生成目录与 transfer state 预检 guest 名称数组、数据长度、
  像素格式及对齐；状态只在 ANGLE 成功后提交，删除已绑定 buffer 必须同步解除搬运状态。
- vertex attribute handler 只接受已有 VBO 的受检 offset；uniform 标量保持位模式，矩阵数据
  必须先按 IDL 长度完整搬运，任何坏地址都不得触达 ANGLE。
- `glGetString` 只为样例使用的真实 ANGLE core 字符串建立有界只读 guest 槽；integer query、
  draw indices 与 readback 输出复用 transfer state，draw 成功后由主 HLE 更新指标。
- 超采样倍率必须在创建任何 ANGLE 资源前完整验证；viewport 缩放溢出明确失败，guest
  `eglQuerySurface` 不得泄漏内部渲染尺寸，GPU 查询不得把逻辑尺寸伪装成真实 target。
- `NativeActivitySession` 只接受 API 19 ARMv7 当前入口，执行真实 Bionic 初始化、
  `ANativeActivity_onCreate`、glue child 与完整销毁回调；阶段可由可选 observer 查询。
- guest child 异常必须唤醒同步生命周期 waiter，并在 root 继续执行、帧或输入边界转为带
  原始原因的 `NativeActivityRunError`；禁止 SDL 主线程无限等待已死亡渲染线程的首帧。
- `PreflightAndroidGuestLink` 复用生产 Bionic namespace 与 Android boundary 完成映射和
  重定位但不执行 guest；报告 guest/boundary 模块及 relocation 数，任一缺失导入明确失败。
- `InitializeApi19GuestProcess` 事务映射统一 root TLS/thread-info/preinit、4 MiB 栈、
  `SVC #1` 返回 trap 与空 property area，并只向受检 libc 导出槽发布地址；固定布局冲突、
  非法线程/进程名或写入失败必须回滚新增映射和导出槽。
- `AndroidGuestCallSession` 组合真实 Bionic namespace、API 19 process、syscall/clone、
  guest JNI ABI/core bindings 与 Android HLE，执行 guest init/fini 并只接受通用 A32 frame；
  可选直接资源 implementation set 只安装通用 framework HLE 并拥有统一 JNI array store；
  Profile phase、窗口和标题事实不得进入该会话。
- host-managed surface 明确表示 GLSurfaceView 等 Java lifecycle 拥有的 ANGLE pbuffer；
  open/present/close 必须严格配对，guest EGL 不得替换或终止该 surface，帧仍走统一 resolve。
- `GuestJniAbi` 把完整 233 槽 JNIEnv 与 8 槽 JavaVM 物化为 32 位 guest 函数表、对象和
  Thumb SVC trap；reserved 槽保持 null，其余槽均有可识别地址。表与对象只读、trap 页
  RX，映射冲突完整回滚，析构后不得残留 guest 映射。
- `JniGuestCallDispatcher` 只消费精确落入上述目录的 `SVC #3`，校验 JNIEnv/JavaVM
  receiver 与非零线程后发布寄存器/栈调用帧；slot 必须在执行前显式绑定并封口，未绑定
  项按名称记账并失败，未知 trap 地址不得吞掉。
- `BindJniGuestCoreSlots` 只绑定已有真实 M3 语义的 19 个 JNIEnv 基础 slot 与 4 个
  JavaVM slot；引用、异常和线程状态复用同一环境，guest 输出指针在 VM 状态变更前预检，
  `GetStaticMethodID` 只精确查询统一 class registry，`NewStringUTF` 使用受检 guest C
  string、M3 Modified UTF-8 解码与 string store 后发布 local reference；
  `CallStaticObjectMethod` 按 method descriptor 解码 A32 variadic 参数并进入统一 invocation
  engine，错误 return kind、class、method 或 handler 明确失败，
  成功查询只发布统一 Guest JNI ABI 地址。非空 attach arguments 在实现其结构前明确失败。
- `NativeActivityRunRequest::supersample_factor` 选择受检 1..4× 内部渲染倍率；guest 的
  EGL surface 和输出帧保持逻辑尺寸，ANGLE pbuffer、viewport 与 GPU render target 使用
  放大尺寸，swap 时通过 gles 确定性 resolve 还原。
- `NativeActivitySession` 实现 core GPU provider，快照只报告真实发生的 clear、默认 FBO、
  ANGLE 后端和最近 2048 条 EGL/GLES 调用；未查询到的扩展、限制和 guest FBO 不伪造。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp` 与
`android_boundary_hle_tests.cpp`；后者通过主 HLE 覆盖独立 GLES 分派组件。
