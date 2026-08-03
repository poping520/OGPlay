# OGPlay 最小 NDK 样例

这是 M4 累积集成出口测试使用的通用 `NativeActivity` APK，不依赖 Java/Kotlin 代码或
第三方库。它不是 M1 出口：运行它需要 M2 的 ELF/Bionic、M3 的生命周期边界和 M4 的
EGL/GLES 能力。
它以 GLES 2.0 绘制纯色画面；触摸位置改变红/绿分量，任意按键改变整组颜色，因而可以同时验证：

- `armeabi-v7a` ELF 装载与 A32/Thumb 执行；
- NativeActivity 生命周期；
- EGL/GLES 2.0 建窗与 present；
- 触摸和物理按键输入。

## 构建

需要 Android SDK、NDK、Python 3 和 JDK。构建过程只使用 SDK/NDK 已安装工具，不访问网络：

```powershell
python samples/minimal_ndk/tools/build.py `
  --sdk $env:ANDROID_SDK_ROOT `
  --ndk $env:ANDROID_NDK_ROOT
```

默认目标为 API 19 和 `armeabi-v7a`，产物位于仓库的 `out/minimal-ndk/`（Git 忽略）。
构建器会生成临时 debug key、签名 APK，并通过 `apksigner verify` 和 APK 内容检查后才成功退出。
