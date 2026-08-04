# 开发工具

## 远端增量构建

`remote_incremental.py` 使用一个明确指定的远端绝对目录持久保存源码、递归 submodule 和
CMake build 目录。调用者通过参数传入主机、账号、平台及工具路径，通过环境变量提供密码；
仓库中不得保存具体设备信息或凭据。

首次运行会初始化依赖并完整编译，后续运行只 fetch 当前 `main` bundle、更新发生变化的
submodule，然后由 CMake 增量编译并执行 CTest。远端目录必须专用于本工具；工具不会递归
删除它，也不会同步本地未提交改动。

核心依赖会递归初始化；ANGLE 只初始化顶层浅 submodule，避免普通增量验证无条件拉取其
完整 GN/gclient 依赖图。启用 `OGPLAY_ENABLE_ANGLE` 的任务应在同一持久目录中单独准备
ANGLE 依赖和 GN 产物，并通过 `OGPLAY_ANGLE_BUILD_DIR` 交给 CMake。

```text
python tools/remote_incremental.py --host <host> --user <user> \
  --remote-root <absolute-project-cache> --platform linux
```

macOS 或非标准安装可通过 `--cmake`、`--ctest` 指定可执行文件。密码默认从
`OGPLAY_REMOTE_PASSWORD` 读取；也可使用 SSH agent 或密钥认证。
