# 当前状态

更新：2026-08-03 · M1 启动会话

## 进行中

- M1 HAL Clock、平台边界与 SDL3 Window/Input 已完成；下一 Work Unit 进入 memory。
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

## 下一步（按优先级）

1. [WU-0020] 定义强类型 `GuestAddress`、地址区间与溢出/边界契约。
2. [WU-0021] 实现 4 GiB guest 地址空间预留、提交/释放和宿主权限转换。
3. memory 契约稳定后实现解释器 CPU，再启用 Dynarmic，随后进入真线程。
4. 建立远端后确认 hosted CI 全绿；失败则修复对应平台门禁。

## 已知问题

- SDL3 已接入生产 HAL；Dynarmic 默认关闭等待 CPU 接口。
- Linux 可用 SDL Unix-console 配置执行无显示服务契约；可见桌面窗口构建仍需 X11 或
  Wayland 开发依赖。
- 黄金帧使用无 GPU SoftwareSurface，不包含 ANGLE/SwiftShader；后者属于 M4。
- Agent Control 已有 stdio JSON-RPC；TCP/UDS/MCP adapter 后续按需要补齐。
- 当前只有本地 Git 仓库，三平台 hosted CI 尚无可执行远端，因此不能宣称远端 job 已绿。
- doctest 2.4.11 在 CMake 4.x 配置期会发出上游旧 policy 的 deprecation warning，
  不影响 warnings-as-errors 编译和测试。
