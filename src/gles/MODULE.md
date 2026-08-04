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
- `CreateNativeAngleEglApi`：ANGLE 启用时创建真实 API；关闭时明确抛出 unavailable，绝不
  回退系统 EGL。
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
- guest 内存参数先验证再搬运；GL 状态可供 Agent 查询。
- 平台探测事实由下层注入；gles 模块不使用平台宏，也不直接依赖平台 SDK 类型。
- EGL 创建严格遵循 display→initialize→config→bind API→context→surface→make-current；
  失败按已完成阶段逆序回滚，成功析构按 unbind→surface→context→terminate 清理。
- EGL 错误保留失败操作和原生错误码；无 ANGLE 的构建必须明确不可用。
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
