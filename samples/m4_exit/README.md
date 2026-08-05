# OGPlay M4 综合出口样例

这是独立于 Work Unit 排期的 M4 出口测试资产。它比 `minimal_ndk` 覆盖更完整，提前定义
最终 GLES2 集成应能运行的通用 NativeActivity 负载；样例可以在 M4 开发过程中先构建，
但只有所需 GLES2 handler 全部接通后才能作为 OGPlay 出口证据。

## 覆盖范围

- EGL display/config/window surface/GLES2 context、查询、交换和逆序销毁；
- vertex/fragment shader 的创建、编译、链接、查询和 uniform；
- VBO、IBO、attribute、`glDrawElements`；
- 程序生成 RGBA 纹理、采样参数与纹理单元；
- viewport、scissor clear、alpha blending 和多批次绘制；
- `glGetString`、`glGetIntegerv`、`glGetError`、已知像素 `glReadPixels` 自检；
- 触摸坐标与按键输入，以及窗口重建/尺寸变化/焦点/销毁生命周期。

初始画面包含深蓝背景、左上橙色方向块、三条彩色条、非对称棋盘纹理、半透明叠层和
白色触摸标记。右上角状态灯为绿色表示 GL 查询与两个稳定像素读回通过，红色表示失败。
触摸会移动白色标记，任意按键会切换三组配色；初始画面不随时间变化，适合黄金帧。

## 构建 APK

需要 Android SDK、NDK r21e、Python 3 和 JDK；构建过程不访问网络：

```powershell
python samples/m4_exit/tools/build.py `
  --sdk $env:ANDROID_SDK_ROOT `
  --ndk $env:ANDROID_NDK_ROOT
```

默认目标是 API 19 与 `armeabi-v7a`，输出为
`out/m4-exit/ogplay-m4-exit-armeabi-v7a.apk`。构建器会验证 ELF32/ARM、关键 EGL/GLES2
动态导入、APK 元数据、native library、zip alignment 和签名。

## 出口判定

1. 初始帧与仓库后续冻结的 SwiftShader 黄金帧在允许阈值内一致，方向与状态灯正确。
2. 触摸四个象限时白色标记准确到达对应位置，黑边输入不得进入内容区。
3. 连续按键三次依次改变配色并回到初始配色。
4. 窗口重建后资源重新创建、场景一致；退出时 guest 线程和 EGL/GL 对象完整释放。
5. Windows、Linux、macOS 使用同一 APK 和软件后端通过上述检查。

当前样例不会替代正在推进的 M4 Work Unit，也不代表 OGPlay 已具备上述全部运行能力。
