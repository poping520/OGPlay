# UI 合成资源夹具

`drawable_resource.zip` 是固定日期、STORED 的合成 ZIP，不包含游戏数据：
`res/drawable/test.png` 为 2×1 RGBA PNG，两个像素分别是红与绿（alpha=255）；
`res/drawable/test.xml` 为 `<selector/>`，用于验证未支持的 XML Drawable 明确拒绝。

`rom_window/build.sh` 生成 `rom_window.apk`。设置 `AAPT` 指向 Android SDK build-tools 的
`aapt`，使用固定 API19 编译桩和 D8。Release 的 `rom_window_integration_tests.cpp` 使用此 APK
以及本地 API19 framework-res.apk；视频用仓库既有 H.264 夹具。
