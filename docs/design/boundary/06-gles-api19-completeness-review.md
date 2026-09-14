# Android 4.4.4 GLES 功能完整性复核

日期：2026-09-14。依据当前工作区源码与 `.local/aosp/`，不使用会话记忆。
状态文档、任务单和 MODULE 用于理解契约，不作为实现已经正确的证明。

## 结论

当前实现不能完整复现 Android 4.4.4 的 GLES/EGL 能力。Native 核心名称集合齐全，
Context/Surface/share-group 已有实质实现，但 ES3 共用入口和 Java 适配仍存在代码级缺口；
EGL 配置、Android 窗口互操作和扩展仅覆盖子集。不得把 catalog complete 解读为规范 complete。

本次为静态审计：执行 UTF-8 源码读取、集合比较、调用路径核对和文档静态检查；
未构建、未运行图形测试、CTS、Khronos 一致性测试或游戏场景。下述行为由代码推导，
没有宣称本轮动态复现，也没有穷尽所有参数组合、驱动差异及 ABI。

## 核心名称集合：独立核对结果

| 本地 AOSP 基线 | 项目入口 | 结果 |
| --- | --- | --- |
| `framework/native/opengl/include/GLES/gl.h` | `data/gles/gles1.json` | 145/145，集合相等 |
| `framework/native/opengl/include/GLES2/gl2.h` | `data/gles/gles2.json` | 142/142，集合相等 |
| `GLES3/gl3.h` 减去 `GLES2/gl2.h` | `data/gles/gles3.json` | 104/104，集合相等 |
| `libs/EGL/egl_entries.in` 的 EGL 1.0–1.4 core | `egl_exports.h` | 34/34 均发布 |

GLES3 core 共 246 项，其中 142 项复用 GLES2。上述统计不是唯一 GL 符号的跨版本相加，
也不证明合法枚举、错误、内存搬运、shader、并发或所有 Java overload 都正确。
本地 GLES/glext.h 与 GLES2/gl2ext.h 分别有 140、121 个扩展函数原型；
这些是头文件声明，不是每台 Android 4.4 设备必须提供的能力全集。

## 明确的实现缺口

### 1. ES3 复用的 glVertexAttribPointer 仍限制为 ES2 类型

`src/runtime/boundary/services/graphics_dispatch.cpp` 的 `VertexAttribScalarBytes`
仅接受 BYTE/UNSIGNED_BYTE/SHORT/UNSIGNED_SHORT/FLOAT/FIXED。
同文件 vertex_attrib_pointer 分支在判断 VBO 之前无条件调用它。
因此 ES3 Context 中使用 HALF_FLOAT、INT/UNSIGNED_INT、2_10_10_10 packed 类型，
会在到达 ANGLE 前得到 GL_INVALID_ENUM。即使 VBO 已绑定，也不能绕开此限制。
104 个 delta handler 齐全不能弥补 142 个共用入口没有全面升级到 ES3 语义的问题。

### 2. Java GLES30 方法已声明，但关键返回值/参数没有桥接

`src/runtime/integration/dexvm_android/android_gl.cpp`：

- `DeclareJavaGlesClass` 把生成方法统一注册到 `JavaGlesHandler`。
- `ManagedGlResult` 只接受 void/int/boolean；其他返回类型抛 UnsupportedOperationException。
- GLES30 的 `glGetStringi` 返回 String、`glMapBufferRange` 返回 Buffer、`glFenceSync`
  返回 long，均未在该 handler 中获得专门返回适配。Native 调用可能已经发生后才抛异常。
- `glTransformFeedbackVaryings` 的非空 String[] 不能通过只处理 String、NIO 和 primitive
  array 的参数分支，进入 unsupported reference argument。
- `GlGetStringHandler` 仅接受 0x1F00–0x1F03，GLES20 查询
  GL_SHADING_LANGUAGE_VERSION（0x8B8C）也被拒绝。

以上签名可在本地 AOSP `framework/base/opengl/java/android/opengl/GLES30.java`
及项目 `generated/java_gles_surface.inc` 对照。签名存在测试不能证明方法调用可用。
另有 GLUtils 仅支持 RGBA/UNSIGNED_BYTE，GLSurfaceView.requestRender 直接返回，
render-mode setter 只存模式值；这些不等于完整 Android GLThread/调度实现。

