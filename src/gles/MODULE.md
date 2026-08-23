# 模块：gles

## 职责

提供 guest EGL/GLES 边界库、IDL 代码生成和 ANGLE 接入，维护可查询的 GPU 指标。

## 公共 API

- `AngleBackendCandidates`：给出 Windows D3D11→Vulkan、Linux Vulkan、macOS Metal
  的稳定硬件顺序，三平台末位软件候选均为 ANGLE Vulkan/SwiftShader。
- `SelectAngleBackend`：依据显式探测结果与 automatic/hardware-only/software-only 偏好
  选择候选；没有可用候选时返回空值，不伪造成功。
- `AngleBackendName`：输出可用于配置、日志与 Agent 查询的稳定 renderer/device 名称。
- `EglApi`：不泄漏原生 EGL 类型的最小调用面；生产实现转调 ANGLE，测试可注入确定性
  失败。调用方必须保证 API 对象比使用它的 `EglLifecycle` 存活更久。
- `EglLifecycle::CreatePbuffer`：创建 ANGLE display、RGBA8+D24S8 EGL config、GLES2 context
  和 pbuffer surface 并设为当前；`EglContextInfo` 暴露实际 EGL 版本、后端和尺寸事实。
- `EglLifecycle::{ReleaseCurrent,BindCurrentOnCallingThread}`：显式释放并在受控调用线程
  重新绑定同一 context/surface，供 host-managed surface 的 GL currency 合法接力。
- `AngleFrame`：在独占的真实 ANGLE pbuffer 上执行 viewport/color/depth/stencil clear、
  depth-range、line/polygon/stencil scalar state、shader/program、buffer allocation/subrange、
  texture、framebuffer/renderbuffer 生命周期与附着、vertex/uniform、query/state、draw 与
  局部 readback 调用，并以
  受检 RGBA8
  全帧 readback 输出左上原点的确定帧；每个原生 GLES 调用都检查错误。
- `DecodeEtc1Rgba8`：按 Khronos ETC1 64-bit block 规范解码 individual/differential、flip、
  modifier selector 与边缘部分块，输出受检 RGBA8；尺寸、压缩长度和非法 differential
  color 明确失败。
- `DecodePvrtc1Rgba8`：以薄适配层调用固定 commit、原样 vendor 的 PowerVR Native SDK
  `PVRTDecompressPVRTC`，解码 PVRTC1 2bpp/4bpp RGB/RGBA；适配层验证二次幂尺寸、最小
  编码面、精确压缩长度、输入对齐和所有大小溢出，并输出受检 RGBA8。
  PowerVR vendor 源码、头文件与 license 必须保持上游逐字节哈希；项目差异只能进入薄
  adapter，不得复制、改写或维护第二份 PVRTC 解码算法。
- `CreateNativeAngleEglApi`：ANGLE 启用时创建真实 API；关闭时明确抛出 unavailable，绝不
  回退系统 EGL。
- `data/gles/*.json`：GLES 边界的声明式单一事实源；每个指针显式声明方向、可空性和
  长度表达式，函数名唯一且稳定排序。
- `tools/generate_gles_api.py`：严格验证 IDL，并确定性生成 `ParameterSpec` / `FunctionSpec`
  C++ 目录；构建与测试均检查生成物没有漂移，并与固定 ANGLE `GLES/gl.h` / `gl2.h`
  分别核对 GLES1.1 core 145 项与 GLES2 core 142 项入口集合。
- header subset 模式只允许目录声明固定 header 中真实存在的入口，用于隔离的标准扩展
  catalog；首个 GLES1 扩展目录覆盖 `GL_OES_matrix_palette` 的 3 个目标所需入口。
- `GuestBuffer::Prepare`：input 由单次 Read 完成验证与复制；output/inout 在 ANGLE 前取得
  带映射世代的 write preflight 并建立有大小上限的宿主暂存；输出只能显式 `Commit`，
  映射未变化时不得重复验证，变化时必须重新验证并 fail closed，对象不可复制。
- `PrepareGuestInput`：对重复 input 搬运复用调用方暂存高水位容量，返回精确长度 span；
  每次调用仍由单次 Read 完整验证并重新读取 guest 内容，禁止把暂存容量复用误作内容缓存。
