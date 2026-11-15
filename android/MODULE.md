# 模块：android

Android 宿主工程（Gradle 根），只含 Java 外壳和打包配置。C++ 留在 `src/`，由根
`CMakeLists.txt` 构建；Gradle 不维护第二份原生源码或数据清单。

## 职责

- `launcher/`：主进程入口。`LauncherActivity` 在 WebView 中加载运行数据里的 `webui/gui`：页面只经私有 https 源
  由 `shouldInterceptRequest` 从运行目录提供，其他请求与导航一律拒绝，不开放文件/内容访问；注入的桥接脚本定义
  `window.rpc`，经 `LauncherBridge` 进入 `libogplay_launcher`。返回键先交给页面的 `ogplay-back` 事件，页面未处理才退出。
  文件与目录用 SAF（`ACTION_OPEN_DOCUMENT` 不限类型 / `ACTION_OPEN_DOCUMENT_TREE`）选取后不复制：文件保持打开
  （只留最近一个）、以 `/proc/self/fd/<n>` 交给原生层；目录登记为 `SafTrees` 句柄，原生导入时列出并逐个打开文件。
  启动时清掉旧版本留在 cache 的副本。系统不允许选存储根、Download 根及 `Android/data|obb`，需选其下的目录。
  Android 的 webui 构建只有手机布局：一律锁竖屏（平板暂同），视口固定设备宽度、不可缩放；API 30+ 全面屏绘制，
  状态栏/导航栏高度经 `__ogplayInsets` 写成 CSS `--safe-top/--safe-bottom`，只有软键盘缩小视图，WebView 不再收到 insets。
  debug 包开启 WebView 远程调试（chrome://inspect）。
- `game/GameActivity`：`:game` 进程，继承 `SDLActivity`，发布运行目录、用户数据目录和 locale 环境变量后进入
  `SDL_main`；参数为启动器启动计划的完整 argv。一个进程只运行一个游戏，结束即退出进程。游戏在独立任务（singleInstance）：
  启动器以 `RESUME_GAME` 把它带回前台（游戏已结束时该意图直接结束），以包内广播 `QUIT_GAME` 请它退出（`nativeSendQuit`，
  走与关闭确认相同的退出流程，后台时也能唤醒）；运行中不再启动另一个游戏，否则只会回到原游戏。
- `runtime/RuntimeData`：把 assets 的 `data/` 按安装版本原子解压到私有目录，供原生层按普通
  文件读取；`publishEnvironment` 发布两个根目录与 `OGPLAY_ANDROID_LOCALE`。

## 构建约定

- `minSdk` 29、`targetSdk`/`compileSdk` 36，只打 `arm64-v8a`；NDK 固定 r30。
- SDL Java 源码直接引用 `third_party/SDL/android-project`，不复制；SDL Java 与原生库版本
  因此始终一致。
- `externalNativeBuild` 构建 `ogplay_android`、`ogplay_launcher` 与 `ogplay_stage_data`；启动器页面来自
  `webui` 的 `npm run build:android`（输出 `data/webui/android/gui`，平台标记 Android），缺失或哈希不符时构建失败。运行库由 CMake 放到
  库输出目录，运行数据放到 `build/generated/ogplay-assets/data`，assets 合并排在原生构建之后。
- 原生 STL 固定 `c++_shared`：`ogplaycore` 的 C++ 接口跨动态库。
- 只改 C++ 时可用 CMake 预设 `android-arm64` 编译原生代码，不经 Gradle。

## 宿主行为

- 声明 `appCategory=game`，在大屏上保留游戏的方向约束；`enableOnBackInvokedCallback=false`
  并让 SDL 捕获返回键（官方称该退出方案是临时的）。系统返回键/手势不交给 guest，由 hal 转为关闭请求，
  与桌面关窗一样弹出关闭确认。
- `GameActivity` 以随系统深浅色的 DeviceDefault 对话框显示 SDL 消息框（≤3 个按钮）：Esc 默认按钮放在
  取消位，返回键或点外部即选它。关闭确认另有“回到游戏库”（中性按钮）：`showLibrary` 以启动器的 launch intent 把其任务带到前台，游戏留在后台运行。
- 不调用宿主 Android framework 服务来运行 guest；guest 仍只看到 API 19。

## 验证

`./gradlew assembleDebug` 产出 APK，再用 `zipalign -c -P 16` 检查 16 KB 对齐。运行行为需要
真机，见 [当前限制](../docs/state/KNOWN-ISSUES.md#title-validation)。
