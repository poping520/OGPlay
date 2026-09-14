# Android 4.4.4 GLES 功能完整性复核

日期：2026-09-14，2026-09-15 按 BND-35 更新。依据当前工作区源码与 `.local/aosp/`，不使用会话记忆。
状态文档、任务单和 MODULE 用于理解契约，不作为实现已经正确的证明。

## 结论

当前实现已闭合本报告发现且属于 OGPlay 游戏进程兼容层范围的具体缺陷：ES3 共用
`glVertexAttribPointer` 类型语义、Java GLES30 关键返回/数组桥接，以及 GLES1
`GL_OES_framebuffer_object` 全部 15 个独立 ABI 与行为入口。Native 核心名称集合保持齐全，
Context/Surface/share-group 已有实质实现。

OGPlay 不是 Android 模拟器。依据 ADR-0063，真实 Android ANativeWindow/BufferQueue、
native-buffer/native-fence FD、设备厂商 config/扩展全集不属于本项目能力目标，不能再作为
待修复缺陷。catalog complete 仍只表示已声明范围完整，不等同 CTS/Khronos 规范认证。

原始结论来自静态审计；BND-35 后执行了受影响目标构建、GLES1 FBO/ES3/Java GLES 定向
回归及 catalog/能力单调性门禁。未运行全量测试、CTS、Khronos 一致性测试或游戏场景，
也没有穷尽所有参数组合和驱动差异。

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

## 原审计问题及处置

### 1. ES3 复用的 glVertexAttribPointer 类型范围：已修复

`VertexAttribScalarBytes` 现按当前 Context client version 校验。ES3 接受 HALF_FLOAT、
INT/UNSIGNED_INT、INT_2_10_10_10_REV 与 UNSIGNED_INT_2_10_10_10_REV；packed 类型要求
size=4。ES2 仍保持原合法类型集合。定向回归在真实 ES3 Context、绑定 VBO 后覆盖五种新增类型。

### 2. Java GLES30 关键返回值/参数桥接：已修复

`JavaGlesHandler` 现为 `glGetStringi` 返回受检 Java String，为 `glMapBufferRange` 返回包装
guest mapping 的 DirectByteBuffer，为 `glFenceSync` 返回 long，并把非空 String[] 逐项转换为
有界 guest C string/pointer array 后调用 native handler。GLES20 `glGetString` 同时接受
GL_SHADING_LANGUAGE_VERSION（0x8B8C）。其他尚无确定 native 适配关系的方法继续明确失败；
`dexvm.java_gles` 因此保持 partial，而不是伪报整个 Java GLES 完成。

### 3. EGL 设备/系统互操作差异：按 ADR-0063 排除，不作为缺陷

`src/runtime/boundary/modules/egl/egl_module.cpp` 的以下差异属于兼容层边界：

- 配置固定为单个 RGBA8/D24S8、samples=0 的 config；不提供真实设备的配置集合、
  RGB565 或 EGL MSAA 配置选择。单配置本身不自动构成 EGL 规范违规，但不能复现设备能力。
- renderable/conformant 表示 boundary 可请求的 client version；native Context 创建仍以真实后端
  结果为准，不能把 config 位当作一致性认证。
- `eglCreateWindowSurface` 只校验 native window 非零，尺寸取全局 graphics layout；
  没有按 native-window 身份建立独立 Android 窗口关系。实际 backing 由 pbuffer 创建，
  swap 经 readback/PublishFrame 呈现，不等同 ANativeWindow/BufferQueue 的原生交换链。
- `eglCreatePixmapSurface`、`eglCopyBuffers` 拒绝 native pixmap，
  `eglCreatePbufferFromClientBuffer` 无成功路径；这些 Android 系统/非 GLES 游戏进程对象不扩入边界。
- 普通 pbuffer、texture pbuffer、bind/release texture、KHR image/sync 已有真实调用路径，
  不能沿用此前“全未实现”的判断。

### 4. 扩展采用受检白名单：符合边界，FBO 缺口已修复

GLES2/3：`graphics_dispatch.h/.cpp` 发布 ETC1、PVRTC、rgb8_rgba8，按后端追加 OES_EGL_image。
GLES1：`facade/android_boundary_hle.cpp` 发布 cube_map、matrix_palette、mapbuffer，并由 BND-35
新增 framebuffer_object 全部 15 项。draw_texture 等未实现扩展不发布、不伪造成功。

