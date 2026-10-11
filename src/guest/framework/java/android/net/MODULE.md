# 模块：有界连接状态客户端

ConnectivityManager 保留 API19 类型常量与合法范围算法；普通查询经 native 事实工厂调用原版
NetworkInfo 构造与状态 setter，返回独立值对象。平台支持目录/权限/离线事实只由
private nativeGetNetworkInfo/nativeRequireOffline 提供，不依赖 Binder 或宿主网卡。

当前仅断开的逻辑 WiFi 类型；无活动网络，NetworkInfo 不可用、不连接。完整值类/
枚举/Parcel 来自固定 framework.jar；在线、事件与系统网络服务不在此客户端范围。
来源：本地 AOSP 4.4.4 ConnectivityManager.java，保留 Apache-2.0 头。
