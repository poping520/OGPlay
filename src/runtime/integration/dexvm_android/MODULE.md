# 子模块：runtime/integration/dexvm_android

## 职责与装配

为 dex_activity 提供有界 android/javax.microedition intrinsic，将 session 的 VFS、UI、ANGLE、
媒体、线程和平台事实注入 DexVM。不运行完整 Android、Binder/system_server、外部包数据库、
Play 服务或手机系统服务；移动宿主按 ADR-0086 通过 HAL 接入，禁止 title/厂商分支。

catalog.cpp 是唯一注册点，每类一个 Declare_<类名>(context)，shape/handler 在所属
content/os/view/graphics/gl/media/database/device 等 family TU 同址。shared.* 只放跨类 helper；
禁止静态自注册、转发命名空间、字符串 handler id、单类 TU 或 misc 聚合。非 Android family
归 core，平台事实经 DexVmAndroidContext/AndroidCoreIntrinsicServices 显式注入。

## 共用不变量

- 普通 Java 算法/字段/数组归 BootDex；catalog 只保留 native/平台事实边界，不重复 class 或
  普通方法。工厂先初始化类再调用原构造器；host state 必须 owner-attached、有 trace/sweep
  和 clone policy。session root、object owner、UiNodeId、native token 不混用。
- guest 引用强类型、字段用 bound token、flags 来自 access_flags.h；override 显式声明，
  不复制继承成员。native token 只存 Java long，GC/teardown 登记清理，不保存 host 指针，
  浅 clone 不得提前释放共享 token。
- 资源/设备/路径/身份来自显式 context，不探测 host 环境。classpath 只读封存 BootDex/APK：
  bootstrap 不见 APK，application parent-first；查找返回归档 guest 路径/entry，读取校验
  已登记来源并复用严格 ZIP/CRC。BootDex 的稳定身份为 `/system/framework/bootdex.jar`，
  APK 使用 context 的 package_resource_path；不映射宿主文件，也不全局跨 VM 缓存。
- 未实现记账并抛可捕获 Java 异常，禁止伪成功；结构化日志不吞异常，生命周期事件不丢代际。
  Clock 为唯一时间源，一个 guest 对应一个 host 线程，共用 VM 锁；图形 ANGLE、窗口/输入 SDL3。

## Context、组件与权限

- Context→ContextWrapper→Application/Service/ContextThemeWrapper→Activity 层级固定；
  process Application/base Context/ClassLoader/descriptor 身份稳定，wrapper 虚派委托 base。
  Activity 仅顶层，isChild=false，不支持嵌入式 child Activity。
- Intent component、Activity intent 以普通字段为准。同包显式启动受检；隐式只在 sealed
  当前 APK 内匹配 action/category、无 data/type、含 DEFAULT 的唯一 enabled Activity。
  零匹配抛 ActivityNotFoundException，多匹配/潜在 data 匹配明确失败。alias 保留组件身份、
  实例化 target；不支持跨包 resolver/chooser/Instrumentation/ActivityManager。
- PackageManager 只发布当前 APK sealed Manifest/path/label/permission/feature；值对象来自
  BootDex，integration 写受检字段。meta-data 的 value 解码 ARSC 类型，resource 保留 ID；
  application 与组件 metaData 分别物化。nativeLibraryDir=/data/app-lib，selected-ABI 库只读挂 VFS。

