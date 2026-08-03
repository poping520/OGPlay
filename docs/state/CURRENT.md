# 当前状态

更新：2026-08-03 · M1 裸 guest 样本会话

## 进行中

- M1 的 Clock、SDL3 Window/Input、4 GiB memory/soft-MMU/快照骨架已落地；
  CPU 解释器正在按指令族递增实现。
- 开发期间本机只执行 Windows/MSVC 验证；Linux/macOS 构建、窗口输入和虚拟内存
  总体验收统一放到 M1 出口阶段。
- 首次远端 hosted CI 仍待仓库建立远端后确认，不阻塞本地 M1 开发。

## 最近完成

- 已完整读取 `docs/roadmap/`，确认正式版为重写内核、迁移知识。
- 已核对本地 `docs/demo/` 的构建与目录，确认不迁移巨型文件和游戏分叉；该目录被 Git 忽略。
- [WU-0001] 顶层规则、ADR、文档索引与交接协议。
- [WU-0002] C++20/CMake、安装目标、固定 doctest 与三平台构建入口。
- [WU-0003] 12 个正式版模块契约与单向依赖边界。
- [WU-0004] 日志环形缓冲、能力账本、Control Service 和 CLI。
- [WU-0005] 三平台 CI、配置/Profile/Quirk/Bionic/黄金帧数据骨架。
- Windows/MSVC Debug 与 Release（warnings-as-errors）编译成功；CTest 13/13 通过。
- 架构门禁通过：`src/` 无裸输出、无游戏特判、无超过 800 行的源文件。
- [WU-0008] 文档迁移路径已同步，本地 Git 仓库初始化并排除 `docs/demo/`。
- [WU-0006] 完成日志 sink、限流、帧标记、符号化 provider 与诊断包。
- [WU-0007] 完成 FixedStep Clock 与确定性空会话。
- [WU-0009] 完成 stdio JSON-RPC 2.0 Agent 闭环。
- [WU-0010] 完成无 GPU SoftwareSurface 黄金帧后端。
- [WU-0011] 完成能力账本相对 Git 基线的单调性门禁。
- [WU-0012/0013] 完成 null-call 账本及 `sym`/`hle` 主动查询。
- Windows/MSVC Release（`/WX`）CTest 27/27；Cygwin GCC Release（`-Werror`）CTest 27/27。
- [WU-0015] doctest 2.4.11、SDL 3.4.10、Dynarmic 验证提交以递归 Git submodule 固定。
- [WU-0016] CMake 移除 FetchContent，默认离线使用 submodule；CI 递归 checkout。
- [WU-0017] 完成实时/固定步长、精确倍率、暂停统一 Clock，Session 同步暂停时间源。
- [WU-0018] 建立 Windows/Linux/macOS HAL 目录契约及平台代码泄漏自动门禁。
- Windows/MSVC warnings-as-errors 与 Cygwin GCC 14.4 `-Werror` CTest 31/31。
- [WU-0019] 完成 SDL3 窗口生命周期和键盘、鼠标、手柄、退出事件 HAL 映射；
  SDL 类型不泄漏，关闭 SDL 的构建会明确失败。
- 目标平台矩阵 warnings-as-errors 构建与 CTest 34/34。
- [WU-0020] 完成强类型 `GuestAddress`、完整 4 GiB 半开区间、低地址保护与溢出契约。
- [WU-0021] 完成 Windows/Linux/macOS 虚拟内存 HAL；Windows 已验证，另外两平台待 M1 出口验收。
- [WU-0022] 完成 4 GiB guest 地址空间、页权限、映射生命周期和结构化 `MemoryFault`。
- [WU-0023] 完成确定性小端 `CheckedMemoryBus`、跨页原子验证及访问观察器。
- [WU-0024] 完成带版本和权限的事务式内存快照骨架。
- [WU-0025] 冻结 A32/T32 核心/扩展寄存器、运行结果、fault 和 CPU 快照契约。
- [WU-0026] 分离 execute 取指与 data read 权限，补齐 `Fetch16/Fetch32`。
- [WU-0027] 完成 A32/T32 条件执行、立即数算术、分支/BX 和同步陷阱解释器基础。
- [WU-0028] 完成 A32/T32 word/byte 单次 load/store、writeback 和数据 fault 原子失败。
- [ADR-0008] 修正里程碑出口依赖：M1 不要求画面，NativeActivity APK 归入 M4 累积集成出口。
- [WU-0029] 完成 M4 API 19 `armeabi-v7a` NativeActivity 载荷；离线构建器已验证
  ELF32/ARM、`android_main`、APK 内容、zipalign、debug 签名和 API/ABI 元数据。
- [WU-0030] 完成 M1 裸 A32/Thumb mailbox 样本；解释器已验证标准化输入、确定性写回、
  guest 线程号传播及 CPU+内存快照复跑。
- Windows/MSVC warnings-as-errors 全量 CTest 63/63 通过；架构门禁全绿。

## 下一步（按优先级）

1. [WU-0031] 扩展 A32 barrel shifter、逻辑/寄存器算术、乘法及对应 Thumb-16 数据处理。
2. [WU-0032] 实现 A32/Thumb 多寄存器传输、栈操作和首批 Thumb-2 控制流。
3. 指令覆盖表稳定后启用 Dynarmic adapter，建立解释器/JIT 指令级对拍。
4. 随后实现真线程模型、thread HAL 与 futex，再装配最小 guest Session。
5. M1 功能闭合后让裸 guest 样本通过解释器/JIT 对拍，并执行 Windows/Linux/macOS 总体验收。

## 已知问题

- SDL3 已接入生产 HAL；Dynarmic 默认关闭，等待解释器覆盖和对拍框架稳定。
- `cpu.interpreter` 仍为 partial：尚缺完整 A32/Thumb-2、VFP/NEON、原子访问和异常模型。
- M1 裸 guest 样本当前只在 Windows/MSVC 解释器验证；JIT 与 Linux/macOS 留到 M1 收尾。
- 最小 NDK APK 已可构建和签名，但被明确归类为 M4 载荷；当前不能宣称已在 OGPlay 中运行。
- Linux/macOS 虚拟内存实现已落地但尚未在本轮执行，能力保持 partial 到 M1 总体验收。
- Linux 可用 SDL Unix-console 配置执行无显示服务契约；可见桌面窗口构建仍需 X11 或
  Wayland 开发依赖。
- 黄金帧使用无 GPU SoftwareSurface，不包含 ANGLE/SwiftShader；后者属于 M4。
- Agent Control 已有 stdio JSON-RPC；TCP/UDS/MCP adapter 后续按需要补齐。
- 当前只有本地 Git 仓库，三平台 hosted CI 尚无可执行远端，因此不能宣称远端 job 已绿。
- doctest 2.4.11 在 CMake 4.x 配置期会发出上游旧 policy 的 deprecation warning，
  不影响 warnings-as-errors 编译和测试。
- 后续本地开发验证不再使用 Cygwin，只使用 Windows/MSVC 预设。
