# 模块：有界 Android framework guest Java

拥有需替换系统服务传输的 API19 客户端算法；编入 BootDex，由 DexVM 执行。
普通字段、计数、同步和异常归 guest 对象；平台租约/设备目录经显式 native 进入 integration。
不依赖宿主 JDK、Binder IPC、system_server 或手机服务。

WifiLock/MulticastLock 沿用 AOSP 引用计数与 held 独立状态、模式切换和过量释放语义；保留二进制类名。
以普通 monitor 代替 Binder monitor，租约只记录应用持锁事实，不启用网络或宿主网卡。
MulticastLock finalize 按 API19 切换非计数再释放；两类锁合计每 manager 最多 50 个租约。
组播释放只作用于本对象，区别于原 WifiService 同 UID 清理；边界见 ADR-0092/0093。
WorkSource、组播传输及无线服务不在当前支持范围。

算法来源：[AOSP Android 4.4.4 WifiManager.java](https://android.googlesource.com/platform/frameworks/base/+/android-4.4.4_r2.0.1/wifi/java/android/net/wifi/WifiManager.java)，
保留 Apache-2.0 头；改编与支持边界由 ADR-0092/0093 记录。

本地 VideoView 适配器以 Java 字段保存监听器、播放目标和代际。回调传入初始化的
MediaPlayer 子类，所需控制绑定同一视频实例；不支持系统媒体服务、字幕或网络源。

传感器客户端见 [hardware](java/android/hardware/MODULE.md)：Sensor/SensorManager 保留
固定 API19 原版源码；Legacy 的映射和转换在 Java，仅替换构造器 WMS 旋转查询。
Context 选择 LocalSensorManager 空设备后端，不提供 SensorService 或真实事件队列。

NFC 的 [Java 客户端](java/android/nfc/MODULE.md) 只支持类型链接及无适配器发现；
查询与 manager 状态在 BootDex，平台边界报告无设备，不提供 NFC 通信或回调。
