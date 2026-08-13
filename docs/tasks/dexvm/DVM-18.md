# DVM-18 · android.* intrinsic 首批 + dex_activity 生命周期装配

## 目标（一句话）

按 pilot 测量面交付 android.* intrinsic 目录与 handler（挂接真实会话状态），
以及 `DexActivityLifecycle` 生命周期反转驱动与 run-apk 接线。

## 变更

- `runtime/integration/dexvm_android.{h,cpp}` + `dexvm_android_handlers.cpp`：
  Context/Activity/Window/View/GLSurfaceView(+Renderer 捕获)/Resources
  （getIdentifier/openRawResource 由 arsc 事实驱动）/Configuration/
  AssetManager/InputStream(+File/FileInput/Output/DataOutput 会话内存文件)/
  Log→结构化日志/AudioManager/Wifi/Sensor 族/Telephony（确定性身份）/
  SoundPool+MediaPlayer→存量 mixer（resid 即资源键）/Bundle/Intent 族/
  Toast/MotionEvent(槽位事实)/KeyEvent/Locale/Thread.sleep(推进统一时间)/
  SMS+网络=记账明确失败；`platform.system.*`（时间/loadLibrary/exit）。
- `session/dex_activity_lifecycle.{h,cpp}`：Start = 实例化 launcher →
  `<clinit>`(loadLibrary) → onCreate/onStart/onResume → onSizeChanged →
  onWindowFocusChanged(true) → onSurfaceCreated/Changed；帧 = 捕获
  renderer 的 onDrawFrame + present；输入 = MotionEvent/onTouchEvent、
  onKeyDown/Up；suspend/resume = focus+onPause/onResume；teardown =
  surfaceDestroyed → onStop/onDestroy → 存量 finalize/close 顺序。
- run-apk：dex_activity 分支（arsc 声音 loader、桥装配、统一 driver 垫片
  复用既有帧/MCP 循环）；interpreter tick 预算修正为每顶层调用（04 §6）；
  嵌套帧引用悬空修复（frames 改 deque）。

## 验收（已过）

- pilot 全生命周期解释执行直至主界面（见 DVM-19 三轮 gate）；
  full CTest 558/558 无回归。
