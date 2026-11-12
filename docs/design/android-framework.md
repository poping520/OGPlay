# Android 平台层

平台层让原样 ROM framework 在一个游戏进程里获得资源、窗口和设备服务。
ActivityThread、Looper、ViewRootImpl 与控件运行 ROM Java，
宿主在 native 和进程内服务接口处提供状态与资源。
这里的 ROM framework 指随附 API19 jar 中的 Android Java 实现，
不是启动完整 Android 系统镜像。

一次普通界面更新会经过 ROM 消息队列、View 遍历、Canvas native、窗口 Surface 与合成器。
把这些步骤连接起来，比复制一棵宿主控件树更能保留应用自定义 View、监听器和布局副作用。
相应地，ROM 真正调用的服务与 native 接口要逐项接通，
类能加载本身不代表界面已经可用。

## 进程启动：请求交给 ROM 执行

```text
session
  → AndroidAppProcess
  → ActivityThread.attach(false)
  → 本地 AMS / IApplicationThread
  → bindApplication → Application → 主队列首次空闲
  → launcher Activity → 主 Looper 持续派发
```

AndroidAppProcess 拥有 Manifest、ABI 清单、native process 和 VM bridge。
Create 先准备 API19 系统 ELF 环境，应用 SO 留到标准加载链实际请求时执行。
attach 后，本地 AMS（Activity 管理服务）经应用线程接口投递 bindApplication，
ROM 完成 Application 创建，主队列首次空闲之后再启动 launcher。

