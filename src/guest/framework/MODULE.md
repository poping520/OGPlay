# 模块：有界 Android framework guest Java

拥有需替换系统服务传输的 API19 客户端算法；编入 BootDex，由 DexVM 执行。
普通字段、计数、同步和异常归 guest 对象；平台租约/设备目录经显式 native 进入 integration。
不依赖宿主 JDK、Binder IPC、system_server 或手机服务。

图片资源、密度与流关闭协议由 API19 ROM BitmapFactory 执行，图形 native 经 Skia CPU 接入。

连接状态和 USB 查询由 ROM Manager 经进程内 AIDL 接口处理。离线网络与空 USB 目录的
宿主事实归 [DexVM Android integration](../../runtime/integration/dexvm_android/MODULE.md)。

WifiManager 和两类锁由 API19 ROM 提供，离线无线状态和权限检查走进程内 IWifiManager。
引用计数、held 状态、finalize 与每 manager 的锁数量上限由 ROM 管理。

ROM VideoView 通过 MediaPlayer 绑定窗口 Surface。视频解码、Clock 与音频输出由 integration 提供。

传感器客户端由 ROM 提供。SystemSensorManager 的 native 从同 NDK 的空设备目录枚举；
LegacySensorManager 从进程内窗口服务读取固定旋转。

NFC 由 ROM NfcAdapter 和 NfcManager 查询本地 PackageManager；当前平台没有 NFC feature。

ActivityThread、Activity、PhoneWindow、Dialog 和 Toast 执行 API19 ROM。进程内 AMS 管理任务、启动与结果，WMS 管理窗口。Toast 经本地 INotificationManager 回调 ROM ITransientNotification；系统通知发布入口明确拒绝。

ROM GLImpl 持有 API19 Buffer 强引用并转发 Java 方法；GLES native 经
[DexVM Android integration](../../runtime/integration/dexvm_android/MODULE.md) 使用调用线程的 EGL current。

ROM LayoutInflater、View 与 ViewRootImpl 拥有层级、布局与绘制。软件 Canvas native 使用 Skia；Surface buffer 提交后交由窗口合成器。

[Time 的 guest 客户端](java/android/text/format/MODULE.md) 为原版类实现 native 算法。
[时区数据适配](java/libcore/util/MODULE.md) 复用原版 ZoneInfo 读取 guest 固定 tzdata。