- `GlesDispatchTable`：从生成目录提供 142 个稳定 GLES2 thunk ID、精确名称/形状查询和
  显式 handler 绑定；未绑定调用抛错并累计可查询线程命中。
- `GlesFunctionCount/FindGlesFunction/DescribeGlesFunction`：以显式 API 选择查询隔离的
  GLES1/GLES2 生成目录；`GlesDispatchTable` 可为任一 API 独立记账未绑定调用。
- `PrepareGles2Call`：按生成参数目录求值字面量、标量、常量乘法和有界 C 字符串，换算
  元素字节并创建 `GuestBuffer`；复杂表达式经 `GlesLengthResolver` 显式注入。
- `GlesTransferState`：保存上下文级 pack/unpack 对齐、array/element buffer 绑定及动态查询
  形状；解析像素、索引和常用查询长度（包括 GLES2 cube-map binding query），并把缓冲
  对象偏移与 client array 标为 deferred。
- `MakeSupersampleLayout` / `ResolveSupersampledRgba8`：验证 1..4× 渲染尺寸并用确定性
  RGBA8 box filter 还原逻辑帧；拥有型 1× readback 原样转移存储，不重复逐像素 resolve；
  NativeActivity pbuffer、viewport、swap 与 CLI 已接入。
- `OGPLAY_ENABLE_ANGLE`：默认开启；启用时只接受清单校验通过的预编译 SDK，并导入
  `ANGLE::EGL`/`ANGLE::GLESv2`。构建后把 SDK `bin/`（含 SwiftShader ICD）暂存到可执行
  文件旁；macOS 额外拷贝 `lib/libEGL.dylib` 与 `lib/libGLESv2.dylib`，因其 install name
  为 `./…`，必须与 ctest 工作目录中的可执行文件同目录。
- `OGPLAY_ANGLE_SDK_ROOT` / `OGPLAY_ANGLE_SDK_CONFIGURATION`：指定平台化 SDK 根目录和
  release/debug 包；CMake 自动匹配宿主平台/CPU并验证所有声明文件。
- `third_party/angle-prebuilt/tools/`：由独立仓库拥有源码构建、SDK 打包和发布自测；OGPlay
  只通过固定 submodule commit 获取工具自测和已发布产物，不拥有生产脚本。
- `third_party/angle-prebuilt`：独立公开仓库的浅 submodule；提供逐字节清单校验的共享头、
  Windows/Linux x64 与 macOS x64/arm64 SDK，不初始化 ANGLE 源码。平台清单中的 GN 参数
  是 SwiftShader 是否可执行黄金帧的唯一构建事实。
- 后续 M4 Work Unit 在当前 pbuffer 生命周期上增加窗口 surface、资源搬运、present、trace
  和快照接口。

## 不变量

- GLES 语义与能力来自 ANGLE，边界函数由 IDL 生成。
- ETC1 上传优先使用 ANGLE 原生 `GL_OES_compressed_ETC1_RGB8_texture`，其次使用其显式
  lossy-decode 扩展；两者都不可用时才由通用 ETC1 解码器转为 RGBA8 后调用 ANGLE
  `glTexImage2D`。其他压缩格式不得借此伪造支持。
- PVRTC1 上传优先使用 ANGLE 原生 `GL_IMG_texture_compression_pvrtc`；无原生扩展时，
  仅因官方 2bpp/4bpp 软件解码器真实可用而向 guest 发布该扩展，并转为 RGBA8 调用
  `glTexImage2D`。未知压缩枚举不得借该回退伪造成功。
- IDL 标量不得携带搬运元数据；所有指针必须具有 direction/nullable/count，禁止生成器
  猜测 guest 内存长度。
- GLES1.1/GLES2 core 目录必须分别与固定 ANGLE 头文件的 145/142 个入口完全一致；扩展
  子集目录中的每个函数也必须存在于固定 ANGLE extension header；
  API-specific namespace 不得混淆函数 ID；二级指针保留
  indirection，宿主指针返回使用专门返回类型，禁止按 32 位标量误传。
