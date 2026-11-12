# 模块：android

Android 宿主工程（Gradle 根），只含 Java 外壳和打包配置。C++ 留在 `src/`，由根
`CMakeLists.txt` 构建；Gradle 不维护第二份原生源码或数据清单。

## 职责

- `launcher/`：主进程入口。当前只准备运行数据并启动内置演示游戏，游戏库界面尚未接入。
- `game/GameActivity`：`:game` 进程，继承 `SDLActivity`，发布运行目录和用户数据目录环境变量
  后进入 `SDL_main`。一个进程只运行一个游戏，结束即退出进程。
- `runtime/RuntimeData`：把 assets 的 `data/` 按安装版本原子解压到私有目录，供原生层按普通
  文件读取。

## 构建约定

- `minSdk` 29、`targetSdk`/`compileSdk` 36，只打 `arm64-v8a`；NDK 固定 r30。
- SDL Java 源码直接引用 `third_party/SDL/android-project`，不复制；SDL Java 与原生库版本
  因此始终一致。
- `externalNativeBuild` 构建 `ogplay_android` 与 `ogplay_stage_data`。运行库由 CMake 放到
  库输出目录，运行数据放到 `build/generated/ogplay-assets/data`，assets 合并排在原生构建之后。
- 原生 STL 固定 `c++_shared`：`ogplaycore` 的 C++ 接口跨动态库。
- 只改 C++ 时可用 CMake 预设 `android-arm64` 编译原生代码，不经 Gradle。

## 宿主行为

- 声明 `appCategory=game`，在大屏上保留游戏的方向约束；`enableOnBackInvokedCallback=false`
  并让 SDL 捕获返回键，使 guest 能收到返回事件（官方称该退出方案是临时的）。
- 不调用宿主 Android framework 服务来运行 guest；guest 仍只看到 API 19。

## 验证

`./gradlew assembleDebug` 产出 APK，再用 `zipalign -c -P 16` 检查 16 KB 对齐。运行行为需要
真机，见 [当前限制](../docs/state/KNOWN-ISSUES.md#title-validation)。
