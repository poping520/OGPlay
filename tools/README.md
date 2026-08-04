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

## ANGLE SDK

ANGLE 源码构建、打包、许可证归档和发布自测全部由独立的
[`OGPlay-angle-prebuilt`](https://github.com/poping520/OGPlay-angle-prebuilt/tree/main/tools)
仓库负责；OGPlay 不再保存这些生产工具。普通项目只消费
`third_party/angle-prebuilt` 浅 submodule。

启用消费时设置 `OGPLAY_ENABLE_ANGLE=ON`。默认 SDK 根目录为
`third_party/angle-prebuilt`，也可用绝对 `OGPLAY_ANGLE_SDK_ROOT` 指向待发布包的根目录；
CMake 会按宿主选择平台/CPU，并逐文件验证清单。普通 Debug 构建仍使用默认
`OGPLAY_ANGLE_SDK_CONFIGURATION=release`。
