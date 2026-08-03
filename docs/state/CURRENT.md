# 当前状态

更新：2026-08-03 · M0 完成会话

## 进行中

- M0 本地实现与验收完成；等待首次远端 hosted CI 确认三平台 job 后进入 M1 功能开发。

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

## 下一步（按优先级）

1. 建立远端后确认 Windows/Ubuntu/macOS hosted CI 全绿；失败则重新打开 M0。
2. 为 M1 拆分首个 Work Unit：HAL Clock 实时后端与平台目录骨架。
3. 按 M1 顺序实现 memory → CPU → 真线程，不得提前进入 Bionic/syscall。

## 已知问题

- ANGLE/SDL3/Qt/Dynarmic 尚未引入；它们分别属于后续 M1/M4/M6。
- 黄金帧使用无 GPU SoftwareSurface，不包含 ANGLE/SwiftShader；后者属于 M4。
- Agent Control 已有 stdio JSON-RPC；TCP/UDS/MCP adapter 后续按需要补齐。
- 当前只有本地 Git 仓库，三平台 hosted CI 尚无可执行远端，因此不能宣称远端 job 已绿。
- doctest 2.4.11 在 CMake 4.x 配置期会发出上游旧 policy 的 deprecation warning，
  不影响 warnings-as-errors 编译和测试。
