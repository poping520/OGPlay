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
- `EglLifecycle::CreatePbuffer`：创建 ANGLE display、EGL config、GLES2 context 和 pbuffer
  surface 并设为当前；`EglContextInfo` 暴露实际 EGL 版本、后端和尺寸事实。
- `AngleFrame`：在独占的真实 ANGLE pbuffer 上执行 viewport/clear，并以受检 RGBA8
  readback 输出左上原点的确定帧；scissor 可生成非对称黄金图案，每个原生 GLES 调用都
  检查错误，供 guest 边界与窗口呈现复用。
- `CreateNativeAngleEglApi`：ANGLE 启用时创建真实 API；关闭时明确抛出 unavailable，绝不
  回退系统 EGL。
- `data/gles/*.json`：GLES 边界的声明式单一事实源；每个指针显式声明方向、可空性和
  长度表达式，函数名唯一且稳定排序。
- `tools/generate_gles_api.py`：严格验证 IDL，并确定性生成 `ParameterSpec` / `FunctionSpec`
  C++ 目录；构建与测试均检查生成物没有漂移，并与固定 ANGLE `gl2.h` 核对入口集合。
- `GuestBuffer::Prepare`：依据 input/output/inout 对完整 guest 区间预检权限，并建立有大小
  上限的宿主暂存；输出只能显式 `Commit`，对象不可复制。
- `GlesDispatchTable`：从生成目录提供 142 个稳定 GLES2 thunk ID、精确名称/形状查询和
  显式 handler 绑定；未绑定调用抛错并累计可查询线程命中。
- `PrepareGles2Call`：按生成参数目录求值字面量、标量、常量乘法和有界 C 字符串，换算
  元素字节并创建 `GuestBuffer`；复杂表达式经 `GlesLengthResolver` 显式注入。
- `GlesTransferState`：保存上下文级 pack/unpack 对齐、array/element buffer 绑定及动态查询
  形状；解析像素、索引和常用查询长度，并把缓冲对象偏移与 client array 标为 deferred。
- `MakeSupersampleLayout` / `ResolveSupersampledRgba8`：验证 1..4× 渲染尺寸并用确定性
  RGBA8 box filter 还原逻辑帧；NativeActivity 接入在后续 Work Unit 完成。
- `OGPLAY_ENABLE_ANGLE`：默认关闭；开启时只接受清单校验通过的预编译 SDK，并导入
  `ANGLE::EGL`/`ANGLE::GLESv2`。
- `OGPLAY_ANGLE_SDK_ROOT` / `OGPLAY_ANGLE_SDK_CONFIGURATION`：指定平台化 SDK 根目录和
  release/debug 包；CMake 自动匹配宿主平台/CPU并验证所有声明文件。
- `third_party/angle-prebuilt/tools/`：由独立仓库拥有源码构建、SDK 打包和发布自测；OGPlay
  只通过固定 submodule commit 获取工具自测和已发布产物，不拥有生产脚本。
- `third_party/angle-prebuilt`：独立公开仓库的浅 submodule；普通构建和 Windows CI 从其
  固定 gitlink 获取 SDK，不初始化 ANGLE 源码。
- 后续 M4 Work Unit 在当前 pbuffer 生命周期上增加窗口 surface、资源搬运、present、trace
  和快照接口。

## 不变量

- GLES 语义与能力来自 ANGLE，边界函数由 IDL 生成。
- IDL 标量不得携带搬运元数据；所有指针必须具有 direction/nullable/count，禁止生成器
  猜测 guest 内存长度。
- GLES2 core 目录必须与固定 ANGLE 头文件的 142 个入口完全一致；二级指针保留
  indirection，宿主指针返回使用专门返回类型，禁止按 32 位标量误传。
- thunk ID 只由有序生成目录决定；分派前必须验证参数槽数量，handler 不得在分派锁内执行。
- 未绑定 GLES 调用必须记录函数、thunk、命中数与 first/last guest thread 后明确失败。
- 长度求值器不是通用脚本引擎；负数、未知标量、乘法溢出和缺失 resolver 必须在任何
  handler 前失败。二级指针的顶层数组元素固定为 32 位 guest 指针。
- VBO/element buffer 等由 GL 状态决定的地址只能由 resolver 显式标记 deferred，禁止把
  buffer offset 当 guest 地址读取；nullable null 不触发宿主分配或大小限额。
- 像素搬运按调用的真实参数位置读取 width/height/format/type；二维行对齐只作用于相邻
  行之间。未知格式、类型、查询形状、负数或长度溢出必须在 native 调用前失败。
- 动态查询和 uniform 输出长度必须由对象发现路径登记；状态更新先完整验证再提交，失败
  不得污染已有 pack/unpack、buffer 或形状事实。
- guest 内存参数先验证再搬运；GL 状态可供 Agent 查询。
- input 只读 guest，output 不得为初始化暂存而读取 guest，inout 必须同时预检读写；任何
  native 调用前必须完成全区间验证，输出回写不得依赖析构副作用。
- 平台探测事实由下层注入；gles 模块不使用平台宏，也不直接依赖平台 SDK 类型。
- EGL 创建严格遵循 display→initialize→config→bind API→context→surface→make-current；
  失败按已完成阶段逆序回滚，成功析构按 unbind→surface→context→terminate 清理。
- EGL 错误保留失败操作和原生错误码；无 ANGLE 的构建必须明确不可用。
- ANGLE 原生 `glReadPixels` 的底部首行必须在边界内翻转为 `ImageView`/SDL 的顶部首行，
  禁止把坐标系差异泄漏到每个消费者。
- 超采样尺寸、输入字节数和布局必须完整匹配；resolve 使用整数求和与固定四舍五入，
  禁止因宿主浮点或图形驱动产生黄金帧漂移。
- SwiftShader 只能作为 ANGLE Vulkan 软件设备使用；hardware-only 不得静默回退软件。
- ANGLE 源码只存在于维护者本地 `angle-prebuilt-repo` 工作区，由该仓库构建脚本固定 commit；
  普通消费端使用独立预编译浅 submodule，清单、平台或哈希不匹配时明确失败，不静默降级
  为源码或系统 EGL/GLES。
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
