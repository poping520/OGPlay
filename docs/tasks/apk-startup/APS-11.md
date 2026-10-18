# APS-11 · libandroid NDK 缺口与 preflight 导入审计

## 目标

补齐 API19 无传感器、Java Surface 窗口及 APK Asset 目录/缓冲区边界；preflight 一次报告所选 ABI 的导入缺口。

## 依赖与设计锚点

- APS-10、DVM-220、DVM-237；复用 native library source closure、ELF namespace、Looper/VFS pipe 与窗口 registry。
- 本任务按用户要求作为一个 WU；不修改 linker 强弱规则、不接宿主传感器/Binder、不实现 callback 派发。

## 语义出处

- `.local/aosp/framework/base/native/android/sensor.cpp`：getInstance 非空，getSensorList 输出空数组地址；createEventQueue 忽略 addFd 返回；hasEvents 的 revents 非 POLLIN 返回 -1。
- `.local/aosp/framework/native/libs/gui/SensorManager.cpp`：进程单例、空目录 malloc(0)、defaultSensor 无匹配 null。
- 同目录 `SensorEventQueue.cpp`、`BitTube.cpp`、`Sensor.cpp`：队列持有 fd/Looper；EAGAIN 读取为零；sensor getter 只对有效设备合法。
- `.local/aosp/framework/base/native/android/native_window.cpp` 与 `core/jni/android_view_Surface.cpp`：fromSurface 取现有 native window，成功增加引用。
- `.local/aosp/framework/base/native/android/asset_manager.cpp`：openDir 始终返回迭代器，只枚举普通文件，缓存名称至下次调用；getBuffer 返回整个资源。
- `.local/aosp/framework/base/libs/androidfw/AssetManager.cpp::openDir`、`Asset.cpp::_FileAsset::getBuffer/_CompressedAsset::getBuffer`：目录合并排序、惰性物化、压缩资源完整解压，缓存随 Asset 释放。

## 变更与边界

- 审计复用 NativeLibrarySources 的 APK/basename/SONAME/system 闭包选择，逐根构造原 Bionic namespace，调用原 version-aware lookup；不修改 linker，不以无关 APK 导出掩盖缺口。按库聚合全部 GLOBAL/WEAK 导入，强缺口退出 1，弱符号单列、同步结构化日志。
- NDK 15 个传感器导出与 Java 共享注入目录；当前只支持空设备，非空 provider 明确失败。manager 稳定非空、list 写非空空数组；queue 持 VFS pipe 两端及 Looper 引用，共用 add/removeFd，callback 拒绝 warn/记账，无派发。creator thread/进程停止回收，token 不复用。
- fromSurface 在 VM 锁内转换 JNI→active holder→现有 windows_ registry acquire，changed 同步几何、destroyed 退役；EGL 拒绝旧代，留存引用仍可释放，不建立第二窗口模型。
- APK 目录直接条目排序、只枚举文件，目录 token/缓存名称受预算和 manager 生命周期约束。getBuffer 惰性读取完整 STORED/Deflate，32 MiB 上限、guest 只读稳定缓存、close 释放，read 仍为窗口读取。
- 扩展既有 `dexvm.sensor_absent_facade`，因 Java/NDK 是同一个空设备事实与支持边界；不新增重复能力项。Windows、宿主传感器与 Looper callback 派发不在本次实测/实现范围。

## 验收

- 受影响目标构建、定向 ARM/loader/integration 回归；记录 cases/assertions。
- TML preflight 基线 20 个强符号，完成后零强符号；弱符号单列。
- macOS release 原 library run 复现一次，只记录下一首错。
- capabilities check、architecture.documentation_layout、git diff --check。

## 结果（机器可判定，已达成）

- 基线 `.local/aps11-preflight-before.log`：退出 1；strong_missing=20、weak_missing=16，与任务表一致。

- 实现后 `.local/aps11-preflight-after.log` 与 `aps11-preflight-library.log` 均退出 0：
  strong_missing=0、weak_missing=16；`.local/aps11-verify-preflight.py` 对实现前精确符号集合及两条入口输出/退出码作机器断言，通过。
- hasEvents 反向验证：临时将空队列 -1 改为 0，只跑 `APS-11 API19 sensor hasEvents preserves negative idle quirk`，
  退出 1，1 case / 7 assertions 中 1 条按预期失败（`0 == -1`）；源码逐字节恢复。
  日志 `.local/aps11-mutation-test.log`、`aps11-mutation.json`。此为固定 API19 语义，不增加 Profile quirk 开关。
- 本机库实例原沙盒缺少空 `obb` 语义目录，最初 library preflight 在 native 前拒绝启动。
  已完整备份到 `.local/aps11-sandbox-before/`，仅补建该空目录，所有既有文件 SHA-256 不变；
  `.local/aps11-sandbox-preparation.json` 保存机器断言，未清理存档，也未改动沙盒代码。

- macOS release 受影响目标 `ogplay-cli ogplay_tests` 构建退出 0，恢复后构建耗时 161.518 秒；
  `.local/aps11-restored-build.log/json`。未构建桌面 GUI 目标，未运行全量测试。
