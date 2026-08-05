# 当前状态

更新：2026-08-05 · M4 guest 渲染失败即时上报

## 当前阶段

- M0、M1、M2、M3 均已完成；M4 图形栈正在开发。
- `WU-0185` 已让 guest 渲染线程失败解除同步等待并返回 CLI；下一编号为 `WU-0186`，
  实现综合样例所需 shader/program handler 组。
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
- [WU-0165..0169] 完成 GLES 状态搬运、真实 ANGLE clear/readback、可注入 HLE 命名空间、
  supervisor trap 传播及 VFS pipe/syscall 42 原子发布；完整验收保留在各 WU 任务单。
- [WU-0170..0174] 闭合 SDL RGBA8 present、APK ZIP32 读取、NativeActivity HLE/session 与
  `run-apk`；真实 API 19 最小 APK 可显示、响应输入并完整销毁，详见各 WU 任务单。
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
- [WU-0179] `AngleFrame` 增加受检 scissor 状态，readback 在边界内把 OpenGL 底部首行
  翻转为 SDL/ImageView 顶部首行；8×8 非对称图案与 SoftwareSurface 逐像素一致；
  Windows/MSVC + ANGLE、真实 API 19 APK/Bionic 环境全量 CTest 283/283 通过，真实
  APK 2 帧窗口冒烟正常退出。
- [WU-0180] 超采样内核验证 1..4× 渲染尺寸、乘法溢出、布局一致性和 RGBA8 字节数；
  box resolve 使用整数累加与固定四舍五入，2× 四色方向及非整除平均值均有确定性测试；
  Windows/MSVC + ANGLE、真实 API 19 APK/Bionic 环境全量 CTest 286/286 通过。
- [WU-0181] NativeActivity 以逻辑尺寸和 1..4× 倍率创建真实 ANGLE pbuffer，缩放 guest
  viewport 并在 swap 时 resolve 回逻辑 RGBA8；guest EGL 查询与输入坐标保持逻辑尺寸，
  GPU target 报告真实放大尺寸；Windows/MSVC + ANGLE、真实 API 19 APK/Bionic 环境
  全量 CTest 287/287 通过。
- [WU-0182] `ogplay run-apk --supersample <1..4>` 在任何 APK I/O 前严格校验倍率，省略时
  保持 1×；真实 API 19 APK 以 2× 呈现 2 帧并正常退出，Windows/MSVC + ANGLE、真实
  APK/Bionic 环境全量 CTest 288/288 通过。
- [WU-0183] ANGLE 预编译子模块升级为同 commit 的共享头、Windows/Linux x64 与 macOS
  x64/arm64 包；逐文件字节在 Git checkout 后仍匹配清单，CMake 同时校验平台、GN 参数、
  共享头和哈希。黄金帧按宿主硬件 backend 运行；当前 Windows 包未编入 SwiftShader，
  测试确认明确失败且不回退硬件；Windows/MSVC + ANGLE 全量 CTest 289/289 通过。
- [WU-0184] 综合 API 19 ARMv7 样例以 NDK r21e 完成离线构建，12 个 EGL 与 42 个 GLES2
  调用和实际 ELF 导入均受检；Android HLE 从生成目录发布全部 142 项 GLES2 符号，样例
  越过装载期缺符号，未绑定调用仍明确失败；Windows/MSVC + ANGLE、真实 API 19
  APK/Bionic 环境全量 CTest 291/291 通过，未宣称综合样例已经出帧。
- [WU-0185] child HLE 异常会保存首次原因并唤醒 glue futex waiter；真实综合 APK 在
  `glCreateShader` 未实现时快速返回 `NativeActivityRunError`，CLI 关闭窗口并以非零状态
  输出精确错误，不再永久等待首帧；Windows/MSVC + ANGLE、真实 minimal/M4 APK 与
  API 19 Bionic 环境全量 CTest 293/293 通过。

## 下一步（按优先级）

1. 实现综合样例所需 shader/program handler，再推进 buffer/texture、draw/readback；
   Linux/macOS 与 SwiftShader 留到 M4 出口统一验收。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
