# M0 工程地基验收

日期：2026-08-03

## 结论

M0 的代码、文档、测试与本地双工具链验收完成，可以开始 M1。Windows/MSVC 与
POSIX/Cygwin GCC 均在 warnings-as-errors 下编译并通过 27 项测试。Windows/Linux/macOS
托管 CI 矩阵已经配置；由于当前只有本地仓库，实际 hosted runner 结果需在首次推送后确认。

## 路线图出口条件

| 出口条件 | 结果 | 证据 |
| --- | --- | --- |
| AGENTS/docs/CURRENT 自举 | 通过 | `AGENTS.md`、`docs/INDEX.md`、`docs/state/CURRENT.md` |
| ADR 机制 | 通过 | ADR-0001 至 ADR-0006，只追加规则已落盘 |
| 单文件 ≤ 800 行 | 通过 | 最大生产源文件 386 行；DEMO 未迁移且被 Git 忽略 |
| 结构化日志与诊断 | 通过 | Ring/Console/File/JSONL、三种限流、帧标记、符号 provider、诊断包 |
| 裸输出门禁 | 通过 | `architecture.no_raw_output` |
| 三平台构建骨架 | 通过 | CMake presets + GitHub Actions Windows/Ubuntu/macOS matrix |
| doctest + CTest | 通过 | MSVC Release 27/27；Cygwin GCC Release 27/27 |
| 无 GPU 黄金帧 | 通过 | SoftwareSurface + 像素差 + average hash |
| 能力账本 | 通过 | TOML 账本、unimplemented/null-call 聚合、单调性门禁 |
| Agent Control 最小版 | 通过 | stdio JSON-RPC 的 session/run/sym/hle/log 闭环 |

## 实测命令

Windows/MSVC：

```powershell
cmake -S . -B build/msvc -G "Visual Studio 18 2026" -A x64 `
  -DOGPLAY_BUILD_TESTS=ON -DOGPLAY_WARNINGS_AS_ERRORS=ON
cmake --build build/msvc --config Release --parallel
ctest --test-dir build/msvc -C Release --output-on-failure
```

POSIX/GCC：使用 Cygwin GCC 14.4、CMake 4.2、Ninja，Release + `-Werror`，CTest 27/27。

## 明确不属于 M0

- SDL3 窗口/输入、4 GiB guest 地址空间、CPU 后端、真 guest 线程：M1。
- AOSP Bionic、ELF linker、syscall/VFS：M2。
- 完整 JNI/Java 框架：M3。
- ANGLE/SwiftShader 的 GLES 一致性黄金帧：M4；M0 的 SoftwareSurface 只验证基础设施。

## 外部发布门禁

首次建立远端仓库后，必须观察 Windows、Ubuntu、macOS 三个 hosted job 全绿；任一失败都
重新打开 M0，不得带失败进入 M1 合并。当前没有远端，故不伪造 hosted CI 结果。

