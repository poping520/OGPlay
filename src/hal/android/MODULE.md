# 模块：hal/android

## 职责

实现 Android 宿主专属 HAL 适配。POSIX 通用部分直接复用 `hal/linux`，这里只放 Android
与 Linux 行为不同的实现。

## 依赖与边界

- 可依赖公开 HAL 接口、SDL3、Bionic 与 NDK 宿主 API；`<android/*>` 只能出现在本目录及
  `frontend/android`。
- 不包含 guest、游戏语义，也不调用宿主 Android framework 服务来运行 guest。
- `HostExecutableDirectory` 读取 Java 外壳发布的 `OGPLAY_ANDROID_RUNTIME_DIR`（其下有
  `data/`），未发布时明确失败；`/proc/self/exe` 在 Android 上指向 `app_process`，不可用。
- `HostExecutablePath` 由运行目录派生，不代表真实可执行文件；Android 不派生子进程。
- `HostUserDataDirectory` 读取 `OGPLAY_ANDROID_USER_DATA_DIR`，未发布时返回空。

## 测试

构建与打包见 `android/MODULE.md`；运行行为需要真机。
