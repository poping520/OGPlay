# 模块：有界 android.app Java 客户端

保留 API19 结果常量、每实例结果字段、两个 setResult、两参数启动转发及默认结果回调。
普通字段/monitor 在 BootDex；finish 在 Java monitor 下取结果快照，再提交 nativeFinish。
三参数启动进入 nativeStartActivityForResult，平台 resolver/命令队列与 session 栈编排
不在 Java 重写。其余现有 Activity 平台方法仍使用精确 integration overlay。

来源：固定 AOSP Android 4.4.4 Activity.java，保留 Apache-2.0 头；这是有界客户端，
不引入原版 ActivityThread/Instrumentation/ActivityManager/Binder/Fragment 运行时。
ActivityManager 仅为内存查询的有界客户端，不接入原版 AMS。MemoryInfo 保留 API19
字段/构造与普通 Parcel 值算法；nativeRead 获取进程快照，lowMemory 使用原版 HOME/
CACHED 分级比较。manager 正常构造并保留 Context/Handler，进程枚举明确拒绝且记账。
结果与窗口字段归各实例，启动/销毁命令和待交付结果通过 session roots 保活。
同包 standard 单窗口及支持限制见 runtime/session 契约，特殊 options/flags 明确失败。

NotificationManager 保留 API19 Context 字段/构造/from 与普通重载转发；取消及
拒绝发布进入两个 private native。backend 明确禁用发布且库存恒为空，只允许
当前应用取消；notify(null) NPE，其他发布失败并记账。无 Binder、系统通知或
PendingIntent 派发。Notification 有界值对象保存普通字段、两种原版值构造及 extras；
系统模板与 Parcel 传输经 private native 记账抛 UOE，null 参数按 Java 校验。
Builder/Style/Action/RemoteViews 执行未纳入；typed 字段声明不代表完整类型反射闭包。
来源：固定 API19 AOSP NotificationManager.java，保留 Apache-2.0 头。

Notification 客户端来源：固定 Android 4.4.4 Notification.java，保留 Apache-2.0 头；
原版值构造和字段初始化保留，模板/Parcel 以明确不支持边界替代，无系统通知服务。