- thunk ID 只在所选 API 的有序生成目录内有效；分派前必须验证参数槽数量，handler 不得
  在分派锁内执行。
- 未绑定 GLES 调用必须记录函数、thunk、命中数与 first/last guest thread 后明确失败。
- 长度求值器不是通用脚本引擎；负数、未知标量、乘法溢出和缺失 resolver 必须在任何
  handler 前失败。二级指针的顶层数组元素固定为 32 位 guest 指针。
- VBO/element buffer 等由 GL 状态决定的地址只能由 resolver 显式标记 deferred，禁止把
  buffer offset 当 guest 地址读取；nullable null 不触发宿主分配或大小限额。
- 像素搬运按调用的真实参数位置读取 width/height/format/type；二维行对齐只作用于相邻
  行之间。未知格式、类型、查询形状、负数或长度溢出必须在 native 调用前失败。
- 动态查询和 uniform 输出长度必须由对象发现路径登记；状态更新先完整验证再提交，失败
  不得污染已有 pack/unpack、buffer 或形状事实。
- shader 源码段数、单段/总字节和符号名必须在宿主分配或 ANGLE 调用前有界；显式长度
  保留内嵌 NUL，负长度按 GLES 规则读取有界 C 字符串。
- buffer/texture 名称数组和资源数据必须通过声明式调用准备器搬运；null 数据只表达
  ANGLE 侧分配，像素字节数由当前 unpack alignment 与真实格式/类型共同解析。
- framebuffer/renderbuffer 名称数组必须完整预检和搬运；绑定、存储、texture/renderbuffer
  附着、完整性查询及 mipmap 生成必须转发当前 ANGLE context 并立即检查原生错误。
- buffer subrange 的 guest offset/size 必须非负，输入 payload 先完整预检再调用 ANGLE；
  `AngleFrame::BufferSubData` 只接受拥有精确长度的受检 span，并立即检查原生错误。
- boolean/float、buffer、texture、framebuffer attachment 与 renderbuffer parameter query
  必须调用对应的真实 ANGLE API；输出 storage 在 native query 前完整预检，只在成功后提交。
  compressed/copy texture 继续复用统一 ETC1/PVRTC fallback，不能绕过当前 context。
- vertex pointer 在绑定 array buffer 时把 32 位 guest 值解释为 VBO offset；client array
  延迟到 draw，按实际索引范围经内部 VBO/EBO 暂存后再进入 ANGLE。uniform vector/matrix
  数组按 IDL 形状完整搬运后再转换为宿主标量；shader/program active-variable 与 info-log
  查询来自真实 ANGLE 对象并保留截断/NUL 语义。
- program link 成功后必须枚举真实 active uniform，并按每个 array element location 登记
  scalar/vector/matrix value count；relink/delete 清除旧 shape。attached shader、source、precision、
  uniform 与 vertex attribute query 使用对应 ANGLE API；`glShaderBinary` 不消费 native GL error，
  unsupported 必须由后续 `glGetError` 原样观察。
- integer query 与 readback 输出必须先按 IDL/transfer state 预检；统一 context 已拥有的
  logical viewport/scissor 与 target-aware texture binding query 从 shared guest state 返回，
  其他 native state 再调用 ANGLE。draw indices 在绑定 element buffer 后作为 offset，未绑定
  则按 guest 索引完整搬运。查询字符串必须来自 ANGLE 并复制到只读 guest 页，禁止返回
  宿主指针或伪造固定文本。
- `AngleFrame::DrawArrays` 直接转发 ANGLE；client staging 由 runtime boundary 在调用前完成。
- texture subimage 像素必须先按 unpack alignment 与真实 format/type 预检，再调用 ANGLE。
- guest 内存参数先验证再搬运；GL 状态可供 Agent 查询。
- GLES1/GLES2 共用的无指针标量状态必须直接进入同一个 ANGLE context；boolean、signed
  integer 与 float 各自保持 JNI/A32 位型，原生 GLES 错误不得被宿主缓存掩盖。
- depth range、line width、polygon offset 与 stencil function/mask/operation 由 `AngleFrame`
  直接调用 GLES2-compatible ANGLE 入口；所有调用必须立即检查原生错误。
