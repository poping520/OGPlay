# 开发工具

## 远端增量构建

`remote_incremental.py` 使用一个明确指定的远端绝对目录持久保存源码、递归 submodule 和
CMake build 目录。调用者通过参数传入主机、账号、平台及工具路径，通过环境变量提供密码；
仓库中不得保存具体设备信息或凭据。

首次运行会初始化依赖并完整编译，后续运行只 fetch 当前 `main` bundle、更新发生变化的
submodule，然后由 CMake 增量编译并执行 CTest。远端目录必须专用于本工具；工具不会递归
删除它，也不会同步本地未提交改动。

核心依赖会递归初始化；`angle-prebuilt` 单独浅更新。ANGLE 源码及完整 GN 工作区只用于
维护者升级，普通增量验证不初始化源码 submodule，也不同步其 gclient 依赖图。

```text
python tools/remote_incremental.py --host <host> --user <user> \
  --remote-root <absolute-project-cache> --platform linux
```

macOS 或非标准安装可通过 `--cmake`、`--ctest` 指定可执行文件。密码默认从
`OGPLAY_REMOTE_PASSWORD` 读取；也可使用 SSH agent 或密钥认证。

## ANGLE 构建

`build_angle.py` 校验维护者 ANGLE checkout 与脚本内固定 commit 一致，按宿主生成固定
GN 参数，并只构建、验证 `libEGL` 与 `libGLESv2`。默认源码工作区是本地
`.local/angle-prebuilt-repo/angle`；也可用 `--source` 指定绝对路径。depot_tools 是本地
构建工具，不进入仓库；其路径通过 `--depot-tools` 传入。Windows 配置固定使用 MSVC 及其
原生标准库，Linux/macOS 使用 Clang。

首次同步并构建：

```text
python tools/build_angle.py all --depot-tools <absolute-depot-tools-path>
```

依赖已同步后的增量构建可分别执行 `configure`、`build`、`verify`；`print-config` 输出当前
平台的完整 GN 参数。默认产物目录是
`.local/angle-prebuilt-repo/angle/out/ogplay`，成功验证后写入带 ANGLE commit、目标平台、
GN 参数和产物清单的 `ogplay-angle-manifest.json`。
`--jobs` 同时限制 gclient 和 Ninja 并发；匿名同步触发上游限流时可降低该值后增量重试。

## ANGLE SDK 打包

`package_angle_sdk.py` 把验证后的 GN 产物转换为
`<platform>-<cpu>/<release|debug>` SDK。包内只保留公共头、链接/运行时文件、许可证及逐文件
SHA-256 清单；默认不携带 PDB，可用 `--include-symbols` 生成专用调试包。

```text
python tools/package_angle_sdk.py package \
  --windows-sdk-license <absolute-sdk-license.rtf>
python tools/package_angle_sdk.py verify \
  --sdk .local/angle-sdk/windows-x64/release
```

目标目录必须不存在，避免旧文件混入新包。普通项目配置只消费独立的
[`third_party/angle-prebuilt`](https://github.com/poping520/OGPlay-angle-prebuilt) 浅
submodule；源码树仅作为本脚本的维护者输入。

启用消费时设置 `OGPLAY_ENABLE_ANGLE=ON`。默认 SDK 根目录为
`third_party/angle-prebuilt`，也可用绝对 `OGPLAY_ANGLE_SDK_ROOT` 指向待发布包的根目录；
CMake 会按宿主选择平台/CPU，并逐文件验证清单。普通 Debug 构建仍使用默认
`OGPLAY_ANGLE_SDK_CONFIGURATION=release`。
