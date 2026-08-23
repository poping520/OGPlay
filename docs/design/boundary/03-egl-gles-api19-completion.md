# Android 4.4.4 EGL/GLES Virtual SO 完整性闭环

状态：Accepted for implementation（2026-08-24）  
目标：API 19 / A32 guest；不扩展到 GLES3 或未命中的厂商扩展

## 1. 背景与目标

现有 Virtual SO 已把 `libEGL.so` 的 12 项、`libGLESv1_CM.so` 的 145 项 core +
3 项 matrix-palette extension、`libGLESv2.so` 的 142 项 core 发布为 dense SVC #2 thunk。
但“发布 symbol”不等于“实现 API”：GLES1 尚有 62 个已发布入口无 handler，GLES2 尚有
67 个已发布入口最终抛 `not implemented`；EGL 缺少基础查询/线程状态/proc-address 入口，
AOSP GLES1 wrapper 还直接导出 7 个 Android Bounds symbol。

本设计完成四个闭集：

1. EGL 增加 13 个基础入口：`eglGetError`、`eglQueryString`、`eglGetProcAddress`、
   `eglGetConfigs`、三个 current getter、`eglQueryContext`、`eglBindAPI`、`eglQueryAPI`、
   `eglReleaseThread`、`eglSwapInterval`、`eglCreatePbufferSurface`；
2. GLES1 增加 7 个 AOSP Bounds symbol；
3. GLES1 已发布未实现的 62 项全部拥有真实 handler；
4. GLES2 已发布未实现的 67 项全部拥有真实 handler。

不实现 GLES3 core、GLES1/GLES2 厂商 extension 全集，也不把未知 API 伪造成成功。目标游戏
后续若暴露非 OpenGL 阻断，只按既有 new-title 流程处理，不把游戏名或包名写入 `src/`。

## 2. 权威基线

### 2.1 AOSP 与 ROM

源码基线固定为 `android-4.4.4_r2.0.1` 对应目录：

- `.local/aosp/framework/native/opengl/libs/EGL/egl_entries.in`；
- `.local/aosp/framework/native/opengl/libs/GLES_CM/{gl.cpp,gl_api.in,glext_api.in}`；
- `.local/aosp/framework/native/opengl/libs/GLES2/{gl2.cpp,gl3_api.in,gl2ext_api.in,gl3ext_api.in}`。

用户给定 KTU84P ROM 的 `system.img` 经 sparse→ext4 后提取：

| 文件 | SHA-256 | global `egl*`/`gl*` function 数 |
| --- | --- | ---: |
| `/system/lib/libEGL.so` | `f98f6dd1e9dc8be9e3b53713291c079fd8f0db73383b6a62aca919395cf5054f` | 48 |
| `/system/lib/libGLESv1_CM.so` | `fc1adf55c488c0e5afa83a5b8775eb5fb8304f454f46de4cb1f505eb95df77d8` | 292 |
| `/system/lib/libGLESv2.so` | `8f6297ce57e131df0505de7f9da0214640f37927a93172cb7d97c8a1ce72b69d` | 367 |

ROM fingerprint 为
`google/hammerhead/hammerhead:4.4.4/KTU84P/1227136:user/release-keys`。ROM dynsym 是本次
symbol spelling/visibility 的最终事实，AOSP 源码用于函数语义和 wrapper 行为。

### 2.2 Khronos

行为遵循 Khronos 官方规范：

- EGL 1.4：<https://registry.khronos.org/EGL/specs/eglspec.1.4.pdf>；
- OpenGL ES 1.1：<https://registry.khronos.org/OpenGL/specs/es/1.1/es_full_spec_1.1.pdf>；
- OpenGL ES 2.0：<https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf>。

ANGLE 只执行 native GL 调用；guest address、对象 identity、current/error shadow、返回值和
错误转换仍由 OGPlay boundary 负责。

### 2.3 目标 APK

