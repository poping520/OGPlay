# Android 平台层

平台层连接原样 ROM framework 与进程内的资源、窗口和设备服务。
ActivityThread、Looper、ViewRootImpl 和控件执行 ROM Java；
宿主只在 native 和服务接口处提供游戏进程需要的事实与资源。

## 进程启动与职责

```text
session
  → AndroidAppProcess
  → ROM ActivityThread.attach
  → 本地 AMS / IApplicationThread
  → bindApplication / launcher Activity
  → 主 Looper
```

AndroidAppProcess 拥有 Manifest、ABI inventory、native process 与 VM bridge。
Create 先建立系统 ELF 进程，不执行 APK root SO。
Application 创建完成并到达主队列首次空闲后才启动 launcher。
Java 主线程由独立宿主线程承载，持续派发 ROM 主队列。

session 排队启动、挂起、恢复与关闭请求。
Activity 生命周期由 ROM ActivityThread 调用，不由宿主复制回调顺序。
Application、Activity 和窗口对象都在同一 VM 对象图中。
失败时清除临时 root 并保留原始异常，不能跳过 Application 继续运行。

## 本地 Binder 服务

ServiceManager 返回本进程的 Java 服务对象。
AIDL 客户端通过 queryLocalInterface 获得接口，直接虚派执行。
这条路径不经 Parcel 传输，不建立 Binder driver 或服务进程。
Parcel 仍可用于进程内值编解码，但不因此提供 IPC。

AMS 管理当前进程的 Activity、Service、Receiver 和 provider 请求。
WMS 管理窗口事实、Surface 身份与输入通道。
IMMS 连接活跃输入上下文与宿主文本输入。
Display、Settings、通知等接口各有有界本地服务。
服务名与 context 查找共用平台表，避免 dump-surface 与运行时声明不同。

Manifest 封存本 APK 的组件和权限事实。
PackageManager 不查询其他宿主应用，也不建立系统包数据库。
同进程 provider 由 ActivityThread 安装，AMS 按 authority 登记。
同进程 Service 通过 IApplicationThread 投递创建和命令。
动态广播由 ROM dispatcher 处理，跨进程目标保持范围外语义。

设备查询可以准确报告缺席。
无传感器、USB、NFC 或网络连接不等于任意操作都返回成功。
具体请求仍按权限、参数和服务能力执行或明确拒绝。
通知发布等系统展示不由本地库存查询推断支持。

## 资源表与配置

```text
ROM Resources / AssetManager / Theme / TypedArray
  → native 令牌适配
  → runtime/resources
  → Loader 的 ARSC / XML / ZIP 数据
```

Java 拥有资源对象的状态与调用协议。
独立资源核心负责资源表、字符串池、配置选择、bag 与编译 XML。
它不打开宿主路径，也不依赖 VM、JNI 或 VFS。
上层注入归档字节、路径身份和保活对象。

系统 framework-res 以只读来源共享，应用资源按会话持有。
AssetManager 将 cookie 与字符串 block 分开编号。
Theme 按 XML、默认样式和主题优先级求值，并保留最终来源。
资源引用、bag 继承与密度选择不能用常量皮肤替代。

Java 与 NDK 使用同一个 manager 和配置。
STORED 资源可借用原 APK 区间；压缩资源走有界流式解码。
Asset FD 表示完整归档加起点和长度，调用方必须保留窗口语义。
媒体捕获区间后可以独立于原 FD 寿命继续读取。

资源配置来自逻辑显示、density、方向、区域和 API19 事实。
宽高变化更新资源管理器，再由 ROM 决定重载和布局。
资源核心本身不派发 Activity 生命周期事件。

## 软件绘制

ROM ViewRootImpl 驱动测量、布局、invalidate 和软件 Canvas。
View 与 Drawable 的普通算法不在宿主重建。
Canvas、Paint、Bitmap 和几何 native 连接 Skia CPU。
每个会话有分型资源表，令牌不包含宿主指针。

Bitmap 像素由 Java byte[] 的稳定 payload 持有。
Skia 借用同一像素，NDK lock 返回同一数组的 guest 地址。
Canvas、GLUtils 和 native bitmap 访问没有第二份影子像素。
NDK 锁期间拒绝并发 Canvas 访问，以维持明确互斥边界。

