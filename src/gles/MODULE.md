# 模块：gles

## 职责

提供 guest EGL/GLES 边界库、IDL 代码生成和 ANGLE 接入，维护可查询的 GPU 指标。

## 公共 API

- `AngleBackendCandidates`：给出 Windows D3D11→Vulkan、Linux Vulkan、macOS Metal
  的稳定硬件顺序，三平台末位软件候选均为 ANGLE Vulkan/SwiftShader。
- `SelectAngleBackend`：依据显式探测结果与 automatic/hardware-only/software-only 偏好
  选择候选；没有可用候选时返回空值，不伪造成功。
- `AngleBackendName`：输出可用于配置、日志与 Agent 查询的稳定 renderer/device 名称。
- `OGPLAY_ENABLE_ANGLE`：默认关闭；开启时只接受固定 submodule 经官方 GN 流程生成的
  `libEGL`/`libGLESv2` 链接与运行时产物，并导入 `ANGLE::EGL`/`ANGLE::GLESv2`。
- 后续 M4 Work Unit 在此策略上定义 EGL 上下文、资源搬运、present、trace 和快照接口。

## 不变量

- GLES 语义与能力来自 ANGLE，边界函数由 IDL 生成。
- guest 内存参数先验证再搬运；GL 状态可供 Agent 查询。
- 平台探测事实由下层注入；gles 模块不使用平台宏，也不直接依赖平台 SDK 类型。
- SwiftShader 只能作为 ANGLE Vulkan 软件设备使用；hardware-only 不得静默回退软件。
- ANGLE 源码由顶层浅 git submodule 固定；其内部依赖遵循官方 depot_tools/gclient/GN
  流程，不把缺失产物静默降级为系统 EGL/GLES。

## 禁止

- 不手写 per-function GLES→桌面 GL 转译。
- 不把 `glFlush` 当 present，不伪造扩展或静默 no-op。

## 测试

`tests/gles/` 契约测试及软件后端黄金帧。