`.local/games/pvz_na_zhcn_fixed.apk` 的 selected ABI 为 `armeabi`。`libpvz.so` SHA-256 为
`e8863cdf19ff57a1a08bccf11ffd15c8dfc20c003c0778cfd8ba9d35b860b329`，其 ELF 直接导入：

- `eglGetProcAddress`；
- 完整 142 个 GLES2 core symbol；
- 不导入 GLES1 symbol。

因此 GLES2 67 项是目标 ELF 的真实链接面。静态全量 import 不能证明运行期每项都会命中，
但任何已发布入口仍必须保持规范行为，不能以中性桩通过。

## 3. 架构约束

现有成功路径保持不变：

```text
guest thunk -> SVC #2 -> HostCallHook -> dense slot
            -> {export-specific fn, concrete module*} -> handler -> continue JIT
```

- 不改变 SVC #2/#3 transport、JNI、libc override、Virtual ELF 或 OpenSL ES；
- 不引入 SONAME/local-id runtime switch、module vtable、`std::function` hot dispatch、
  `GetState/SetState` 或成功路径 `HaltExecution`；
- EGL/GLES1/GLES2 继续注入同一个 `GraphicsBoundaryContext` 和唯一 `GuestGlContext`；
- 普通参数继续由 `A32CallFrame` 一次解析；guest pointer 只经 `GuestPtr<T>`、IDL
  `PrepareGlesCall` 或明确的 address-space bulk read/write；
- 所有 native mutation 遵循“完整 guest 预检 → ANGLE 调用 → shadow commit”；
- ANGLE error 不跨层伪装成成功。GL API 可观察错误由真实 ANGLE `glGetError` 表达；
  transport/memory/内部契约 fault 继续走 pending structured fault。

`graphics_dispatch.cpp` 已超过常规文件上限，本次实现必须按状态、query、shader/uniform、
texture 等职责拆分，不继续扩张单一翻译单元。

## 4. EGL 状态与 13 个入口

新增窄化 `EglBoundaryContext`，聚合 `GraphicsBoundaryContext` 与只读 proc resolver callback。
resolver 只在 `eglGetProcAddress` 冷路径查询 sealed catalog，返回现有 public callable 的 guest
thunk 地址；未知、data 或 private callable 返回 0。它不得改变 hot table。

`EglModule final` 自有：

- display initialized/config/context/surface 的 session shadow；
- 以 guest thread id 为 key 的 current display/draw/read/context、bound API 和 last error；
- query-string 的稳定只读 guest pages；
- swap interval 与 pbuffer logical dimensions。

错误遵循 EGL 1.4 sticky-per-thread 语义：成功调用不清除旧错误，`eglGetError` 读取并重置为
`EGL_SUCCESS`。非法 display/config/context/surface、attribute 与 null-required output 返回
规范 false/null 并设置对应 EGL error，而不是 C++ exception。guest memory fault 仍保持原异常。

具体行为：

- `eglGetConfigs` 与 `eglChooseConfig` 使用同一 fake config 事实，支持 count-only 与 nullable
  config array；
- current getters/query/release 读取同一 thread state；`eglReleaseThread` 解除 native current
  ownership并恢复该线程 EGL state；
- `eglBindAPI` 仅接受 `EGL_OPENGL_ES_API`，`eglQueryAPI` 返回当前绑定 API；
- `eglSwapInterval` 接受实现支持范围并保存，不伪造 host display pacing；
- `eglCreatePbufferSurface` 解析 terminated `EGL_NONE` attribute list，仅支持 width/height，
  尺寸必须正且受限；返回 surface identity，真正 ANGLE frame 在 make-current 时创建；
- `eglQueryString` 返回与实际实现一致的 vendor/version/client APIs/extensions；extensions
  不宣告未实现 API；
- `eglGetProcAddress` 至少解析 sealed EGL/GLES public function；目标查询不到的 extension 返回 0。

## 5. GLES1 Bounds 与 62 项

