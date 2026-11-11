# 模块：有界 Android framework guest Java

拥有需替换系统服务传输的 API19 客户端算法；编入 BootDex，由 DexVM 执行。
普通字段、计数、同步和异常归 guest 对象；平台租约/设备目录经显式 native 进入 integration。
不依赖宿主 JDK、Binder IPC、system_server 或手机服务。

图片资源、密度与流关闭协议由 API19 ROM BitmapFactory 执行，图形 native 经 Skia CPU 接入。

连接状态和 USB 查询由 ROM Manager 经进程内 AIDL 接口处理。离线网络与空 USB 目录的
宿主事实归 [DexVM Android integration](../../runtime/integration/dexvm_android/MODULE.md)。

WifiManager 和两类锁由 API19 ROM 提供，离线无线状态和权限检查走进程内 IWifiManager。
引用计数、held 状态、finalize 与每 manager 的锁数量上限由 ROM 管理。

本地 VideoView 适配器以 Java 字段保存监听器、播放目标和代际。回调传入初始化的
MediaPlayer 子类，所需控制绑定同一视频实例；不支持系统媒体服务、字幕或网络源。

传感器客户端由 ROM 提供。SystemSensorManager 的 native 从同 NDK 的空设备目录枚举；
LegacySensorManager 从进程内窗口服务读取固定旋转。

NFC 由 ROM NfcAdapter 和 NfcManager 查询本地 PackageManager；当前平台没有 NFC feature。

Activity 的 [Java 客户端](java/android/app/MODULE.md) 保存每实例结果字段及普通转发；
两个 native 只提交平台启动/结束，返回栈与结果交付由 session 编排。
Dialog/AlertDialog 和 managed Dialog 的缓存/字段/回调也在此客户端；native 仅投影
有界模态 UI 与输入，不接入完整 Android WindowManager。

ROM NotificationManager 经进程内 INotificationManager 取消当前应用通知。库存为空，
发布入口明确拒绝。

ROM GLImpl 持有 API19 Buffer 强引用并转发 Java 方法；GLES native 经
[DexVM Android integration](../../runtime/integration/dexvm_android/MODULE.md) 使用调用线程的 EGL current。

[view 客户端](java/android/view/MODULE.md) 提供 LayoutInflater 的 Context、clone/from、
同步/重载以及 ContextThemeWrapper 服务缓存；native 复用固定注册表的 XML 构建，
构建与窗口替换分离，不加载完整 PhoneWindow 或反射工厂。


[Time 的 guest 客户端](java/android/text/format/MODULE.md) 为原版类实现 native 算法。
[时区数据适配](java/libcore/util/MODULE.md) 复用原版 ZoneInfo 读取 guest 固定 tzdata。