- `ogplay_tests --test-case="APS-11*,DVM-237*,DVM-195*,DVM-220*,BND45*,Android link preflight*" --no-colors`
  退出 0：**29 cases / 1519 assertions**，耗时 2.866 秒；`.local/aps11-tests.log/json`。
  覆盖 ARM32 五参数队列调用/栈对齐、Java/NDK 同一注入目录对象、root forThread 非空、callback
  拒绝 warn/记账、无效句柄/不复用/线程与进程退役、Surface JNI 与窗口引用/几何/销毁及 EGL
  拒绝旧代、根/子/缺席目录、STORED/Deflate buffer 内容/游标/只读/上限/释放，以及导入强弱与 NEEDED/别名/系统/boundary scope。

- macOS `build/release` 原命令 `./ogplay-cli library run com.amazon.games.tilmorningslight-2`
  退出 1，耗时 3.294 秒；第 0 帧已越过 libunity 导入/装载，进入已注册的
  `com/unity3d/player/UnityPlayer.nativeFile(Ljava/lang/String;)V`（guest_thread=1）。
  原 `Unable to load library: /data/app-lib/libunity.so` / `ASensorManager_getInstance` 首错消失；
  下一首错为 **`JNI guest class is not declared: android/app/ApplicationErrorReport`**，本 WU 未修复。
  日志 `.local/aps11-tml-run.log/json`，机器断言 `.local/aps11-tml-proof.json`。
- 真实 title 在上述 JNI 首错停止，尚未执行传感器 queue 调用序列，不能宣称其后续 guest
  线程已经关联 Looper；真实 ARM fixture 已验证 root 的 forThread 非空、同目录及给定五参数
  调用序列，空 Looper 的明确失败另有回归。后续遇到未关联线程时仍须独立处理，不能伪造 Looper。
- reached-fault 证据不等于 title gate/绘制/游戏兼容验收。Windows 未实跑；不支持宿主传感器、
  真实事件及 Looper callback 派发；非空设备目录保持明确失败。

- `python3 tools/capabilities.py check`、`architecture.documentation_layout`、UTF-8 与
  `git diff --check` 均通过；日志 `.local/aps11-capabilities.log`、`aps11-documentation.log`、
  `aps11-diff-check.log`。仅更新 CURRENT 的 TML 行，保留工作区原有 CLI 帮助文本修改。

## 验收后修正（2026-10-17）

- 验收发现 destroyed 在 surfaceDestroyed 回调前退役 native window，偏离 AOSP SurfaceView
  （回调期间 Surface 仍有效）。改为回调结束后退役，fromSurface 在回调期间按 Surface.isValid
  仍可取得窗口；同步 android 与 dexvm_android MODULE。
- 新增 `APS-11 SurfaceView native window retires after surfaceDestroyed callbacks`（双解释器）：
  回调内断言尚未退役且 isValid 为真，回调后恰好退役一次。
- Release `--test-case="APS-11*,DVM-245*,DVM-237*,DVM-195*,DVM-220*,BND45*,Android link preflight*"`
  35 cases / 2049 assertions 通过。`android_egl_sharing_tests` 3 项与 `BND37 GLSurfaceView publishes
  API19 context config` 在 APS-11 之前的二进制上同样失败，属既有问题，未处理。
- TML 用 `run-apk --ephemeral-sandbox` 复跑，首错仍为 ApplicationErrorReport。库实例沙盒运行后
  再次缺少 `obb` 语义根，`library run` 无法打开，属既有沙盒缺陷，另行排查。

## 后续 BootDex 配方补齐（2026-10-17）

- 从固定 `framework.jar` 整体导入 ApplicationErrorReport 及其内部类，复用 BootDex→JNI
  身份/方法注册，无 C++ 空壳、新 native 或 overlay。类集合与制品身份仍以配方和 manifest 为准。
- 语义依据：AOSP `framework/base/core/java/android/app/ApplicationErrorReport.java` 的
  `getErrorReportReceiver(Context,String,int)`；先读 Settings.Global，缺省或显式 0 时返回 null，
  不查询安装器/外部接收方。未覆盖的报告启用路径不因类准入而宣称完成。
- `CrashInfo(Throwable)` 依赖 FastPrintWriter，完整 Writer/charset 依赖另行收口；
  JNI FindClass 合法缺类的 null/pending/ClassLoader/no-init 语义亦为独立工作，本次未修改。
- BootDex build/check、payload 身份校验及类族层级/native/overlay 审计通过，证据
  `.local/aps12-bootdex-{build,check}.log`、`aps12-payload-check.log`、`aps12-family-proof.json`。
- Release 仅构建 `ogplay_tests` 目标；复用 `JNI framework services use VM methods objects and state`
  夹具，验证类族共享 JNI 身份/方法归属及缺省、显式关闭两种查询：switch/threaded 后端均通过，
  **1 case / 1724 assertions**（0.504 秒）。日志 `.local/aps12-tests-build-final.log`、
  `aps12-tests.log/json`。capabilities check、文档布局、UTF-8 与 diff 检查通过；未跑全量测试。
- 使用同一 APK 的 `run-apk --ephemeral-sandbox` 复现一次（持久沙盒仍有上述既有根目录问题），
  退出 1、耗时 2.812 秒：旧 JNI 缺类错误消失，下一首错为
  **`class is not available: Landroid/graphics/SurfaceTexture;`**，位于 `player/e.<clinit>` 的
  new-instance，经 `player/l.<clinit>` 回到 UnityPlayer 构造器 dex_pc=271。日志和机器断言见
  `.local/aps12-tml-run.log/json`、`aps12-tml-proof.json`。此为 reached-fault，不是 title gate。
