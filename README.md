# OGPlay

OGPlay 是面向 2010–2016 年安卓原生老游戏的跨平台兼容层。它翻译 guest ARM 指令，
并用宿主原生实现承接 Android API；它不启动 Android 系统镜像。

当前能力与验证基线以 [`docs/state/CURRENT.md`](docs/state/CURRENT.md) 和
[`capabilities.toml`](capabilities.toml) 为准。规划入口见
[`docs/roadmap/README.md`](docs/roadmap/README.md)，里程碑证据见
[`docs/state/`](docs/state/)，当前交接见
[`docs/state/CURRENT.md`](docs/state/CURRENT.md)。

## 本地构建

需要 CMake 3.25+、Ninja 和支持 C++20 的编译器。

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev -R "<affected-test>"
```

Windows + Visual Studio 2026 可使用：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc -R "<affected-test>"
```

代码改动默认只构建受影响目标并运行直接相关的单点/定向测试；只有用户明确要求时才运行
全量 CTest。

## 主面板（游戏启动器）

Windows 构建后可直接双击 `build/windows-msvc/Debug/ogplay.exe`。产品由
`ogplay.exe`、`ogplay-cli.exe` 和共享应用库 `ogplaycore.dll` 构成，三者同版本交付。
主程序默认打开游戏库，启动游戏时创建自身 `--run-game` 子进程；关闭启动器不结束游戏，
产品运行无需 CLI 文件。Linux/macOS 当前只构建 CLI 与共享库，GUI 宿主尚未接入。

```powershell
cmake --build --preset windows-msvc --config Release --target ogplay
build/windows-msvc/Release/ogplay.exe --library-root .local/my-library
```

构建/安装产物携带 `data/` 中的 Profile、quirk、能力账本、API 19 guest payload 与 Web UI；
资源以宿主 EXE 为锚点定位，开发时允许源码树回退。GUI 支持导入单体 APK、设置与启动，
移除条目只删除库内副本，保留外部数据和持久存档。`--smoke-frames N` 是有界 WebView
加载/RPC 冒烟入口，不代表游戏帧数。`OGPLAY_BUILD_GUI=OFF` 可只构建共享库与 CLI。

命令行与自动化统一使用 `ogplay-cli`：

```sh
build/dev/ogplay-cli --version
build/dev/ogplay-cli capabilities
build/dev/ogplay-cli agent-stdio
build/dev/ogplay-cli run-apk game.apk --mcp
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
- [`docs/adr/`](docs/adr/README.md)：按主题归并、保留编号的架构决策
- `docs/modules/INDEX.md`：模块契约索引
- `docs/tasks/`：按里程碑归档、可独立验收的 Work Unit
- `capabilities.toml`：机器可读能力账本
- `src/`：正式版代码；DEMO 仅保留在本地 `docs/demo/` 作知识参照，不进入 Git
