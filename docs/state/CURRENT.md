# 当前状态

更新：2026-08-05 · M4 等比内容区输入映射

## 当前阶段

- M0、M1、M2、M3 均已完成；M4 图形栈正在开发。
- `WU-0178` 已复用显示布局把 pointer 坐标映射回 guest 并区分黑边；下一编号为
  `WU-0179`。
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
- [WU-0172] 最小 NativeActivity 所需 Android/Looper/input/EGL/GLES 导入进入唯一 Thumb
  HLE trap；ANGLE clear 发布 RGBA8 帧，command/input 唤醒保持显式；全量 CTest
  276/276 通过。
- [WU-0173] 真实 API 19 Bionic 启动 APK `ANativeActivity_onCreate` 与 glue child；生命周期
  command、首帧、键盘输入变色、销毁 join/finalizer 均由真实样例集成测试闭合；全量
  CTest 277/277 通过。
- [WU-0174] `ogplay run-apk` 选择唯一 ARMv7 native ELF，SDL 主线程显示 guest 帧并转发
  键鼠，关闭窗口同步销毁；真实 APK 有界 smoke 呈现 2 帧并以 0 退出，带真实 API 19
  Bionic/APK 环境的全量 CTest 277/277 通过。
- [WU-0175] core 提供不依赖 GLES/Agent 的 GPU 状态 provider；Agent 结构化暴露
  `gpu.stats/render_targets/capabilities/trace`，trace 过滤与 1..1000 限额显式传递，
  provider 缺失或越界返回可判定错误；Windows/MSVC + ANGLE、真实 API 19 APK/Bionic
  环境全量 CTest 280/280 通过。
- [WU-0176] `NativeActivitySession` 实现真实 GPU provider：样例 clear 计数、默认 RGBA8
  FBO、ANGLE renderer/device 与最近 2048 条成功 EGL/GLES 调用可查询；扩展、限制、
  guest FBO 和 shader 事实未发现时保持空，EGL 销毁后当前目标恢复为空；Windows/MSVC
  + ANGLE、真实 API 19 APK/Bionic 环境全量 CTest 280/280 通过。
- [WU-0177] `FitDisplayRect` 以溢出安全的整数运算计算等比内容区；SDL present 每帧先
  清黑色 surface，再以 nearest 模式居中缩放，4:3、竖屏与同宽高布局均有确定性测试；
  Windows/MSVC + ANGLE、真实 API 19 APK/Bionic 环境全量 CTest 281/281 通过。
- [WU-0178] `MapDisplayPoint` 与 present 共用同一内容矩形和舍入；CLI 按最近 guest 帧
  映射 pointer，忽略黑边按下/移动，并以夹紧坐标转发黑边释放以避免手势卡住；
  Windows/MSVC + ANGLE、真实 API 19 APK/Bionic 环境全量 CTest 282/282 通过，真实
  APK 2 帧窗口冒烟正常退出。

## 下一步（按优先级）

1. 推进超采样策略及 ANGLE/软件后端 GLES2 一致性与黄金帧出口工作。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
