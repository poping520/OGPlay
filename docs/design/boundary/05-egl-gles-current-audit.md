# 当前 EGL/GLES 完整性审计

日期：2026-09-14。结论：**不能宣称完整实现 Android 4.4.4 的 GLES/EGL。**

修复前审计只依据工作区代码、本地 AOSP 和 Khronos API 文档，不使用记忆。初始审计为静态
分析，当时没有运行构建、图形测试或 CTS；下文 A–H 的后果由代码推导。后续修复已运行
构建和定向图形测试，结果单独记入 BND-34，不改变初始审计记录的性质。
当前消息没有可访问的 GLES SO 压缩包；工作区中也未定位到对应包，因此没有做该包的
ELF dynsym、符号类型和 ABI 逐项比对。第三方 ANGLE 运行库不是 Android 接口 SO 样本。

## 修复跟踪（BND-34）

下文 A–H 保留为修复前审计记录，行号对应当时快照。当前改动与测试见
[BND-34](../../tasks/boundary/BND-34.md) 和 [ADR-0063](../../adr/media.md#adr-0063)。

| 原问题 | 本次处理 |
| --- | --- |
| A/B Context、Surface、share group | 独立 native 所有权、eager share、真实 draw/read、swap 默认缓冲选择 |
| C GLES1 Context 状态 | 完整 fixed/legacy/client-array 保存，资源元数据按共享组持有 |
| D ES3 旧入口搬运 | buffer targets、PBO、row/skip、VAO/整数属性和删除恢复 |
| E 查询越界 | UBO 变长、sampler 单值、向量/64 位和共享 program uniform 宽度 |
| F map 身份/生命周期 | share-group key、native 存活检查、空闲区复用、显式 flush、最终组退役 |
| G 扩展 | 统一清单、受检 KHR/OES image/sync、四入口 matrix palette 加权绘制 |
| H EGL 窗口边界 | texture pbuffer 与 ANGLE swap/interval；保留未发布的 Android 系统对象边界 |

**未完成的整体目标**：Android native-buffer/native-fence FD/presentation-time、Pixmap/OpenVG、
厂商扩展全集、用户所述 SO 的完整 ABI 比对，以及 CTS/Khronos 全量一致性验证。因此本次修复
不能把结论改成“完整模拟 Android 4.4.4 GLES”。

## 1. 接口名称覆盖

用 UTF-8 读取本地 `framework/native/opengl/include` 的头文件，提取 APIENTRY 函数名，
与当前 IDL/导出表做集合差分，结果如下。此统计不包含全部扩展或 SO 私有导出。

| 范围 | AOSP | 当前目录 | 缺少名称 |
| --- | ---: | ---: | ---: |
| GLES 1.1 gl.h | 145 | 145 | 0 |
| GLES 2.0 gl2.h | 142 | 142 | 0 |
| GLES 3.0 相对 GLES2 新增 | 104 | 104 | 0 |
| EGL 1.4 core | 34 | 34 | 0 |

`src/runtime/boundary/modules/module_catalog.cpp:125` 将 GLES1 core/扩展注册到
`libGLESv1_CM.so`，GLES2 和 GLES3 delta 注册到 `libGLESv2.so`。
因此不能再描述为“GLES3 104 项尚未发布”；但目录齐全不等于参数、状态和渲染语义齐全。

## 2. 已确认的实现缺口

### A. Context 与 Surface 生命周期没有真正分离（高优先级）

- `egl_module.h:59` 的 ContextState 按 surface 保存多个 AngleFrame。
- `egl_module.cpp:530` 在同一 guest Context 首次绑定新 Surface 时，重新执行
  `AngleFrame::CreatePbuffer`；后者经 `egl_lifecycle.cpp:299` 创建另一个 native Context。
- 共享对象不能代替 Context 状态共享。新 backing 只显式初始化 viewport/scissor，
  program、blend/depth、VAO 等状态不会因 native share 自动继承。
- 同一 Surface 被另一个 guest Context 使用时同样另建 pbuffer，Surface 像素不具备
  独立于 Context 的唯一存储。可用“Context A 清红，Context B 绑定同 Surface 读回”验证。
- `egl_module.cpp:145` 仅对 glReadPixels 切到 read backing；copy texture、blit 等读操作
  没有对应的统一 draw/read native surface 绑定。

### B. share group 建立依赖首次 MakeCurrent 顺序（高优先级）

`egl_module.cpp:536` 只有共享源已经拥有 frame 时才传入 native share context。
先创建 A，再创建 share=A 的 B，但先绑定 B 时，B 获得独立 native namespace。
而 `share_context` 只是 guest 句柄，后续通过 `contexts_.at` 查找；源 Context 提前销毁时，
尚未创建 backing 的共享成员还可能查到已删除句柄。需要独立的 share-group 生命周期。

### C. GLES1 每 Context 状态保存不完整（高优先级）

`egl_module.cpp:175` 的切换只保存 GuestGlContext、矩阵、shade model、normalize/rescale。
`gles1_dispatch.h:120` 的 hints、capabilities、logic operation、fixed_，以及
`gles1_fixed.h:53` 的 fog/lights 等仍属于另一份对象，未完整随 Context 保存恢复。
灯光、材质、雾及 client array 切换隔离不能由现有矩阵切换测试证明。

### D. ES3 共用旧入口的参数范围未补全（高优先级）

- `graphics_dispatch.cpp:197` 的 glBindBuffer 先执行 transfer.BindBuffer；
  `gles_transfer_state.cpp:262` 只接受 ARRAY_BUFFER/ELEMENT_ARRAY_BUFFER。
  合法的 UNIFORM_BUFFER、PIXEL_PACK_BUFFER、PIXEL_UNPACK_BUFFER、COPY_* 等绑定被拒绝。
- `gles_transfer_state.cpp:254` 接受 pack row/skip，但没有保存；二维 pixel_bytes
  在 `:349` 只采用 alignment，也未消费 ES3 unpack row/skip。
  非紧密二维上传/读回的搬运范围会与驱动访问范围不一致。
- `gles3_module.h:478` 的 3D 上传直接把参数当 guest pointer，没有 PBO offset 分支。
- `angle_frame_gles3.cpp:23` 直接 glBindVertexArray，但未同步边界的 element-buffer/
  attribute shadow；后续搬运和 indexed draw 仍可能读取旧 VAO 的状态。

### E. ES3 查询输出长度有确定错误（最高修复优先级）

`gles3_module.h:132` 中 FunctionId 36（glGetActiveUniformBlockiv）始终只分配一个 word，
`angle_frame_gles3.cpp:92` 将该地址直接交给驱动。查询 ACTIVE_UNIFORM_INDICES 时应输出
整个索引数组；多个 active uniform 可造成宿主暂存越界写，而不只是结果截断。
参见 [Khronos 原始参考页](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/es3.0/glGetActiveUniformBlockiv.xml)。

同处 FunctionId 48/49（glGetSamplerParameterfv/iv）固定预检并回写 4 words，
但 ES3.0 sampler 查询是单值，因而多写 guest 12 字节或错误拒绝页尾合法指针。
参见 [Khronos sampler 参考页](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/es3.0/glGetSamplerParameter.xml)。

### F. map-buffer 身份没有按 share group 隔离

`gles3_module.h:513` 与 `:607` 的 mappings_ 仅以 GLuint buffer 名为 key。
两个不共享 Context 可以各自拥有 buffer 1，映射却会发生身份冲突或返回别人的映射。
应结合 share-group identity；同时需要覆盖 delete、Context teardown 与未 unmap 的生命周期。

### G. 扩展只有有限子集，且 ES3 宣告不一致

- `egl_module.cpp:114` 发布空 EGL_EXTENSIONS，导出表只有 34 core。
  本地 AOSP `framework/native/opengl/libs/EGL/eglApi.cpp:79` 包含内建
  EGL_KHR_get_all_proc_addresses、EGL_ANDROID_presentation_time，以及驱动条件扩展
  image/native-buffer/fence-sync 等；当前没有对应完整桥接。
- `graphics_dispatch.cpp:33` 的 GLES2/共用 glGetString 扩展白名单只有 ETC1、PVRTC、rgb8_rgba8。
- `gles3_module.h:325` 的 glGetStringi 却直接发布 ANGLE 扩展名，未与 guest 支持清单统一；
  获得底层扩展名称不能保证 eglGetProcAddress 有对应 guest thunk。
- GLES1 发布五个扩展（`facade/android_boundary_hle.cpp:151`）；matrix-palette 入口虽然存在，
  `gles1_draw.cpp:527` 明确拒绝 skinning draw，而且未宣告此扩展。不能计作完整扩展实现。

### H. EGL 窗口系统只是兼容子集

`egl_module.cpp:952` 起的 pixmap、CopyBuffers、texture-pbuffer、client-buffer 路径明确失败；
窗口呈现使用项目 pbuffer/SDL 链。`:883` 的 swap interval 只写 SurfaceState，当前 src
未发现读取该字段或驱动实际交换时序的消费者。
部分 EGL 功能可以按 config 不支持，不能仅因此判断所有 EGL 实现不合规；但这明确不等于
复刻 Android 窗口、native buffer、同步及交换行为。完整 Android 系统也不在 OGPlay 范围内。

## 3. 多版本、Java 与验证判断

已有 ES1/2/3 client-version 路由、稳定 proc thunk、A32 宽值适配及 Java GLES30 声明。
Java EGL10/EGL14 通过 NativeEgl 共用 registry，GLES30 通过 managed bridge 共用 native
handler，因此原生缺口会影响 Java；本次不宣称已经逐个验证所有 Java overload。
Android 4.4 的公开 GLES3 目标是 ES3.0，不能把现代 ANGLE 的更多能力直接算入 API19 完整性。

当前测试包含 context 线程接管、pbuffer/share、VAO、sync/map/3D upload 和 fixed 像素用例。
这些用例是有价值的定向覆盖，不是所有参数组合、跨 Context 生命周期或 Khronos 一致性认证。

建议先补 E 的输出边界，再修 A/B/C 的对象所有权，随后补 D/F 与扩展声明闭环。
每项以具体反例建立定向测试：同 Context 换 Surface 状态、同 Surface 换 Context 像素、
共享成员反序首次绑定、灯光隔离、UBO/PBO、非紧密像素、多个 uniform 索引、sampler 页尾
输出、独立 share group 同名 buffer 映射。只构建受影响目标并运行相关测试。

## 4. 能力账本与本次变更

`capabilities.toml` 的 complete 是既定 Work Unit/目录范围，不能解释为 GLES 全规范完成。
当前 idl_codegen、gles2_dispatch 的旧 note 与新增完成记录并存；BND-33 的完成结论也不能
覆盖本报告指出的遗漏。本次没有新增实现能力，未降低或修改任何 capability status。
仅增加本报告并刷新 CURRENT 的审计快照；检查 UTF-8、链接目标与 diff，不触发构建/测试。
