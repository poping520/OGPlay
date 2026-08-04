# OGPlay

OGPlay 是面向 2010–2016 年安卓原生老游戏的跨平台兼容层。它翻译 guest ARM 指令，
并用宿主原生实现承接 Android API；它不启动 Android 系统镜像。

**M0–M2 已完成验收。** 当前代码具备跨平台 ARM guest 内核、ELF32/ARM linker、
API 19/22/23 真实 Bionic、Android syscall 基线与统一 VFS；下一阶段进入 M3 JNI 与
最小框架。规划入口见 [`docs/roadmap/README.md`](docs/roadmap/README.md)，里程碑证据见
[`docs/state/`](docs/state/)，当前交接见
[`docs/state/CURRENT.md`](docs/state/CURRENT.md)。

## 本地构建

需要 CMake 3.25+、Ninja 和支持 C++20 的编译器。

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Windows + Visual Studio 2026 可使用：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

运行最小 CLI：

```sh
build/dev/ogplay --version
build/dev/ogplay capabilities
build/dev/ogplay agent-stdio
```

`agent-stdio` 使用一行一个 JSON-RPC 2.0 请求，可驱动 M0 的确定性空会话：

```json
{"jsonrpc":"2.0","id":1,"method":"session.open"}
{"jsonrpc":"2.0","id":2,"method":"run.step","params":{"frames":3}}
```

Linux/macOS CI 使用 `ci` 配置；Windows 使用对应的 MSVC 预设。第三方依赖以固定提交的
Git submodule 提供，发行前须同步更新来源与许可证清单。

## 仓库入口

- `AGENTS.md`：AI 与开发者共同遵守的规则
- `docs/adr/`：只追加的架构决策
- `docs/modules/INDEX.md`：模块契约索引
- `docs/tasks/`：按里程碑归档、可独立验收的 Work Unit
- `capabilities.toml`：机器可读能力账本
- `src/`：正式版代码；DEMO 仅保留在本地 `docs/demo/` 作知识参照，不进入 Git
