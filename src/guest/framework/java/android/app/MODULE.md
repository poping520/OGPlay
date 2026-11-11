# 模块：有界 android.app Java 客户端

保留 API19 结果常量、每实例结果字段、两个 setResult、两参数启动转发及默认结果回调。
API19 public onAttachedToWindow/onDetachedFromWindow 默认空方法归 BootDex；
受管 Activity 的 Window.Callback 时序由 session 编排，不表示 View 树附着。
Application 由固定 API19 ROM 提供，监听注册、注销与快照派发执行原版 Java。
Activity base onCreate/start/resume/pause/stop/save/destroy 调用对应派发；getApplication
为当前进程对象边界。应用的 super 调用决定派发时机。会话不主动生成保存状态事件。
普通字段/monitor 在 BootDex；finish 在 Java monitor 下取结果快照，nativeFinish 正常返回才置 mFinished，isFinishing/isDestroyed 读取实例 Java 字段，performDestroy 先置 mDestroyed 再虚派 onDestroy；进程关闭不合成 finish/onDestroy。
三参数启动进入 nativeStartActivityForResult，平台 resolver/命令队列与 session 栈编排
不在 Java 重写。getLayoutInflater 在 Java 委托 getWindow；getWindow 的精确 overlay 惰性建立每实例
Window，其他现有 Activity 平台方法仍使用精确 integration overlay。

来源为固定 AOSP Android 4.4.4 Activity.java，保留 Apache-2.0 头。ROM ActivityManager 的
内存与进程查询经本地 IActivityManager，支持边界见
[DexVM Android integration](../../../../../runtime/integration/dexvm_android/MODULE.md)。
结果与窗口字段归各实例，启动/销毁命令和待交付结果通过 session roots 保活。
同包 standard 单窗口及支持限制见 runtime/session 契约，特殊 options/flags 明确失败。

Activity 的 managed Dialog 缓存、创建/准备/Bundle 与 dismiss/remove 协议归 Java，
默认 onDestroy 关闭已管理实例；null 工厂返回 false，未知 dismiss ID 抛 IAE，remove
未知 ID 无操作。Dialog/AlertDialog/Builder 保存 Context/owner、普通字段、三按钮及
监听器，onCreate 一次、onStart/onStop/显示状态、取消与关闭区别沿 API19 客户端协议。
监听器经构造 Looper 的 Handler 排队；按钮旧显示代次不关闭重入的新一轮显示。
native 仅负责真实模态呈现/可见性/输入焦点，未支持配置记账抛 UOE。
plain String 标题/消息/三按钮走 native 模态投影；非零主题记录 UI 降级。
setContentView(View) 通过 Activity 应用附加窗口显示居中内容，hide/show/dismiss/取消键复用 Java 协议；
无 Activity 或布局资源重载明确失败。列表/Window facade、状态保存恢复与完整 DialogFragment 未支持。普通 Java Array/字段是唯一协议状态，GC 追踪
cached Dialog/Bundle/listener/Runnable；无 C++ shadow 管理表。来源：固定 API19
Activity.java/Dialog.java/AlertDialog.java 的有界改编，保留 Apache-2.0 头。

Notification 与 NotificationManager 整类执行 ROM。进程内 INotificationManager 支持当前应用
取消空库存中的通知，发布入口明确失败；此目录不提供通知类替换。
