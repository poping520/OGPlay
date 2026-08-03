# 模块：minimal_ndk sample

## 职责

提供不含游戏逻辑的 M1 出口 APK，用最小 NativeActivity 闭环验证 guest CPU、生命周期、
EGL/GLES 2.0 present 和输入。

## 公共边界

- Android API 19、`armeabi-v7a`、NativeActivity、GLES 2.0。
- 只依赖 NDK 平台库 `android`、`EGL`、`GLESv2` 和 `log`。
- 构建工具通过参数或环境变量定位，不记录开发机绝对路径。

## 不变量

- 触摸和按键都必须产生肉眼可判定的画面变化。
- 不包含游戏名、厂商名、商业资源或兼容性 quirk。
- APK、签名密钥和中间产物只进入 Git 忽略的 `out/`。

## 验证

`tools/build.py` 必须完成 NDK 编译、APK 打包、对齐、签名验证和 native library 内容检查。
