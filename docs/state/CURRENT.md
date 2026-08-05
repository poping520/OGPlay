# 当前状态

更新：2026-08-05 · M4 NativeActivity command pipe 完成

## 当前阶段

- M0、M1、M2、M3 均已完成；M4 图形栈正在开发。
- `WU-0169` 已用 VFS descriptor 和受检 guest 数组闭合 Android ARM `pipe`；本轮继续
  minimal NativeActivity APK 的窗口呈现和启动测试，下一编号为 `WU-0170`。
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

- [WU-0105..0149] M3 JNI/Java 框架、DEX L1 与 runtime 七子模块迁移完成，并通过
  Windows、Linux、macOS 三平台验收；详见 `M3-ACCEPTANCE.md`。
- [WU-0150..0159] 冻结 ANGLE backend、预编译 SDK 与独立源码维护流程，并在 Windows
  D3D11 上闭合真实 EGL pbuffer 生命周期。
- [WU-0160..0164] 建立 GLES2 142-entry IDL/catalog、guest 搬运、显式分派和受限调用准备。
- [WU-0165] 上下文状态解析 pack/unpack 像素行跨度、guest index 数组、buffer offset、
  常用固定查询及显式登记的动态/uniform 形状；未知形状和无效状态更新明确失败；
  启用 ANGLE 的 Windows/MSVC 全量 CTest 263/263 通过。
- [WU-0166] 真实 ANGLE D3D11 pbuffer 执行 GLES2 viewport/clear，逐像素 readback 验证
  确定颜色帧；无 ANGLE、负尺寸和原生 GLES 错误均明确失败；全量 CTest 264/264 通过。
- [WU-0167] ELF loader 可注入 Bionic HLE 命名空间，追加绝对边界模块参与符号解析但
  不被映射/重定位；builder 删除、替换或重排 guest 模块会明确失败；全量 CTest
  266/266 通过。
- [WU-0168] Linux syscall 之外的 supervisor trap 只有显式 handler 确认后才继续；同一
  handler 传播至真实 clone child，拒绝和缺失路径继续保留可观测停止；全量 CTest
  267/267 通过。
- [WU-0169] VFS pipe 提供隔离的只读/只写端；syscall 42 先验证完整 guest descriptor
  数组再原子发布，写回失败回收两端，真实 write/read 字节闭环通过；全量 CTest
  269/269 通过。
- [WU-0170] SDL 窗口严格校验 RGBA8 帧并缩放更新 software surface；只有成功提交才累计
  present，dummy backend 覆盖真实 surface 更新；全量 CTest 270/270 通过。
- [WU-0171] APK ZIP32 central directory、路径和 local/central 元数据严格受检；未压缩
  native ELF 只有 CRC32 一致才会返回；全量 CTest 273/273 通过。

## 下一步（按优先级）

1. 接通 NativeActivity 启动、ANGLE guest 边界和 SDL 输入/显示。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