### 3. EGL 有完整核心入口，但实现范围受限

`src/runtime/boundary/modules/egl/egl_module.cpp`：

- 配置固定为单个 RGBA8/D24S8、samples=0 的 config；不提供真实设备的配置集合、
  RGB565 或 EGL MSAA 配置选择。单配置本身不自动构成 EGL 规范违规，但不能复现设备能力。
- renderable/conformant 固定包含 ES1/ES2/ES3 bit；不先按后端实际支持生成 config 能力。
  native Context 创建失败仍可能发生，不能把这两个字段当作一致性认证。
- `eglCreateWindowSurface` 只校验 native window 非零，尺寸取全局 graphics layout；
  没有按 native-window 身份建立独立 Android 窗口关系。实际 backing 由 pbuffer 创建，
  swap 经 readback/PublishFrame 呈现，不等同 ANativeWindow/BufferQueue 的原生交换链。
- `eglCreatePixmapSurface`、`eglCopyBuffers` 始终拒绝 native pixmap；
  `eglCreatePbufferFromClientBuffer` 无成功路径。OpenVG 未发布，不能将其单独算为 GLES core 缺陷。
- 普通 pbuffer、texture pbuffer、bind/release texture、KHR image/sync 已有真实调用路径，
  不能沿用此前“全未实现”的判断。

### 4. 扩展只发布有限集合

GLES2/3：`graphics_dispatch.h/.cpp` 发布 ETC1、PVRTC、rgb8_rgba8，按后端追加 OES_EGL_image。
GLES1：`facade/android_boundary_hle.cpp` 额外发布 cube_map、matrix_palette、mapbuffer。
GLES1 OES framebuffer_object、draw_texture 的入口未在当前 boundary/catalog 找到。

EGL：`GuestExtensionsLocked` 仅发布 get_all_proc_addresses，加上后端支持的七项 KHR
sync/image 扩展。不提供完整 Android presentation_time、image_native_buffer、recordable
等能力。`eglCreateImageKHR` 仅允许 GL texture/cubemap/renderbuffer target，明确拒绝
Android native buffer。未发布的扩展不得因 ANGLE 自身支持就算作 guest 支持。

本地 AOSP `framework/native/opengl/libs/EGL/eglApi.cpp` 明确区分：
presentation_time 是 wrapper 内建；部分扩展依赖驱动；native_fence_sync 等入口还受到
面向第三方的过滤。故“所有头文件/厂商扩展”不是统一 Android 4.4 设备能力要求，
但项目当前连 AOSP 的 Android EGL 应用互操作面也未完整覆盖。

### 5. GLES1 GL_OES_framebuffer_object 扩展 ABI 与行为桥接缺失

**根因：项目实现了 GLES2 core 的 framebuffer/renderbuffer 后端，但没有建立
GLES1 GL_OES_framebuffer_object 的扩展目录、导出入口与行为适配。**
带 OES 后缀的函数是独立 ELF 符号，GLES2 同类函数存在不能替代 GLES1 扩展 ABI。

本地 AOSP `framework/native/opengl/include/GLES/glext.h:699` 定义该扩展，
`libs/GLES_CM/glext_api.in:166` 包含 glGenRenderbuffersOES 等 wrapper，
经 `GLES_CM/gl.cpp:162` 和 `libs/Android.mk:83` 构建进 libGLESv1_CM。
项目 `data/gles/gles1_extensions.json` 仅登记 matrix_palette/mapbuffer 七入口；
`src/runtime/boundary/facade/android_boundary_hle.cpp:678` 按目录发布 GLES1
core/extension，另有 bounds/image 入口，未提供 OES FBO 扩展族。

缺失范围共 15 项，均有不带 OES 后缀的 GLES2 core 对应函数：

| 范围 | 缺失入口 |
| --- | --- |
| Renderbuffer 对象 | glIsRenderbufferOES、glBindRenderbufferOES、glDeleteRenderbuffersOES、glGenRenderbuffersOES |
| Renderbuffer 存储/查询 | glRenderbufferStorageOES、glGetRenderbufferParameterivOES |
| Framebuffer 对象 | glIsFramebufferOES、glBindFramebufferOES、glDeleteFramebuffersOES、glGenFramebuffersOES |
| FBO 完整性/附件 | glCheckFramebufferStatusOES、glFramebufferRenderbufferOES、glFramebufferTexture2DOES、glGetFramebufferAttachmentParameterivOES |
| Mipmap | glGenerateMipmapOES |

