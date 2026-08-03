# OGPlay

OGPlay 是面向 2010–2016 年安卓原生老游戏的跨平台兼容层。它翻译 guest ARM 指令，
并用宿主原生实现承接 Android API；它不启动 Android 系统镜像。

当前仓库处于 **M0：工程地基**。本阶段只建立可持续开发、可观测和可机器验证的框架，
不以运行游戏为目标。规划入口见 [`docs/roadmap/README.md`](docs/roadmap/README.md)，
当前交接见 [`docs/state/CURRENT.md`](docs/state/CURRENT.md)。

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
```

Linux/macOS CI 使用 `ci` 配置；Windows CI 使用 Visual Studio 2022 生成器。第三方依赖
由 CMake 固定版本获取，发行前须同步更新来源与许可证清单。

## 仓库入口

- `AGENTS.md`：AI 与开发者共同遵守的规则
- `docs/adr/`：只追加的架构决策
- `docs/modules/INDEX.md`：模块契约索引
- `docs/tasks/`：可独立验收的 Work Unit
- `capabilities.toml`：机器可读能力账本
- `src/`：正式版代码；DEMO 仅保留在本地 `docs/demo/` 作知识参照，不进入 Git