| 查询 | 支持与失败边界 |
| --- | --- |
| getInstallerPackageName(String) | 仅当前直接加载的 APK，未记录 Android 安装器，返回 null；未知包（含空串、system、null）按 API19 抛 IllegalArgumentException；不推测商店来源 |
| getResourcesForApplication(String) | 仅当前包，共用 Context 的应用 Resources/AssetManager；未知包（含空串、system）NameNotFoundException，null NPE；不支持跨包与其他重载 |
| getPackageArchiveInfo | 仅当前封存只读 APK，路径经 guest VFS 规范化；0/GET_ACTIVITIES/GET_DISABLED_COMPONENTS；归档 UID=-1、安装目录 null、安装时间 0；路径缺失/非普通文件返回 null，未登记归档/缺事实/未知 flags 记账失败 |
| getPackageInfo | GET_ACTIVITIES/META_DATA/PERMISSIONS 组合；activities 仅请求时发布，按声明顺序保留 alias、默认排除禁用项，否则 null |
| getActivityInfo/getReceiverInfo/getServiceInfo | 当前包完整组件名；0、GET_META_DATA、GET_DISABLED_COMPONENTS，按应用/组件启用状态过滤；缺失 NameNotFoundException |
| getPermissionInfo | 仅 Manifest `<permission>` 定义；0/GET_META_DATA；仅请求或已授权未定义仍 NameNotFoundException |
| queryBroadcastReceivers | flags=0、非空 action、同一 filter category 子集，不要求 DEFAULT；排除禁用项，同组件取首个匹配，API19 稳定排序 |
| resolveService | 非空 action、无 component/data/type/categories、flags=0；仅能确定无候选，返回 null；潜在匹配或未知条件失败 |
| bindService/unbindService | 缺席查询沿用原规则；另支持主线程 BIND_AUTO_CREATE、component-only、当前 APK 启用的同进程服务，见 ADR-0084 |

广播查询返回真实 ArrayList/ResolveInfo/ActivityInfo，保留 priority/match/isDefault/filter label/icon；
permission/exported 不限制声明查询。外部包、selector/component/data/type、未解析潜在 data、
未知 flags/缺少 inventory 不能伪装零匹配，须记账抛 UnsupportedOperationException。服务声明
查询不启动进程；正匹配 resolver、started service 与跨进程服务未实现。显式绑定复用主 Looper，
异步构造/attach/onCreate/onBind 后交付原本地 Binder；同组件共享实例，最后解绑执行
onUnbind/onDestroy，排队后解绑取消回调。连接、Intent 与服务对象由 session GC roots 保活；
null onBind 不产生连接回调，退出拒绝新绑定并释放残留服务。连接早于 bind 结果登记，失败仍可
解绑一次，并作为 Context GC 强边。定向反射不解析无关签名。

权限定义、requestedPermissions 和 granted 集合独立。checkPermission 仅查询 self PID/UID，
外部身份/未授予为 denied，null permission 抛 IAE；无 Binder 时 checkCallingPermission 始终
denied，CallingOrSelf 查询 self，enforce 拒绝抛 SecurityException。wrapper 委托 base，不建 UID
数据库、Binder caller 或运行时授权系统。

code/resource 共用只读 /data/app/<package>-1.apk，files/cache 位于 app VFS。getFileStreamPath
与 openFileInput/Output 共用单文件名校验，前者不创建，后者 PRIVATE 覆盖/APPEND 追加。
getDir(name, PRIVATE) 在已安装的 app 数据根下创建 app_<name>，wrapper 委托 base；
每次返回普通 File，mkdir 失败仍返回路径，实际存在性/IO 由 VFS 决定。名称含 / 抛 IAE，
null/空串按 API19 拼接；缺 VFS 或非 PRIVATE 模式明确失败并记账，不提供跨应用权限模式。
getObbDir(s) 返回 primary external 的 Android/obb/<package> 并经 VFS overlay 建目录。

Settings 公开协议/转换/moved-key 路由来自 BootDex，只 overlay NameValueCache 存储：Secure
读取稳定沙盒身份，System 用进程隔离表，Secure/Global 特权写入记账并 false；无 Binder
SettingsProvider、跨用户/观察者/host 设置。SystemProperties 仅受审 native 边界。

## PendingIntent 与闹钟取消