7 个 Bounds symbol 进入独立 `gles1_bounds` metadata，不改变既有 145 core ID。每项直接绑定
export-specific handler；前四个与三个 OES pointer wrapper分别复用已有 descriptor validation，
并使用额外 `count` 对 guest client array 做非负/溢出校验。它们不是运行期 name lookup wrapper。

62 项分三种实现：

1. fixed-point aliases：以 `GLfixed / 65536.0f` 精确转换后复用对应 float validator/state；
2. vector/query/texture：按 pname 得出固定元素数，先完整搬运，再调用 ANGLE或读取唯一 shadow；
3. matrix/raster/misc：复用现有 fixed state 与 AngleFrame，禁止仅保存而不让 draw 消费。

本次完成列表以 BND-19/BND-20 任务单为闭集。`glPointSizePointerOES` 纳入现有 client-array
descriptor；如果 fixed shader 尚不能消费 point-size array，则启用后 draw 必须明确失败，不能忽略。

## 6. GLES2 67 项

67 项按三组实现，完整清单见 BND-21..23：

- raster/state/object：直接 ANGLE mutation、object predicate 和简单 scalar query；
- buffer/texture/query：IDL 驱动的 guest transfer，query shape 由规范 pname 决定；
- shader/uniform/attribute：字符串、数组和返回值完整预检，program/uniform shape 与 delete/link
  生命周期一致。

`AngleFrame` 增加与 GLES2 core 一一对应的窄方法，不向 boundary 暴露裸 host GL pointer。
`AndroidBoundaryGles` 拆为多个 implementation 文件，仍由 `Gles2Module final` 直接注入调用；
不恢复 function-id 中央 switch 作为 module dispatch。compile-time export binder 仍直接落到
`Gles2Module::Invoke<Id>`，组件内部按职责处理 API 语义。

`glShaderBinary`/`glReleaseShaderCompiler` 必须调用 ANGLE 并按真实 GL error 表达 unsupported，
不能直接返回成功。`glGetVertexAttribPointerv` 返回 guest 原始 pointer/offset identity，不能返回
host staging pointer。

## 7. WU 切分

| WU | 内容 |
| --- | --- |
| BND-16 | 设计、AOSP/ROM/APK ABI 冻结 |
| BND-17 | EGL 13 个基础入口与 thread/error/proc resolver |
| BND-18 | GLES1 7 个 Bounds direct bindings |
| BND-19 | GLES1 fixed-point/matrix/scalar aliases |
| BND-20 | GLES1 query/texture/misc，关闭全部 62 项 |
| BND-21 | GLES2 raster/state/object predicates |
| BND-22 | GLES2 buffer/texture/query |
| BND-23 | GLES2 shader/uniform/attribute，关闭全部 67 项 |
| BND-24 | PVZ 短时运行闭环、全量测试与 capability/doc audit |

每个行为 WU 都运行 focused tests 并独立提交；BND-24 才运行全量 CTest。游戏进程必须有
frame/tick/wall-time 上限，命令退出后确认无 `ogplay` 残留进程。

## 8. 验收

- EGL catalog 增加且真实实现指定 13 项，sticky error、thread current、proc-address、pbuffer
  和 guest-memory fault 有测试；
- ROM 的 7 个 GLES1 Bounds symbol 全部发布并直接绑定；
- GLES1 145 core + 3 extension 中不再有本设计列出的 62 个 unbound handler；
- GLES2 142 core 全部拥有真实 handler，architecture test 不再允许 fallback
  `not implemented`；
- fast/slow success/failure equivalence、shared GL state、late dlopen、metadata-only preflight
  和 architecture gate 保持通过；
- 目标 APK 在关闭 survey 的 bounded run 中越过 OpenGL import/dispatch 阻断，并以新的第一条
  非 OpenGL fault或可见帧作为运行证据；
- `cmake --preset dev`、`cmake --build --preset dev`、全量 `ctest --preset dev` 通过；
- 验收后无持续运行的 `ogplay` 进程。