Java 主线程由独立宿主线程承载，主 Looper 持续处理消息。
session 排队生命周期请求并等待明确进展，ROM ActivityThread 决定具体回调和副作用。
[有界 DEX 与平台决定](../adr/dexvm.md#adr-0017)采用这条路径，
是为了避免手工重放生命周期遗漏应用初始化、排队工作和组件状态。

失败保留原始 Java 异常，并清理临时 root，即装配期间用于保活的 GC 根。
Application、Activity 与窗口对象属于同一 VM 对象图，
初始化失败不会跳过 Application 继续构造界面。
精确状态转换见 [session 契约](../../src/session/MODULE.md)。

## 一次界面失效怎样流过平台层

设想 ROM View 的状态改变并请求重画。
消息与 Choreographer 决定何时遍历，ViewRootImpl 进入测量、布局和软件绘制，
控件读取的尺寸和样式来自当前 Resources，Canvas native 使用 Skia 写入 Surface backing。
post 后窗口系统取得新的不可变内容，合成再发布给前端。

```text
View 状态变化 → ROM 消息 / 遍历 → Resources 与布局
                                      ↓
输入：WMS → ViewRootImpl → View ← Canvas 软件绘制
                                      ↓
                               Surface post → 合成帧
```

图中输入与绘制在同一 ROM View 对象图汇合，
但输入目标仍先由 WMS 按窗口事实选择。
这使自定义 View 可以同时保留自己的触摸状态、invalidate 与 onDraw，
无需把这些关系投影成宿主控件属性。后面几节按图中顺序分别说明服务、资源、
绘制、窗口和输入各自的设计。

## 本地 Binder 服务

ROM framework 的很多系统 API 最终查询一个服务接口。
真机服务通常运行在其他进程，经 Binder 传递调用；OGPlay 只托管游戏进程，
所以把需要的服务实现为同一 VM 内的 Java 对象，由 ServiceManager 返回。
Binder 在这里保留对象接口协议，不提供跨进程驱动。

AIDL 是 Android 根据接口描述生成客户端与服务端类型的机制。
ROM 的 AIDL 查找代码先用 queryLocalInterface 检查对象是否提供本地接口，
命中后直接虚派到该对象。平台层利用这条已有同进程路径，
使 ROM 调用者保持原样，也避免为本进程对象构造无意义的 IPC 往返。

这个选择意味着服务只能提供当前进程范围内的事实。
Parcel 仍可做进程内值编解码，但本地服务调用不经 Parcel 传输，
也不会因此建立 system_server 或系统包数据库。
边界依据是[进程级决定](../adr/runtime.md#adr-0001)，
精确支持见 [Android 平台契约](../../src/runtime/integration/dexvm_android/MODULE.md)。

AMS 管当前进程 Activity、Service、Receiver 与 provider 请求；
WMS 是窗口管理服务，保存窗口、Surface 与输入通道事实；
IMMS 是输入法管理服务，把活跃输入连接与宿主文本输入接起来。
Display、Settings 和通知等有各自有限的本地服务实现，服务名与 Context 查找使用同一平台表。

Manifest 封存当前 APK 的组件和权限信息，PackageManager 按这些事实回答。
同进程 provider 由 ActivityThread 安装并按 authority 登记，
Service 请求经 IApplicationThread 投递，动态广播使用 ROM dispatcher。
独立进程或 isolated Service 按不可解析通道回答，
理由和返回行为见[默认进程决定](../adr/runtime.md#adr-0170)。

设备查询也可以准确报告缺席，例如空传感器目录或断开的网络状态。
这种回答使 ROM 保留自己的空列表、权限和参数处理逻辑；
它不代表任意 USB、NFC、通知发布或网络操作都能成功。
真实缺口集中在[已知问题](../state/KNOWN-ISSUES.md#执行与平台)。

## 资源解析为什么有独立核心

```text
ROM Resources / AssetManager / Theme / TypedArray
  → native：句柄、数组、异常适配
  → runtime/resources：配置、引用、bag、样式
  → Loader：ARSC / 编译 XML / ZIP
```

ROM 保留公开对象与调用协议，资源核心处理二进制表与 API19 选择算法。
ARSC 是 APK 中的编译资源表，bag 是 style 等由父项和有序键值构成的复合资源。
同一 APK 的资源可能由清单解析、游戏库图标提取和运行时 Resources 访问，
资源核心独立后，三条调用链使用唯一的表实现，不会分别解释资源 ID 和配置。

核心只接收字节、来源身份与保活对象，不直接打开宿主路径，也不依赖 VM、JNI 或 VFS。
如果核心自己解析宿主路径，它就需要知道各个消费者的文件权限、缓存和寿命；
现在路径、VFS 和 Java 对象适配分别留在调用方，上层负责注入来源，
并负责句柄、数组及异常转换。

系统 framework-res 是进程内共享的只读来源，应用资源按会话持有。
字符串池解码缓存可以共享受锁保护的结果，Theme 和 parser 的可变状态则由各自调用方串行化。
AssetManager 的 cookie 标识归档来源，string block 是字符串池编号，两者用途不同；
manager、XML 与 StringBlock 的句柄寿命要显式连接，
不能从某个整数 cookie 推断任意字符串池仍然有效。
具体边界见[资源契约](../../src/runtime/resources/MODULE.md)。

Theme 按 XML、默认样式与主题优先级解析，并保留值的实际来源；
资源引用、bag 继承和密度选择共同决定控件的构造与外观。
例如布局创建控件时，ROM 读取属性，经 TypedArray 查询最终值，
核心选择匹配配置、解析引用并返回带来源的信息。
如果宿主直接提供固定皮肤值，应用主题和 framework 默认样式之间的覆盖关系就会丢失。
复用真实表需要更严格的归档校验和缓存失效，而不是按控件补常量。

Java 与 NDK 共用 manager 和配置。STORED 条目可借用原 APK 区间，
压缩条目使用有界流式解码。Asset FD 表示完整归档中的起点和长度，
媒体捕获该窗口后可以独立于原 FD 继续读取。
这样大资源不必为了改变消费者接口再完整复制一次。

## 软件绘制与同一份像素

ROM ViewRootImpl 负责测量、布局、invalidate 和软件 Canvas 调度，
View、Drawable 与控件算法仍在 Java 内执行。
Canvas、Paint、Bitmap 和几何 native 连接 Skia CPU，
会话中的分型资源表保存逻辑令牌，而不是把宿主指针写进 Java 字段。

Bitmap 像素由 Java byte[] 的稳定 payload 持有。
payload 指实际内容存储；Skia pixel ref 借用同一字节，NDK lock 返回对应 guest 地址，
GLUtils 也读取同一来源。因此 Java、软件 Canvas 和 native 图像操作不需要互相追赶影子副本。
设计依据见[稳定 backing 决定](../adr/dexvm.md#adr-0152)。

共享像素减少复制，代价是需要 GC 保活、像素版本和 NDK 写锁互斥。
NDK 锁定期间拒绝冲突 Canvas 访问，使资源的可写阶段可判断。
BitmapShader 保留自己的像素借用，原 Bitmap 回收或重配不使已保留存储悬空。
具体职责见 [raster 契约](../../src/runtime/raster/MODULE.md)。

Paint、Shader、滤镜和字体使用同一 Skia 状态。
系统字体由随附 payload 注入，按 family 与样式共享不可变字体面，
应用字体从 assets 或 guest VFS 读取。
[字体决定](../adr/runtime.md#adr-0169)避免依赖宿主字体与 locale，
以保留一致的文字度量；当前整形和双向重排仍按已知边界降级。

## 窗口事实与 Surface 生命周期

WMS 描述单个逻辑显示中的应用窗口，WindowSystem 保存 frame、层级、焦点、可见性和裁剪。
ROM 根据这些返回值执行布局，宿主并不维护一棵平行 View 树。
同一 app token 是同组应用窗口的身份依据，基础窗口、Dialog 和子窗口按窗口类型及关系排序，
而不是简单依赖创建先后。

这种排序保留“Activity 的基础 decor 尚未添加，对话框已打开”等合法关系。
窗口事实因此独立于像素生产管理，后提交的图像不自动成为最上层。
排序和层级关系的精确边界见
[窗口契约](../../src/runtime/integration/dexvm_android/MODULE.md#uisurface-与输入)。

Surface 与 SurfaceControl native 使用分型身份。
软件 Canvas 首次锁定或尺寸变化时请求全量重画，此后保留 dirty 外像素；
post 发布不可变图层，避免合成器看到生产者正在修改的内容。

隐藏、替换与 remove 遵守 ROM deferred-destroy 协议，即延迟到 ROM 完成销毁握手后再退役旧 Surface。
ROM SurfaceView 根据 Surface 有效性决定销毁回调，若隐藏窗口时立刻让旧 Surface 失效，
应用就得不到清理旧 EGL 资源的正常时机。配置重建时，延迟销毁保留握手所需的旧身份，
完成后再退役，新窗口则使用新端点和 backing。
旧引用要在替换和 remove 时明确释放，过期 endpoint 也要保持失效，
不能因为新窗口复用某个数值就被重新激活。
这项机制维护的是 Java 与 native 生命周期的一致性，不保证所有 context-loss 场景都可恢复。

```text
ROM 软件窗口 ─┐
EGL Surface ──┼→ WindowSystem 图层快照 → 合成 → FrameService
视频 Surface ─┘
```

合成先取得稳定图层内容，再应用 crop、透明区域、format、预乘和窗口 alpha。
符合完整覆盖条件的单层可直接借用像素，其他情况按正常层级合成，
所以一张 Surface 的存在不足以推断它能覆盖 Dialog 或透明区域。
借用条件和像素寿命见[图形文档](graphics.md#窗口交付与像素借用)。

## 从触摸到 View，以及文本输入

session 将逻辑输入提交给 WMS。DOWN 命中目标窗口后捕获整段手势，
后续 MOVE/UP 保持目标，窗口 flags 决定 OUTSIDE 交付；按键则交给焦点窗口。
InputEventReceiver 消费对应通道的事件，ROM ViewRootImpl 与 ViewGroup
继续决定 View 树内分发、拦截与取消。

窗口级命中和树内分发分开，保留了应用自定义 intercept 行为。
如果在宿主直接按坐标调用某个 View，滚动容器、Dialog 和 CANCEL 的协议就会被绕过。
NDK AInputQueue 与 Java MotionEvent 使用同一输入样本，
NativeActivity 完成事件后唤醒所属 Looper，NDK callback 范围另见契约。

文本走 IMMS 的活跃 IInputContext，它表示 ROM 当前接受编辑操作的输入连接。
SDL text/composing 沿该连接提交，前端据此启停宿主文本输入。
这使文本、退格和组合输入保留 ROM 编辑协议，而不是由按键映射器拼接字符串。
Dialog、AlertDialog 和 Toast 的内容与回调也继续由 ROM 管理。

## 配置变化与帧时钟

显示变化先取消当前触摸捕获，再更新窗口几何。
IWindow.resized 触发 ROM relayout，DisplayManager 回调使相关缓存失效，
Activity 按 Manifest configChanges 接收配置或执行 relaunch。
窗口重建沿 remove/add 与 Surface 退役路径更新 EGL backing，
前端在新合成尺寸发布后调整输入坐标。

配置选择、生命周期和输入几何彼此关联，却由各自权威处理。
资源核心只更新配置，不派发生命周期；ROM 决定重建，WMS 管窗口事实，
前端按已呈现几何映射坐标。旧方向的在途帧仍可能晚于重建归还，
回收按帧自身布局校验，而不是一律套启动尺寸。

Handler、MessageQueue 与 Choreographer 保留消息排序、同步屏障和动画遍历算法。
宿主提供 poll/wake，并在显示帧放行时排队一次性 vsync，
其时间戳与 System.nanoTime 使用同一 Clock。
理由见[帧源决定](../adr/session.md#adr-0165)：独立宿主定时线程会引入第二个动画时间基。

session 在推进后续 Clock 前等待登记的 Java EGL 生产者完成 swap、进入可观测阻塞或解绑退出。
等待不持 VM 锁，主线程生命周期阻塞时允许 GL 完成必要握手，
避免双方互相等帧。双向同步的具体边界见
[session 契约](../../src/session/MODULE.md)，原生自建 EGL 循环仍沿单向 pacer。

## 当前支持边界

硬件 Canvas、系统装饰、多显示和完整网页引擎不由软件窗口路径提供。
WebView 目前保留有界离线回调并作为 ROM View 参与布局，不能由构造成功推断网页可用。
文字、SurfaceTexture、真实 context loss、多个透明 Activity 与跨平台未验收项见
[窗口与媒体问题](../state/KNOWN-ISSUES.md#window-media)。

无返回值的纯展示请求可以记录 UI 降级。降级只影响对应装饰效果，
不跳过布局、监听器注册或对象返回，因此普通 ROM 算法能继续运行，
账本也保留了哪一处外观与真机不同。逻辑返回值与底层资源继续按真实状态或明确失败处理，
精确规则留在[平台契约](../../src/runtime/integration/dexvm_android/MODULE.md)。
本篇的流程连接不构成对完整 Android framework 的支持承诺。

## 契约、决定与操作

- [integration](../../src/runtime/integration/MODULE.md)、[Android 平台契约](../../src/runtime/integration/dexvm_android/MODULE.md)
- [资源核心](../../src/runtime/resources/MODULE.md)、[raster](../../src/runtime/raster/MODULE.md)
- [session](../../src/session/MODULE.md)、[ROM 边界决定](../adr/dexvm.md#adr-0017)
- [帧源决定](../adr/session.md#adr-0165)、[字体决定](../adr/runtime.md#adr-0169)
- [新游戏流程](../playbook/NEW-TITLE.md)、[已知问题](../state/KNOWN-ISSUES.md)
