# 模块：有界 ContentResolver Java 客户端

来源：固定 AOSP Android 4.4.4 ContentResolver.java 与 ContentService.ObserverNode，
保留 Apache-2.0 头；类层级及公开观察者方法签名沿 API19。query/getType 仍由精确
integration overlay 提供既有语义；不引入原版 Provider/Binder 客户端。

进程级 static URI 树（authority 与 path segments）强引用观察者直到注销；重复登记
独立派发，注销移除该对象全部登记并裁剪空节点。锁内收集、锁外派发；通知本节点及
全部下级，上级仅 notifyForDescendents=true 时参与。self 通知取决于发起者的
ROM deliverSelfNotifications，派发调用 ROM dispatchChange(boolean, Uri)。

private native hasHandler 仅读取 ROM ContentObserver.mHandler，不建立宿主表或根。
有 Handler 时由原版代码投递；无 Handler 时改投主 Looper，区别于 Android 的 Binder
线程回调。已收集/排队的通知不因随后注销撤销；回调可重入登记/注销。

仅支持进程内显式 notifyChange 及 Settings.System 有效值变更；null notify URI 沿
API19 遍历全树，注册 null URI 抛 IAE，null observer 注册/注销抛 NPE。syncToNetwork
接受但无效果；无隐藏 userHandle 重载、跨用户、系统/其他应用 Provider 或
ContentProvider 事件源，无 Transport/Binder、跨进程通知或 sync 服务。
