# 模块：m4_exit sample

## 职责

提供独立于 M4 Work Unit 编号的复杂 NativeActivity 出口载荷，以单一确定性场景同时覆盖
EGL 生命周期、常用 GLES2 对象/状态/绘制/读回路径以及触摸、按键输入。

## 公共边界

- Android API 19、`armeabi-v7a`、NativeActivity、GLES 2.0。
- 只依赖 NDK 平台库 `android`、`EGL`、`GLESv2`、`log`，不包含 Java/Kotlin 或第三方资源。
- 构建工具通过参数或环境变量定位 SDK/NDK，不记录开发机绝对路径。

## 不变量

- 初始场景不使用时钟和随机数；同一逻辑尺寸的稳定区域必须可用于黄金帧比较。
- 左上橙色方向标记、右上自检灯和程序生成纹理必须保持非对称，禁止掩盖上下翻转。
- 绿色自检灯只在 GL 查询、已知区域读回和错误状态全部通过后显示，否则显示红色。
- 触摸必须移动白色定位标记；按键必须切换调色板并产生肉眼可判定的变化。
- APK、签名密钥和中间产物只进入 Git 忽略的 `out/`。

## 验证

`tools/build.py` 必须完成 NDK 编译、精确 EGL/GLES2 导入集合检查、APK 打包、对齐、签名
验证和内容检查；`--contract-only` 必须在不依赖 SDK/NDK 时核对 42 个 GLES2 调用均存在于
仓库 IDL。
在 OGPlay 完成所需 GLES2 handler 前，本模块只保证 APK 可构建，不将运行失败伪装成通过。