Paint、Shader、滤镜与字体使用同一 Skia 状态。
系统字体由 payload 注入，按 family 与样式共享不可变字体面。
不读取宿主字体目录，也不由宿主 locale 决定字形。
文字整形、双向重排等差异记录 UI 降级，范围见已知问题。

## 窗口与 Surface

WMS 只描述单个逻辑显示中的应用窗口。
ROM 计算布局输入，WindowSystem 保存 frame、层级、可见性、焦点和裁剪。
同一 app token 的 base window、Dialog 和子窗口按明确规则排序。
系统 inset 和动画策略是进程配置事实，不启动系统装饰组件。

Surface 与 SurfaceControl 的 native 使用分型身份。
软件 Canvas 首次锁定或尺寸变化请求全量重画，其后保留 dirty 外像素。
post 发布不可变图层，合成期间不持有窗口事实锁执行 Java。
SurfaceView、EGL 与视频向同一个窗口标识提交生产端内容。

```text
ROM 软件窗口 ─┐
EGL Surface ──┼→ WindowSystem 图层快照 → 合成 → FrameService
视频 Surface ─┘
```

合成应用 crop、透明区、format、预乘和窗口 alpha。
符合完整覆盖条件的单层可以直接借用像素 lease。
不能用一层普通 Surface 的存在推断它覆盖其他窗口。
隐藏与 remove 的 Surface 退役遵守 ROM deferred-destroy 协议。
旧 endpoint 失效后不能被新窗口身份复活。

## 输入派发与文本

session 将逻辑输入提交给 WMS。
DOWN 命中窗口后捕获整段手势，按 flags 交付 OUTSIDE。
按键交给焦点窗口，ViewRootImpl 与 ViewGroup 决定树内分发和拦截。
宿主不维护一棵与 Java View 并行的点击树。

InputEventReceiver 消费对应通道的事件和批次。
MotionEvent 与 NDK AInputQueue 来自同一输入样本。
NativeActivity 的事件完成后唤醒所属 Java Looper。
NDK callback 支持边界不因 Java 队列接入而扩大。

IMMS 使用活跃 IInputContext 提交 SDL 文本和 composing。
前端根据输入连接启停宿主文本输入。
Dialog、AlertDialog 与 Toast 的内容及回调仍由 ROM 管理。
服务只保存窗口、时限和交付事实。

## 配置变化与帧时钟

显示变化先取消当前触摸捕获，再更新 WMS 几何。
IWindow.resized 请求 ROM relayout，DisplayManager 回调失效缓存。
Activity 按 Manifest 的 configChanges 决定配置派发或 relaunch。
窗口重建沿 remove/add 和 Surface 退役路径更新 EGL backing。
前端在新合成尺寸发布后调整输入坐标映射。

ROM Handler、MessageQueue 与 Choreographer 负责消息排序。
宿主只提供 native poll/wake，并排队 vsync。
同步屏障、异步消息和 idle callback 不在宿主另写算法。
Java 与 NDK Looper 使用同线程的等待和唤醒底座。

session 放行显示帧后，在推进下一段 Clock 前等待登记的 Java EGL 生产者。
完成 swap、明确阻塞、解绑、退役或线程退出都可解除等待。
查询等待不持 VM 锁，不能把排队取锁误认成仍在绘制。
主线程生命周期阻塞可以解除 swap 节流，避免反向等待。
原生自建 EGL 线程仍沿 native pacer，双向同步缺口单独保留。

## 范围与阅读入口

硬件 Canvas、系统装饰、多显示和完整网页引擎不由软件窗口路径承诺。
WebView 当前保留有界离线客户端行为，位置和绘制由 ROM View 层级承载。
局部 UI 设置降级必须记录；逻辑返回值与底层资源错误仍明确失败。
游戏运行状态与验收边界见已知问题，不能由本篇结构说明推断兼容。

- [integration](../../src/runtime/integration/MODULE.md)、[Android 平台契约](../../src/runtime/integration/dexvm_android/MODULE.md)
- [资源核心](../../src/runtime/resources/MODULE.md)、[raster](../../src/runtime/raster/MODULE.md)
- [session](../../src/session/MODULE.md)、[ROM 边界决定](../adr/dexvm.md#adr-0017)
- [帧源决定](../adr/session.md#adr-0165)、[字体决定](../adr/runtime.md#adr-0169)
- [新游戏流程](../playbook/NEW-TITLE.md)、[已知问题](../state/KNOWN-ISSUES.md)
