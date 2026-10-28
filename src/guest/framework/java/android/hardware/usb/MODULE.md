# 模块：无 USB 设备的 API19 Java 客户端

UsbManager 源自 AOSP Android 4.4.4，保留 Apache-2.0 头、常量、公开签名与
(Context, IUsbManager) 构造器；Context 初始化并缓存 manager，service 为 null。
IUsbManager、UsbAccessory、UsbEndpoint、UsbInterface 由固定 framework.jar 提供。

private static native 边界报告无设备：不向 Bundle 写设备，当前配件为 null；
Java 保留列表遍历及配件分支。openDevice 保留设备名读取与异常捕获，不创建连接；
权限查询返回 false，权限查询与设备打开均记账 dexvm.usb_transport。
权限请求、配件打开、功能与存储设置记账并抛 UnsupportedOperationException，
不保存或派发 PendingIntent。属性查询保持 ROM SystemProperties 算法。

不引入 UsbDevice、UsbDeviceConnection、UsbRequest 实现及其 native，不提供
宿主 USB 透传、UsbService、Binder、热插拔事件或设备通信。