当前 APK 可创建 service/broadcast PendingIntent 令牌；创建不解析或启动目标组件。
原版 BootDex Intent 复制/filterEquals/replaceExtras 保持快照及匹配语义，extras 不参与身份；
种类、创建包、requestCode、ONE_SHOT/fill-in flags 参与身份，NO_CREATE/CANCEL_CURRENT/
UPDATE_CURRENT 为查找控制位。CANCEL_CURRENT|NO_CREATE 按 API19 返回旧的已取消令牌。
仅普通 Intent 与 BootDex Uri；selector、content MIME provider 推断和未知 flags 记账失败。
registry 由 VM execution lock 串行访问，不永久保活；活 wrapper trace Intent，GC sweep 移除
记录，闹钟条目作为 session roots 保活令牌。包装身份规范化到同一对象；不支持 Parcel/克隆令牌。
`getSystemService("alarm")` 返回单例，cancel 移除匹配闹钟而保留令牌；null 或无闹钟正常返回。
PendingIntent.cancel 使令牌失效，查询不再匹配；退出在 VM 锁内停止创建并清理两类记录。
闹钟注册/调度、send 与 started service 未提供，记账抛 UnsupportedOperationException。

## 资源、Parcel、数据库与日志

- AssetManager/Resources 只读 APK/ARSC/AXML；open 返回 ByteArrayInputStream，openFd 仅 STORED
  entry 的逻辑 FD+区间，失败映射 IOException/NotFoundException。应用 Resources/AssetManager
  是同一对象对，mAssets 为 GC 强边；getSystem 用独立对象对且不读应用 APK。
- getXml 的 AXML 仅 getEventType/next/getName/getText/close；其余完整 XmlPullParser 接口
  记账抛 UnsatisfiedLinkError。文本 XML 用 BootDex KXml，不进入二进制 AXML reader。
- 系统 getString/getInteger 每 context 一次从 BootDex 封存归档读取 system-resources.json
  （META-INF/ogplay，schema 1/API19，stored，≤64 KiB/256 项）。配方拥有名称/类型/消费者/
  配置，ID 从最终 DEX 提取，build/check 验证。无 APK/host 回退或旧 ID 别名；缺失/重复/
  畸形映射抛 ISE，未知 ID/错类型抛 NotFoundException。journal=DELETE，WAL 不支持。
- Configuration 稳定对象使用同一 VM Locale.getDefault 并调用原版 setLayoutDirection；
  TextUtils 仅确认 ROOT/en/zh 的 LTR，其他 locale 在 likely-subtags 接通前记账失败。
- Parcel 普通协议归 BootDex，integration 提供 VM 隔离 backing/游标/本地 Binder 对象记录与
  生命周期；记录为 GC 强边，marshall 拒绝含对象，FD/远程 Binder 不支持。接口长度先校验。
  Binder 线程策略按 context 保存 mask/GATHER，只记录；StrictMode 仅无 violation 查询/清理，
  不运行检测，violation 编解码失败；异常编码仍执行 BootDex。
- SQLite 使用固定真实引擎，主库/journal/临时文件只经 VFS；原 Java 栈拥有事务/连接池/Cursor，
  native 负责 ABI/token/错误码映射/受审配置。损坏由 SQLite 与原处理器判定；teardown 取消并
  关闭 host 资源，CursorWindow 有界填充且遵循 requiredPos/countAllRows，时间经进程 Clock。
- SharedPreferences 按 context/package 持久化 XML，editor/listener 遵循对象与 state-table 契约。
- Log Throwable 重载/getStackTraceString 共用 guest printStackTrace(PrintWriter)，flush 后
  取字符串，保留虚派/异常身份；null 或 cause 链含 UnknownHostException 子类返回空串，
  不按消息判断。EventLog 写入保留 API19 类型/截断/返回值；无服务时读取失败。均用结构化日志。

## UI、NativeActivity 与输入

- live View 对应唯一 UiNode，hierarchy/id/visibility/layout/text/style、动态 attach/detach 与
  查找共用 UiTree；Java 字段修改不自动 traversal。getContext 返回构造/inflation 的 mContext。
  addView(width,height) 虚派默认 LayoutParams、写 BootDex 字段后走统一 attach；不复制参数对象。
- Background getter/setter 保持 guest Drawable 身份；Button 构造/inflation 默认非空背景，
  普通 View 可为 null。alpha 仅经仍有效的 callback node 重绘，替换/清空解除旧 callback。
