# Android 4.4.4 GLES 完整性开发要求

## 目标

本开发单用于约束 OGPlay 的 API 19 GLES/EGL 兼容边界。目标不是复现完整 Android 图形
系统，而是让游戏进程直接调用、且已纳入支持范围的 GLES/EGL 能力具备一致的 ABI、参数、
错误、状态和生命周期语义。

核心名称、Java 方法或扩展出现在目录中，只证明入口可发现，不代表行为完整。任何能力只有在
符号发布、扩展查询、实际执行和机器可判定测试一致时才能记为完成。

## 范围

### 范围内

- Android 4.4.4 GLES1、GLES2、GLES3 core 中由游戏进程直接调用的入口。
- EGL 1.0–1.4 core 中兼容层可用独立 Context、Surface 和 share group 实现的入口。
- 已选择发布的 GLES/EGL 扩展及其独立 ABI。
- Java GLES wrapper 到唯一 native GLES/EGL boundary 的参数和返回值桥接。
- guest 指针、数组、Buffer、字符串、64 位值、对象句柄和错误状态的受检搬运。
- Context、线程、share group、对象和映射的状态归属及销毁语义。

### 范围外

依据 ADR-0063 和“OGPlay 不是 Android 模拟器”的边界，以下内容不作为本开发单的问题点：

- ANativeWindow、BufferQueue、Binder、system_server 或 Android 原生交换链。
- Android native buffer、native fence FD、presentation-time 系统集成。
- OpenVG、native pixmap 和完整 Android 窗口系统互操作。
- 复刻某一真实设备的 EGLConfig、GPU 驱动或厂商扩展全集。
- 未经目标游戏直接调用证据支持的现代支付、社交、反作弊或系统服务能力。

范围外入口必须不发布或明确失败，禁止伪造成功。若实际游戏证据要求扩张以上边界，应先新增
ADR，不得在图形模块中隐式引入 Android 系统对象。

## 开发问题

### 1. GLES3 共用入口必须采用 GLES3 语义

GLES3 相对 GLES2 的新增入口齐全，不代表两者共用的 142 个入口自动满足 GLES3。
所有共用入口必须按当前 Context client version 校验枚举、参数和状态。

`glVertexAttribPointer` 在 ES3 Context 中必须支持：

- GL_HALF_FLOAT；
- GL_INT、GL_UNSIGNED_INT；
- GL_INT_2_10_10_10_REV、GL_UNSIGNED_INT_2_10_10_10_REV。

两种 packed 类型只允许 size=4。ES2 Context 不得因共用实现而错误接受 ES3-only 类型。
VBO offset 和 client pointer 必须保持不同的内存语义。

### 2. Java GLES 方法必须按签名完整桥接

生成 Java 方法目录只负责发布 API surface。`JavaGlesHandler` 必须针对不能由通用
void/int/boolean adapter 表达的签名提供专用桥接，至少包括：

- `glGetStringi`：native 结果转换为 Java String，非法 name/index 保留 GL error；
- `glMapBufferRange`：返回包装 guest mapping 的 direct Buffer，不暴露 host pointer；
- `glFenceSync`：以 Java long 保留 guest sync identity；
- `glTransformFeedbackVaryings`：逐项校验 String[]，构造有界 C string pointer array；
- GLES20 `glGetString(GL_SHADING_LANGUAGE_VERSION)`。

native 调用失败时不得先产生 Java 成功对象。临时字符串、指针数组和 direct Buffer 的生命期
必须覆盖 native 调用，映射失效后不得继续访问。其余没有可靠 adapter 的签名必须记账并明确
抛出异常。

### 3. 已发布扩展必须具备完整独立 ABI

扩展函数的 OES/KHR 后缀是独立 ELF 符号，不能依赖 loader 通用剥离后缀映射到 core 名称。
直接导入、`dlsym` 和 `eglGetProcAddress` 必须解析到同一 concrete handler。

GLES1 `GL_OES_framebuffer_object` 必须完整提供以下 15 项：

| 范围 | 入口 |
| --- | --- |
| Renderbuffer 对象 | glIsRenderbufferOES、glBindRenderbufferOES、glDeleteRenderbuffersOES、glGenRenderbuffersOES |
| Renderbuffer 存储/查询 | glRenderbufferStorageOES、glGetRenderbufferParameterivOES |
| Framebuffer 对象 | glIsFramebufferOES、glBindFramebufferOES、glDeleteFramebuffersOES、glGenFramebuffersOES |
| FBO 完整性/附件 | glCheckFramebufferStatusOES、glFramebufferRenderbufferOES、glFramebufferTexture2DOES、glGetFramebufferAttachmentParameterivOES |
| Mipmap | glGenerateMipmapOES |

该扩展必须复用现有 ANGLE framebuffer/renderbuffer 操作、唯一 GuestGlContext 和 share-group
对象元数据，不得建立第二套资源状态。只有全部入口可执行后才能发布
`GL_OES_framebuffer_object` 扩展字符串。

其他扩展采用受检白名单：未选择实现的扩展不属于缺陷，但不得泄漏 ANGLE 后端扩展字符串或
只发布名称而缺少行为。

### 4. EGL 声明必须与兼容层事实一致

EGL core 入口即使全部发布，也必须准确表达兼容层能力：

- Context client version 以真实后端创建结果为准；
- Surface、draw/read binding、thread current 和延迟销毁必须相互独立；
- share Context 必须在 native 创建时建立真实 share 关系；
- pbuffer、texture pbuffer、bind/release texture 和已选 KHR image/sync 必须走真实调用路径；
- 不支持的 pixmap、client buffer 或系统对象路径返回准确 EGL error；
- 扩展字符串只能来自 guest 已实现白名单，不得透传整个 ANGLE 列表。

兼容层使用 pbuffer/readback/PublishFrame 向 SDL3 呈现是允许的实现方式，不要求模拟
ANativeWindow/BufferQueue；但不得将其描述成 Android 原生交换链。

### 5. 建立行为完整性矩阵

对支持范围内的每个 core 和扩展入口，至少核对：

- 精确 native/Java 签名和 A32 ABI；
- 合法与非法枚举、数值范围及对应 GL/EGL error；
- guest 输入、输出长度、nullable 和 offset；
- Context、thread、share group 或对象所有权；
- 创建、绑定、查询、删除及 context switch 后的状态；
- 失败时不部分提交输出、不伪造成功。

新增游戏适配前，应一次性检查目标 ABI 全部 SO 的 GL/EGL 未定义符号和依赖作用域，避免按
首次失败逐个补符号。

## 验收要求

- GLES1/2/3 与 EGL core 名称集合通过固定 API 19 基线比较。
- GLES1 OES FBO 全部符号通过强导入、`dlsym` 与 `eglGetProcAddress` 定向检查。
- OES FBO 覆盖对象生成/删除、storage、attachment、status、查询、绘制读回和 Context 切换。
- ES3 vertex pointer 覆盖所有新增类型、packed size 限制、VBO offset，并有 ES2 关闭对照。
- Java GLES30 覆盖 String、Buffer、long、String[] 的真实调用和失败路径。
- Context/share-group/thread/mapping 生命周期使用定向测试，不以 `IsBound` 代替行为验证。
- 修改只构建受影响目标并运行相关测试；除非用户明确要求，不运行全量测试。
- 受支持后端可运行适当的 Khronos/一致性子集，但不得因未运行完整 CTS 就伪称规范认证。
- 能力变化同步更新 `capabilities.toml`、相关 MODULE 和 `docs/state/CURRENT.md`。
