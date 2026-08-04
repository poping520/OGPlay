# 当前状态

更新：2026-08-04 · M4 EGL pbuffer 生命周期完成

## 当前阶段

- M0、M1、M2、M3 均已完成；M4 图形栈正在开发。
- `WU-0159` 已在预编译 ANGLE 上建立 display/config/GLES2 context/pbuffer surface 生命周期，
  具备精确错误与失败回滚；下一个任务编号为 `WU-0160`。
- 本机开发只使用 Windows/MSVC 预设；Linux/macOS 使用持久目录增量验证，并在里程碑
  出口执行三平台总体验收。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0105..0128] 完成 JNI/JavaVM 常用行为、双向累计契约与 DEX L1；低频槽继续 trap。
- [WU-0129..0142] 完成 Activity、资源、偏好、Locale、包信息和 native 工作线程累计闭环。
- [WU-0143] M3 功能开发闭合，累计出口 fixture 保持 partial。
- [ADR-0013/WU-0144] runtime 冻结 jni/framework/bionic/syscall/execution/vfs/integration
  七个子模块、无环依赖方向及渐进迁移规则。
- [WU-0145..0146] 建立子模块索引/文档门禁，并完成 VFS 镜像目录迁移。
- [WU-0147] 按特殊授权一次性移动剩余头文件与实现并更新全部路径，不修改代码逻辑。
- [WU-0148] 修复旧版 CMake 文档门禁策略和 AppleClang DEX 隐式转换问题。
- [WU-0149] 同一源码基线在 Windows/MSVC、Linux/GCC、macOS/AppleClang 完成严格构建，
  三端全量 CTest 均通过 232/232，M3 正式验收。
- [WU-0150] 建立独立 gles 目标，冻结 D3D11/Vulkan/Metal 与 Vulkan/SwiftShader 的
  三平台候选顺序、可用性探测和硬件/软件偏好契约。
- [WU-0151] 浅 submodule 固定官方 ANGLE；默认关闭，开启时严格验证 GN 产物并导入
  EGL/GLESv2 targets。
- [WU-0152] 核心依赖保持递归更新，ANGLE 顶层改为独立浅更新；普通远端验证不再
  无条件拉取 ANGLE 完整依赖图。
- [WU-0153] 构建驱动校验 ANGLE gitlink，固定三平台 GN 参数并只生成、验证
  `libEGL`/`libGLESv2`；Windows 使用 MSVC。
- [ADR-0014/WU-0154] 普通消费改用独立预编译浅 submodule；已固定平台化包布局、
  Release 优先策略、许可证及完整性清单，并从 Windows 真实产物完成打包复验。
- [WU-0155] CMake 按宿主平台/CPU选择 SDK，校验 schema、配置和全部声明文件后导入
  EGL/GLESv2；Windows/MSVC 启用 ANGLE 的全量回归通过。
- [WU-0156] 独立公开仓库发布约 13.4 MiB Windows x64 Release SDK；主仓库固定浅
  gitlink，Windows CI 和远端增量只获取预编译包，不初始化 ANGLE 源码工作区。
- [ADR-0015/WU-0157] OGPlay 删除 ANGLE 源码 gitlink；维护 checkout 和原始归档归入
  未跟踪的本地二进制仓库工作区，构建脚本继续固定并校验源码 commit。
- [WU-0158] ANGLE 源码构建、SDK 打包和发布自测归入 `angle-prebuilt-repo`；OGPlay 删除
  生产脚本，仅从固定 submodule commit 验证并消费 SDK。
- [WU-0159] EGL 生命周期通过可注入 API 覆盖全阶段错误和逆序回滚；Windows/MSVC 真实
  ANGLE D3D11 已完成 EGL 1.x、GLES2 context 与 pbuffer 的创建、make-current 和销毁，
  启用 ANGLE 的全量 CTest 243/243 通过。

## 下一步（按优先级）

1. `WU-0160` 定义 GLES 1.1/2.0 边界 IDL schema、校验器与首批生成物。
2. 随后接入 guest 内存搬运，并在现有 EGL 内核上增加窗口 surface 与 present。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
