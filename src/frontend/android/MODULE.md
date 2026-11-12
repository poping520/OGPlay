# 子模块：frontend/android

Android 原生入口，对应桌面的 `frontend/desktop`。`libogplay_android.so` 导出 `SDL_main`，
由 `SDLActivity` 按名称解析并在 SDL 线程调用；参数即 `GameRunOptions` 命令行（APK 路径及
run-apk 选项），经 `app::ParseGameRunArgs` 进入 `app::RunGame`，与桌面共用运行服务。

只调用 `ogplaycore` 公开外观，不直接依赖 runtime。一个 `:game` 进程只运行一个游戏，
退出即结束进程，不做进程内重启。失败经 logcat 上报。`<android/*>` 与 `<jni.h>` 只能出现在
本目录及 `hal/android`。

验证：`android/` 的 `assembleDebug` 与 APK 内容检查；运行行为需要真机。