影响：直接强导入任一缺失符号的应用 SO 无法完成动态链接，Java 加载路径可抛出
UnsatisfiedLinkError，阻塞应用初始化。`src/loader/link_namespace.cpp:190` 按精确
名称、版本及作用域解析符号，第 203 行拒绝未解析的非弱符号。
失败发生在函数执行前；运行时隐藏扩展字符串无法消除装载期的符号依赖。
该扩展属于游戏进程直接调用的图形能力，修复无需引入 Android 系统服务。

修复办法：

- 按完整 15 项扩展族补齐声明式目录、ABI/thunk 和 concrete handler，发布到
  libGLESv1_CM.so，统一直接导入、dlsym 与 eglGetProcAddress 的访问路径。
- 复用现有 ANGLE framebuffer/renderbuffer 操作和唯一 GuestGlContext，逐项核对
  OES 枚举、存储格式、参数搬运、错误和状态恢复；避免在 loader 中通用剥离 OES 后缀，
  也不建立第二套对象状态或游戏专属分支。
- 将 Java 同名扩展方法接入真实 adapter；扩展族行为完整后再宣告
  GL_OES_framebuffer_object，保持符号发布、扩展查询和实际执行能力一致。
- 增加强导入全部 OES 名称的 ELF 定向回归，以及对象生成/删除、存储、附件、
  framebuffer status、查询、真实绘制读回和 Context 切换测试。
- 对目标 APK 所选 ABI 的全部 SO 一次性核对 GL/EGL 未定义符号及依赖作用域，
  以关闭 survey 的场景验证加载和绘制，避免逐个补符号掩盖整族能力缺失。

`capabilities.toml` 中的 `gles.framebuffer_resources=complete` 指 GLES2 已交付范围，
`gles.gles1_extension_catalog`/`runtime.gles1_extension_boundary=complete` 指选定扩展，
均不涵盖 OES FBO 扩展族。该能力仍待实现与定向验收。

## Context 和多版本：已有实质实现，但不足以宣称完整

- EGL registry 保存 per-thread current/error，Context 有独立 native Context，Surface
  独立 backing，make-current 绑定不同 draw/read surface；实现占用检查与延迟退役。
- share 在创建时传真实 native share Context，guest 对象元数据按 share group 共享；
  GLES1 fixed/legacy/palette 状态随 Context 保存恢复。
- client version 1/2/3 均接受：ES1 通过 ES2 ANGLE Context 加自有固定管线 shader，
  ES2/ES3 创建对应 native 版本；ES3 delta 共用 libGLESv2.so。
- GLES3 handler 检查当前版本为 3；GLES1 绘制最多两个 texture stage。
  两个 stage 是实现上限，不能仅凭该上限认定 GLES1 最低规范违规。
- 当前没有本轮完整多线程、跨版本 share、context-loss、所有状态切换和驱动一致性证据。
  不把未验证项目表述成已确认缺陷，也不因已有若干回归就推断全规范正确。

## 修复优先级与能力账本

1. 优先补齐影响应用动态链接的 GLES1 OES FBO 扩展族，同时修复 ES3 共用入口类型范围和
   Java GLES 返回/数组桥接，为上述具体反例增加定向测试。
2. 针对全部 core 建立“签名、合法枚举、错误、输出长度、状态归属”的行为矩阵；
   不能只验证生成目录或 IsBound。
3. 按实际后端生成 config/版本/扩展事实，明确所需 Android 窗口与 image 互操作边界。
   若要越出当前游戏进程兼容层范围，应另立 ADR。
4. 在受支持后端运行适当的一致性子集；全量测试仍需用户明确要求。

已核对 `capabilities.toml`：catalog/core boundary 的 complete 表示登记的局部交付，
java_gles、GLES1 部分绘制及 EGL KHR 边界仍有 partial。此次没有新增能力或修改生产代码，
不调整能力状态，不将审计当作能力倒退；本报告记录的是当前完整性判断。
