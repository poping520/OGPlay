# 子模块：frontend/android

Android 原生入口，对应桌面的 `frontend/desktop`。`libogplay_android.so` 导出 `SDL_main`，
由 `SDLActivity` 按名称解析并在 SDL 线程调用；参数即 `GameRunOptions` 命令行（APK 路径及
run-apk 选项），经 `app::ParseGameRunArgs` 进入 `app::RunGame`，与桌面共用运行服务。

`libogplay_launcher.so`（`launcher.cpp`）供主进程 `LauncherActivity` 的 WebView 使用：以 `GuiPlatform::Android()` 构造
`GuiRpcService`，库根为宿主用户数据目录，`gui.log` 写在库根。所有 Handle/Poll 在 Java 桥的单一工作线程执行；
RPC 文本与路径以 UTF-8 字节数组跨 JNI。启动回调把启动计划的 argv 交给 `GameActivity`；`Poll` 依据运行锁与
`last-run.json` 是否改写判断游戏结束（30 秒内既未持锁也未改写视为启动前失败），产出 `ogplay-exit` 事件。
选择不复制：选中的文件由 Activity 持有其 fd，路径为 `/proc/self/fd/<n>`（HAL 复制该 fd 读取，APK 快照与压缩包
都经它读），`label` 为文件名；选中的目录为句柄 `saf-tree:N`，`label` 为卷内路径（`/sdcard/…`、`/storage/<卷>/…`，
宿主据此推断挂载位置），`open_tree` 返回 `SafTreeSource`：经桥列出一次文件（`listTree`），导入时逐个由 Java 打开
并交出 fd（`openTreeFile`）读取写入。导入线程是原生线程，首次回调 Java 时附着 VM、线程结束时分离。
`GuiPlatform::Android()` 不允许引用方式，数据包总是复制入库。

只调用 `ogplaycore` 公开外观，不直接依赖 runtime。一个 `:game` 进程只运行一个游戏，
退出即结束进程，不做进程内重启。失败经 logcat 上报。`<android/*>` 与 `<jni.h>` 只能出现在
本目录及 `hal/android`。

验证：`android/` 的 `assembleDebug` 与 APK 内容检查；运行行为需要真机。