EGL：`GuestExtensionsLocked` 仅发布 get_all_proc_addresses，加上后端支持的七项 KHR
sync/image 扩展。不提供完整 Android presentation_time、image_native_buffer、recordable
等能力。`eglCreateImageKHR` 仅允许 GL texture/cubemap/renderbuffer target，明确拒绝
Android native buffer。未发布的扩展不得因 ANGLE 自身支持就算作 guest 支持。

本地 AOSP `framework/native/opengl/libs/EGL/eglApi.cpp` 明确区分：
presentation_time 是 wrapper 内建；部分扩展依赖驱动；native_fence_sync 等入口还受到
面向第三方的过滤。故“所有头文件/厂商扩展”不是统一 Android 4.4 设备能力要求，
这些 Android 系统互操作面受 ADR-0063 排除，不列入 OGPlay 游戏进程兼容层完成条件。

### 5. GLES1 GL_OES_framebuffer_object ABI 与行为桥接：已修复

原根因为项目已有 GLES2 framebuffer/renderbuffer 后端，却没有建立 GLES1 独立 OES ABI。
BND-35 已按精确名称补齐，未在 loader 中通用剥离 OES 后缀，也未建立第二套对象状态。

本地 AOSP `framework/native/opengl/include/GLES/glext.h:699` 定义该扩展，
`libs/GLES_CM/glext_api.in:166` 包含 glGenRenderbuffersOES 等 wrapper，
经 `GLES_CM/gl.cpp:162` 和 `libs/Android.mk:83` 构建进 libGLESv1_CM。
项目 `data/gles/gles1_extensions.json` 现登记 matrix_palette/mapbuffer 七入口及 OES FBO
十五入口，共 22 项；facade 将其发布到 libGLESv1_CM.so，直接导入、dlsym 与
eglGetProcAddress 共用同一 sealed symbol 路径。

该扩展族共 15 项，均有不带 OES 后缀的 GLES2 core 对应函数：

| 范围 | 已交付入口 |
| --- | --- |
| Renderbuffer 对象 | glIsRenderbufferOES、glBindRenderbufferOES、glDeleteRenderbuffersOES、glGenRenderbuffersOES |
| Renderbuffer 存储/查询 | glRenderbufferStorageOES、glGetRenderbufferParameterivOES |
| Framebuffer 对象 | glIsFramebufferOES、glBindFramebufferOES、glDeleteFramebuffersOES、glGenFramebuffersOES |
| FBO 完整性/附件 | glCheckFramebufferStatusOES、glFramebufferRenderbufferOES、glFramebufferTexture2DOES、glGetFramebufferAttachmentParameterivOES |
| Mipmap | glGenerateMipmapOES |

15 项 concrete handler 复用 ANGLE framebuffer/renderbuffer 操作与唯一 GuestGlContext，名称
生成/删除同步 share-group 元数据，参数输出经受检 guest buffer 搬运。只有整族 handler 完成后
才发布 GL_OES_framebuffer_object 字符串。定向回归覆盖全部符号可见性以及 framebuffer
生成、绑定、`glIsFramebufferOES` 和删除生命周期；更完整的绘制组合由后续场景按需验证。

`capabilities.toml` 的 `gles.gles1_extension_catalog` 与
`runtime.gles1_extension_boundary` 已同步记录 BND-35，状态保持 complete（选定扩展范围）。

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

## 当前剩余验证边界

1. 针对全部 core 的“签名、合法枚举、错误、输出长度、状态归属”行为矩阵仍可继续扩充；
   这是测试深度，不是已知代码缺陷。
2. 在受支持后端运行适当的一致性子集；全量测试仍需用户明确要求。
3. 目标 APK 如出现新的强导入或实际扩展调用，按游戏进程直接调用证据新增 Work Unit；
   不预先实现全部厂商扩展。
4. 任何 ANativeWindow/BufferQueue、native-buffer/fence FD 或完整设备 config 诉求都属于范围扩张，
   必须先修改 ADR，不能作为本报告的普通修复项。

已核对 `capabilities.toml`：catalog/core boundary 的 complete 表示登记范围完成；java_gles、
GLES1 部分绘制及 EGL KHR 边界仍按各自契约保持 partial。BND-35 没有把局部完成提升为
“完整 Android 4.4.4 GLES/EGL”声明。
