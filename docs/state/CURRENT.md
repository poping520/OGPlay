# 当前状态

更新：2026-08-03 · M0 初始化会话

## 进行中

- M0 工程地基尚未全部完成；本次要求的“可编译项目框架”已经完成并通过验证。

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

## 下一步（按优先级）

1. [WU-0006] 完成日志 sink、限流、地址符号化抽象与崩溃转储基础设施。
2. [WU-0007] 定义 Session/Clock，并完成 stdio JSON-RPC 确定性空会话闭环。
3. 增加能力账本“状态只能前进”的 CI 比较门禁。
4. 完成 M0 全部出口条件后才进入 M1；不得提前实现游戏运行功能。

## 已知问题

- ANGLE/SDL3/Qt/Dynarmic 尚未引入；它们分别属于后续 M1/M4/M6。
- 黄金帧目前只验证无 GPU 的确定性图像比较管线，不包含 ANGLE 软件后端。
- Agent Control 只提供 M0 的进程内结构化请求入口；JSON-RPC/TCP/UDS/MCP 传输后续补齐。
- doctest 2.4.11 在 CMake 4.x 配置期会发出上游旧 policy 的 deprecation warning，
  不影响 warnings-as-errors 编译和测试。