- setText、Editable、host EditText 共用文本事务：过滤/变更区间/watcher 快照/同步回调/失效
  只有一份实现。inflation 先投影 API19 默认属性和已登记 textAppearance/ProgressBar 样式，
  再 style、XML 显式属性；未知 style 失败。LinearLayout(Context,null) 默认水平，null Context
  抛 NPE，非空 AttributeSet 构造失败；不宣称完整主题/LayoutInflater 工厂。
- 焦点由 lifecycle/UiTree 唯一拥有，requestFocus 重载共用可聚焦/触摸模式/祖先状态/转移/
  清除/监听器语义；无 Sensor/SystemUI/WMS 时不伪造回调。滚动条样式仅 mViewFlags 的
  0x03000000 位、默认 INSIDE_OVERLAY；scroll-container/开关只存状态，不实现绘制/inset 重算。
- InputMethodManager 无 InputConnection 会话；restart/show/hide 先从当前窗口 owner 与
  attached UiTree 焦点投影 checkFocus。无候选时 restart 返回、show/hide 为 false；有候选
  时创建连接记账失败，包括无关 View/token 请求。不得按非编辑器跳过 dummy connection。
  不发布 served View、输入连接或软键盘可见状态；不支持 proxy、ResultReceiver、桌面 IME。
- clickable 实例方法可覆盖；setOnClickListener 登记前虚派 isClickable/setClickable(true)，
  null 仍设 clickable 但解绑监听器，setClickable(false) 不删监听器。触摸点击要求 clickable，
  程序化 InvokeViewOnClick 不要求；基础 onTouchEvent 只消费、不伪造点击，listener/子类不被禁用。
- clipChildren/clipToPadding 默认 true，变化仅 draw dirty；绘制遵守祖先、输出和自身裁剪，
  不改布局尺寸或扩大触摸命中。pointer dirty 先 layout，再 clipped reverse-Z/deepest-first
  命中并虚派回调；键盘从 SDL scancode 映射 API19 keyCode/Unicode/meta/repeat。
- SurfaceView holder 按 attach/host surface generation 严格 created→changed→destroyed；
  detached 子树不提前收事件或关闭 host surface。Canvas/Bitmap 为 ARGB，post 发布软件帧。
  getSurfaceFrame 返回 holder 字段持有的唯一 BootDex Rect；初始为零，created/changed
  回调前与 late holder 发布 managed surface 像素尺寸，destroyed 保留末次尺寸。旧 holder
  不跟随后续 generation；不从 UiTree bounds 推导独立 buffer，不新增全局 frame 根表。
  SurfaceView(Context,null) 构造复用 View 的 UiNode/Context 初始化；null Context 抛 NPE，
  非空 AttributeSet 记账失败，不宣称 XML/theme 或三参数 defStyle 构造支持。
- NativeActivity/NativeContentView 执行原 Java，仅 overlay 私有 native，经 NativeActivityRuntime
  接当前进程。takeSurface/takeInputQueue 支持 null/切换，先销毁旧 owner 再发布新对象；
  首次遍历前解除不收旧事件。Callback2 changed 后 redraw；主线程首次/dirty 遍历向已附着
  observer 分发 global layout，getLocationInWindow 使用 UiTree screen frame。
- InputDevice/MotionRange 普通算法归 BootDex；getDeviceIds/getDevice 投影启动前逻辑目录，
  未知 id/轴为 null，空目录返回新数组。不探测 host/InputManager/Binder；hasKeys/震动/
  Parcel 失败，完整反射未验证，不宣称摇杆、触摸板或热插拔。
- KeyCharacterMap 与 KeyEvent 修饰键算法归 API19 BootDex；load 按进程目录查询，未知 id
  回退虚拟键盘，目录无键盘则明确失败。FULL 虚拟键盘采用受检 AOSP Virtual.kcm 数据，
  非键盘为空 SPECIAL_FUNCTION 映射。native 字符/标签/数字/match/fallback 查询共用不可变
  selector；KeyEvent Unicode 查询复用该映射，宿主显式 Unicode 保留。动态宿主布局、
  Unicode 分类/重音合成、事件合成、物理键能力与 system-key policy 记账失败。
