# M4 ANGLE 与 NativeActivity 验收

日期：2026-08-06

## 验收结论

M4 已通过出口验收。源码基线 `f1b59bb` 在 Windows/MSVC、Linux/GCC 与
macOS/AppleClang arm64 上均以 ANGLE 开启和 warnings-as-errors 完成配置与构建，并通过
严格全量 CTest 302/302。最终验收 commit 含审查修复（Linux SwiftShader ICD/loader 隔离、
macOS `libEGL`/`libGLESv2` dylib 暂存）及 ANGLE SDK `b47ed8c`。

## 已完成开发范围

| 开发项 | 状态 | 机器证据 |
| --- | --- | --- |
| ANGLE 预编译 SDK | 完成 | 同 commit 共享头与 Windows/Linux x64、macOS x64/arm64 包；清单哈希受检 |
| EGL pbuffer 生命周期 | 完成 | 真实 ANGLE 创建/逆序回滚；Vulkan 使用 headless display mode |
| GLES2 IDL/分派 | 完成 | 142 入口目录；综合样例 42 项真实绑定，其余明确失败 |
| shader/buffer/vertex | 完成 | 综合样例所需 handler 与 guest 搬运闭合 |
| SDL present / 超采样 | 完成 | RGBA8 等比黑边、1..4× resolve、逻辑尺寸不泄漏 |
| NativeActivity / run-apk | 完成 | 最小与综合 API 19 APK 出帧、输入与完整销毁 |
| 严格本机出口 | 完成 | `tools/m4_exit.py` 预检 Bionic/APK/ANGLE 后强制全量 CTest |

## 三平台出口证据

| 平台与工具链 | 硬件 backend | SwiftShader | 严格全量 CTest | 出口入口 |
| --- | --- | --- | --- | --- |
| Windows / MSVC | D3D11 | 包未编入，明确失败且不回退 | 修复后 `f1b59bb` 302/302 | `WU-0195` 复验 |
| macOS / AppleClang arm64 | Metal | Vulkan/SwiftShader 与隔离回归通过 | 修复后 `f1b59bb` 302/302 | `WU-0196` 复验 |
| Linux / GCC x64 | Vulkan | Vulkan/SwiftShader 与隔离回归通过 | 修复后 `f1b59bb` 302/302 | `WU-0198` |

Linux 出口额外闭合 GCC warnings-as-errors 下的 designated-initializer /
range-for 诊断，以及 SwiftShader 经清单 ICD + Vulkan loader 的初始化路径（ANGLE 原生
`DEVICE_TYPE_SWIFTSHADER` 会绕过 loader 丢失 WSI 扩展）。审查修复把 Vulkan driver
覆盖限制在 display 初始化临界区，恢复外部 `VK_DRIVER_FILES`/`VK_ICD_FILENAMES`，并验证
software→hardware 连续会话不会静默回退；关键图形与两个 APK 测试各连续 5 次稳定。
审查修复后 hardware/SwiftShader 黄金帧、backend 隔离与两个真实 APK 又各连续执行
5 次，全部通过。

macOS 复验额外闭合：审查修复引入的 `ogplay_stage_angle_runtime` 原先只拷贝 SDK
`bin/`（SwiftShader ICD），而 macOS `libEGL`/`libGLESv2` 位于 `lib/` 且 install name 为
`./…`，导致干净构建下 doctest 发现阶段无法加载 dylib。POST_BUILD 现同时暂存这两个
dylib；修复后严格全量 CTest 302/302，Metal/SwiftShader 黄金帧、backend 隔离、boundary
与两个真实 APK 各连续 5 次稳定。

Windows 复验在同一 `f1b59bb` 上以 `windows-msvc` 严格出口通过 302/302；D3D11 硬件
黄金帧、SwiftShader 明确失败且不回退、backend 隔离与两个真实 APK 各连续 5 次稳定。

## Work Unit 范围

- `WU-0150..0159`：ANGLE backend、预编译 SDK 与 Windows 真实 EGL pbuffer。
- `WU-0160..0174`：GLES2 IDL、guest 搬运、分派、SDL present、APK 与 NativeActivity。
- `WU-0175..0182`：GPU 查询、黑边呈现、scissor、超采样与 CLI。
- `WU-0183..0191`：多平台 ANGLE 包、综合样例 42 项 GLES2、首帧与输入像素锁定。
- `WU-0192..0197`：宿主硬件 backend 映射、严格出口门禁与 macOS 出口。
- `WU-0198`：Linux/x64 严格出口、跨平台编译与 SwiftShader ICD 路径，并编写本验收记录。

## 明确不属于 M4

- 窗口 EGL surface、完整 NDK、通用多库 `run-apk` 与 profile import（后续里程碑）。
- GLES1.1、未绑定的其余 GLES2 handler，以及 guest FBO/扩展查询。
- 完整 Android 系统、Binder、ART、Play 服务或现代支付/社交能力。