- blend color/equation、sample coverage 与 flush 同样直接进入 ANGLE；`glFlush` 只提交待处理
  GL 命令，绝不拥有 surface present、readback 或帧序号推进。
- input 只读 guest，output 不得为初始化暂存而读取 guest，inout 必须同时预检读写；任何
  native 调用前必须完成全区间验证，输出回写不得依赖析构副作用。
- 平台探测事实由下层注入；gles 模块不使用平台宏，也不直接依赖平台 SDK 类型。
- EGL 创建严格遵循 display→initialize→config→bind API→context→surface→make-current；
  失败按已完成阶段逆序回滚，成功析构按 unbind→surface→context→terminate 清理。
- host-managed pbuffer 的默认 framebuffer 必须明确请求 RGBA8、24-bit depth 与 8-bit
  stencil；不得让已启用的 depth/stencil test 因缺失 attachment 静默失效。
- ANGLE backend 选择经 `EGL_EXT_platform_base` 的 `eglGetPlatformDisplayEXT` 与 `EGLint`
  属性表进入，使用空 `void*` 表达默认 native display；不依赖部分平台包未实现的 EGL 1.5
  core 同名入口，也不把平台相关的 `EGLNativeDisplayType` 泄漏给扩展入口。Vulkan
  pbuffer 额外声明 `EGL_PLATFORM_VULKAN_DISPLAY_MODE_HEADLESS_ANGLE`。Linux SDK 随包
  交付 Vulkan loader；SwiftShader 请求在 display 初始化临界区以清单限定 loader 并使用
  hardware device attribute，临界区结束后必须恢复调用方环境，且真实 renderer 与
  `EglContextInfo` 都必须保持 SwiftShader 事实。非法的 renderer/device 组合必须在任何
  原生调用前明确失败。
- EGL 错误保留失败操作和原生错误码；无 ANGLE 的构建必须明确不可用。
- ANGLE 原生 `glReadPixels` 必须在边界内输出 `ImageView`/SDL 所需的顶部首行；支持
  `GL_ANGLE_pack_reverse_row_order` 时由驱动直接反向打包并恢复既有 pack 状态，否则
  在宿主内翻转，禁止把坐标系差异泄漏到每个消费者。`glReadPixels` 自身的同步语义已足够，
  全帧 readback 前不得额外强制 `glFinish`。
- 超采样尺寸、输入字节数和布局必须完整匹配；resolve 使用整数求和与固定四舍五入，
  禁止因宿主浮点或图形驱动产生黄金帧漂移；拥有型 1× resolve 必须保留输入存储。
- GLES dispatch table 只允许在初始化阶段绑定；`Seal()` 发布不可变 handler 表后，
  已绑定调用直接读取 slot 且不获取互斥锁，未绑定调用仍须加锁记账并明确失败。
- SwiftShader 只能作为 ANGLE Vulkan 软件设备使用；hardware-only 不得静默回退软件。
- ANGLE 源码只存在于维护者本地 `angle-prebuilt-repo` 工作区，由该仓库构建脚本固定 commit；
  普通消费端使用独立预编译浅 submodule，清单、平台或哈希不匹配时明确失败，不静默降级
  为源码或系统 EGL/GLES。
- 平台包与共享头清单必须使用同一 ANGLE commit；Git checkout 不得转换任何清单覆盖文件
  的行尾，避免内容逻辑相同但 SHA-256 不同。
- ANGLE GN 参数关闭测试、Null、OpenGL 和 WebGPU 后端；保留各平台既定硬件后端，
  Linux/macOS 同时构建 SwiftShader，Windows/MSVC 的 SwiftShader 留给独立 Clang 产物。
- Windows 禁用 Chromium 自带 libc++，由 MSVC 使用其原生标准库，避免混用编译器 ABI。
- ANGLE 版本必须同时满足项目后端需求与三平台既有工具链；升级 commit 前先验证其固定
  Chromium build 依赖要求，不以静默安装系统 SDK 的方式追随上游。

## 禁止

- 不手写 per-function GLES→桌面 GL 转译。
- 不把 `glFlush` 当 present，不伪造扩展或静默 no-op。

## 测试

`tests/gles/` 契约测试及软件后端黄金帧。