- 输入派发从 AndroidBoundaryInput 快照物化 Java 事件。MotionEvent 的私有 primitive
  数组归 VM/GC 所有，保存触点身份、轴、offset、精度、纳秒时间与历史；getter 不读取
  实时宿主状态。Java 毫秒/纳秒接口按 API19 转换，索引越界抛 IllegalArgumentException；
  recycle 清除本对象的数组引用，后续读取明确失败。KeyEvent 保留 down/event time、flags/source。

## GLES/EGL 与渲染调度

Java GLES/EGL 通过 session managed 冷入口复用 [native boundary](../../boundary/MODULE.md)
的 binding/registry/current/error，参数错误进 guest 锁存，host 契约故障硬失败。

- EGL10/EGL14 wrapper 映射真实 display/config/context/surface handle；config 逐项包装，
  可投影显式请求的颜色位数但不替换 native handle。current、延迟销毁、pbuffer、share、
  扩展串/错误以 registry 为准；数组 offset 受检，仅回写指定切片。未支持入口保留精确 EGL error。
- GLES30 原版类/常量/overload surface 按 delta catalog 适配，缺 handler 记账失败。String、
  sync long、mapped direct Buffer、active/uniform-block/transform-feedback 查询及 String[]
  使用专用编组；GLES20 glGetString 接受 SHADING_LANGUAGE_VERSION，GLUtils 按四种 Bitmap.Config
  编码。GLU 使用 API19 数学与 GL10 虚派，不入 native 目录，offset/失败不回写受检。
- GLSurfaceView 逐 View 保存版本/config。其有界 GLThread 是真实 Java Thread，run 经显式
  hook 进入 session 驱动；queueEvent 经唤醒 hook 通知该线程。线程不自动 prepare Looper。
  queueEvent 保活 Runnable，在 current GL 渲染线程、
  renderer callback 前 FIFO 执行，不依赖绘帧；requestRender/WHEN_DIRTY 单次消费请求。
  PreserveEGLContextOnPause 默认 false、普通 boolean 字段、方法可覆盖、可先于 renderer 配置；
  onPause/onResume 当前仅 lifecycle 停帧，按该字段拆分 Surface/Context 重建和 context-loss 待实现。

## Looper、线程与 WebView

- Handler/Looper/HandlerThread/Timer/AsyncTask 共用 scheduler，uptime deadline 相同时按 sequence
  FIFO；主 Looper 在 lifecycle safe point 泵送，子 Looper 在对应 host 线程执行，不同步伪装 post。
  设备 Clock 默认确定性 60 秒 boot-age，无 suspend 时 uptime/elapsedRealtime 共用事实。
- Java prepare/主 Looper 经 prepare_native_looper 显式 hook 关联 native 线程；主 Looper
  仅在首次建立主 Looper 时调用关联 hook，既有身份查询及主消息泵不重复 prepare。
  Java scheduler 保留消息所有权，quit 不清除存活线程的 native Looper。
  myQueue 只发布稳定队列身份，范围外失败。
- runOnUiThread 仅 root context 同步虚派，worker 投递唯一主 Looper；worker join 后、root JNI
  detach 前释放 native token。AsyncTask 的 DexVmError 保留线程故障并终止，不转 null 或继续
  onPostExecute。Thread/JNI 复用 core runtime/catalog/context；Runtime.nativeExit 发布退出标记
  并调用不可返回 VM Exit，System.exit 不得设标记后返回。
- ResultReceiver 本地协议归 BootDex：有 Handler 排队、无 Handler 同步；Parcel 保留本地 Binder
  identity，不建远程 scheduler。Dialog 展示明确失败。
- WebView.destroy 可覆盖；按构造 Looper 检查线程，targetSdk≥18 跨线程抛 RuntimeException。
  清理 WebSettings/client/interface 且不重建，重复销毁防御幂等；不宣称完整 AOSP 等价。
  配置/RenderPriority 只存状态/提示，不影响 host 调度；引用由 guest 字段/HashMap 保活。
