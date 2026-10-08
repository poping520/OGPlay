# 模块：有界 android.app Java 客户端

保留 API19 结果常量、每实例结果字段、两个 setResult、两参数启动转发及默认结果回调。
普通字段/monitor 在 BootDex；finish 在 Java monitor 下取结果快照，再提交 nativeFinish。
三参数启动进入 nativeStartActivityForResult，平台 resolver/命令队列与 session 栈编排
不在 Java 重写。其余现有 Activity 平台方法仍使用精确 integration overlay。

来源：固定 AOSP Android 4.4.4 Activity.java，保留 Apache-2.0 头；这是有界客户端，
不引入原版 ActivityThread/Instrumentation/ActivityManager/Binder/Fragment 运行时。
结果与窗口字段归各实例，启动/销毁命令和待交付结果通过 session roots 保活。
同包 standard 单窗口及支持限制见 runtime/session 契约，特殊 options/flags 明确失败。

NotificationManager 保留 API19 Context 字段/构造/from 与普通重载转发；取消及
拒绝发布进入两个 private native。backend 明确禁用发布且库存恒为空，只允许
当前应用取消；notify(null) NPE，其他发布失败并记账。无 Binder、系统通知或
PendingIntent 派发，也不纳入 Notification/Builder/RemoteViews 构建链。
来源：固定 API19 AOSP NotificationManager.java，保留 Apache-2.0 头。