- 默认无浏览器后端：HTTP/数据加载异步 onReceivedError(ERROR_UNSUPPORTED_SCHEME)，javascript
  只记日志，stop/destroy 取消回调，历史为空；严格诊断抛 UOE。外部 HTTP/HTTPS ACTION_VIEW
  同样只记录返回，同包显式 Activity 行为不变。

## 媒体、网络、设备与 JNI

- MediaPlayer/VideoView 只消费受检资源/路径/逻辑 FD 区间，交唯一 decoder/mixer，不建 host fd
  或第二播放器。EncodedMusicMixer 每实例、JavaSoundPoolMixer 每池隔离；音乐增量读取资源/
  APK/VFS 窗口或 lease，仅短音效全量读取。VideoView 原子捕获 lease，FD 关闭仍保留窗口，
  同路径替换不得复用旧缓存；不查询 host 路径。openRawResourceFd 仅 stored 文件型资源。
- MediaPlayer 原事件经 postEventFromNative/Handler，mNativeContext 为非零 32 位 token。
  显式维护 Idle/Initialized/Preparing/Prepared/Started/Paused/Stopped/PlaybackCompleted/Error
  阶段；stop 幂等且需重新 prepare，非法 transport 停音源并发 error，非法 prepare 抛 ISE；
  回调后重验可能 release 的表项。AudioManager volume/mute 下推对应 stream，视频属 MUSIC；
  网络 URI、subtitle、DRM、effects 失败。
- AudioTrack 普通协议归 BootDex，仅 overlay native；rate 来自 mixer，stream/static、通知、
  listener、pause/flush/release 共用状态，marker/period 默认及释放后查询按 AOSP 为 0。
  回压等待释放全部 VM 锁、恢复复验 owner；host 音频线程不得入 VM。
- WifiLock/MulticastLock 客户端算法归 BootDex Java；native 租约校验 WAKE_LOCK、mode 1/2/3
  或 CHANGE_WIFI_MULTICAST_STATE，后者获取/释放均受检。两类锁合计每 manager 最多 50 个，
  逐 owner 释放，GC/退出沿统一清理；不改变连接、网卡或组播传输。WorkSource/无线服务
  不支持，边界见 ADR-0092/0093。
- 网络使用 core policy/transport，默认离线；Connectivity/Wifi 只发配置事实，不探测 host
  网络/DNS/代理/证书。传感器/电话只返回 API 允许缺席；location 仅值类型/listener/稳定 facade，
  无 provider/历史，更新注册/移除失败。KeyguardManager 不缓存对象，三项查询读进程 provider
  快照，未注入表示桌面无锁屏，后续只替换 provider，不散入 host/Binder/WMS 查询。
- load/loadLibrary 经 process loader 携 application ClassLoader，失败映射 Java 异常；soundpool/
  media_jni 仅为平台库身份、不加载 ELF。固定 Conscrypt javacrypto 映射 ogplay_jni，其余名称
  不改写。JNI 对象按真实 runtime class 原子幂等注册，数组元素不猜声明类型。
- Mac/MacSpi/HmacSHA1 SPI 来自 BootDex/Conscrypt，仅注册已接通算法，经 AOSP NativeCrypto ABI
  调 guest ARM libcrypto，token 共用 GC/teardown；本模块不承诺 BKS/KeyStore、BouncyCastle 或 TLS。

## 诊断与验证边界

Dashboard AudioTrack/UiTree 仅 TryAcquire VM 锁，PCM 用 mixer try-lock，忙即 nullopt；UI 计数
含根节点，不执行 layout/draw。VideoView try-lock 最多复制 128 项，不调用 decoder，base_position
不冒充实时位置。Animation listener 只提供类型，不提供执行器。

定向验证入口为 tests/dexvm 的 android/widget/layout/scheduler/egl、runtime JNI/native loader 和
frontend lifecycle；相关行为覆盖 switch/threaded 及架构约束。title 探索不等于 Scenario 验收。
外部 package/service resolver、ContentProvider、完整 UI/framework/传感器、现代支付/社交/反作弊
仍不在范围；能力状态与运行证据见 capabilities、CURRENT 和对应任务单。
