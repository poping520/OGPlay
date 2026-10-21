# 子模块：runtime/integration/dexvm_android

## 职责与装配

为 dex_activity 提供有界 android/javax.microedition intrinsic，将 session 的 VFS、UiTree、
ANGLE、媒体、线程及平台事实接入 DexVM。禁止 title/厂商分支；不实现完整 Android、
Binder/system_server、外部包数据库、Play 或手机系统服务。移动宿主经 HAL 接入（ADR-0086）。

`catalog.cpp` 唯一注册；每类一个 `Declare_<类名>(context)`，shape/handler 在所属
content/os/view/graphics/gl/media/database/device family TU 同址。`shared.*` 仅跨类 helper；
禁止静态自注册、转发命名空间、字符串 handler id、单类 TU/misc 聚合。非 Android family
归 core，平台事实由 `DexVmAndroidContext` / `AndroidCoreIntrinsicServices` 显式注入。

## 共用不变量

- 普通 Java 字段/数组/算法归 BootDex，catalog 仅 native/平台边界，不重复类或普通方法。
  工厂先初始化类再执行原构造器；host state 必须 owner-attached，具备 trace/sweep/clone policy。
  session root、object owner、UiNodeId、native token 不混用，浅 clone 不提前释放共享 token。
- guest 引用强类型，字段用 bound token，flags 来自 `access_flags.h`；override 显式声明、不复制
  继承成员。token 不保存 host 指针，保存在 Java long；MediaPlayer.mNativeContext 沿 API19
  为非零 32 位 token；资源均登记 GC/teardown。原版 Java 的参数异常、状态及监听协议不另建副本。
- 资源/设备/路径/身份仅取显式 context，不探测 host。classpath 封存 BootDex/APK：bootstrap
  不见 APK、application parent-first，返回归档 guest 路径/entry，校验来源及 ZIP/CRC。
  BootDex 身份 `/system/framework/bootdex.jar`，APK 用 package_resource_path；无宿主文件映射/跨 VM 缓存。
- 未实现记账并抛可捕获 Java 异常，禁止伪成功；结构化日志保留异常，回调保持身份/代际。
  Clock 唯一时间源，一个 guest 对应一个 host 线程，共用 VM 锁；图形 ANGLE，窗口/输入 SDL3。

## Context、组件与进程

Context→ContextWrapper→Application/Service/ContextThemeWrapper→Activity 层级固定。
Application/base Context/ClassLoader/descriptor 身份稳定，wrapper 虚派委托 base；getClassLoader
返回关联应用 facade，不取 receiver defining loader/Thread context loader。Activity 仅顶层，isChild=false。
Window/attributes/decor 按实例持有，decor 焦点由 owner 判定；Intent/结果字段及默认回调归 Java。

启动/结束经 private native 提交 FIFO 命令，Intent 用原复制构造器。同包显式启动受检；隐式仅
匹配 sealed APK 中 action/category、无 data/type、含 DEFAULT 的唯一 enabled Activity。
零匹配 ActivityNotFoundException，多匹配/潜在 data 匹配失败。重复过滤器独立匹配、组件名去重；
同名查询/启动取最后配置，alias 保留身份、实例化 target，launchMode/taskAffinity 用解析时继承值。
无跨包 resolver/chooser/Instrumentation/完整 ActivityManager。非空 options、for-result 非零 flags、
非前台调用、跨包/跨 task 结果、向复用实例请求结果、非默认 affinity 匹配、后台改前台内容记账失败。
普通同 task 启动不匹配 affinity，不能因该元数据拒绝普通启动。
默认 affinity 下按 standard/singleTop/singleTask/singleInstance 提交 mode 快照；task ID/root 查询
来自 live task 分组。复用交付 onNewIntent，原 Intent 仅由应用 setIntent 改写；不实现完整 AMS。

返回栈/命令/待结果为 GC roots，退出线程仅读 atomic 深度；挂起实例查自身内容，UiTree reset
只清前台、后台保持附着及独立可见性。关闭拒绝新 launch/finish；自进程 Process.killProcess/
Runtime.nativeExit 共用不可返回 VM 退出协议，展开 Java 并终止 execution contexts，System.exit
不得返回，外部进程拒绝。无 Binder/AMS/LMK、任务管理或宿主内存采样。

| 内存查询 | 平台边界 |
| --- | --- |
| ActivityManager.MemoryInfo | Java 字段/算法，nativeRead 写注入快照；availMem=(free+cached)*1024，64 位字段/阈值完整提交，lowMemory 比较归 Java；缺事实/进程枚举 UOE，wrapper 共用 base |
| Debug.MemoryInfo | 原版字段/构造/汇总/Parcel；仅当前进程，注入值优先 provider，全部校验后提交；缺事实/忙 UOE，非法数据 IAE，null NPE；无 PID 查询/trace/DDMS |

## PackageManager、权限与路径

仅发布 session 封存的当前 APK Manifest/path/label/permission/feature，不从默认字段猜安装状态。
值对象来自 BootDex，Context 保持稳定 ApplicationInfo，查询返回独立值；FLAG_INSTALLED 是已知事实。
meta-data 的 value 解码 ARSC 类型、resource 保留 ID，application/组件分别物化；selected-ABI 库
在 `/data/app-lib` 只读挂 VFS。

| 查询 | 支持与失败边界 |
| --- | --- |
| getInstalledApplications | 封存单包清单，0/GET_META_DATA；原 ArrayList/独立 ApplicationInfo，含禁用应用；缺清单/身份/guest 路径或未知 flags UOE |
| getInstallerPackageName(String) | 当前直接加载 APK 无安装器记录则 null；未知包（含空串/system/null）IAE，不推测商店 |
| getResourcesForApplication(String) | 当前包共用应用 Resources/AssetManager；未知包（含空串/system）NameNotFoundException，null NPE；无跨包/其他重载 |
| getPackageArchiveInfo | 当前封存只读 APK、VFS 规范路径，0/GET_ACTIVITIES/GET_DISABLED_COMPONENTS；UID=-1、安装目录=null/时间=0；缺路径/非普通文件 null，未登记归档/缺事实/未知 flags 失败 |
| getPackageInfo | GET_ACTIVITIES/META_DATA/PERMISSIONS 组合；activities 仅请求时按声明顺序发布、保留 alias，默认排除禁用项，否则 null |
| getActivityInfo/getReceiverInfo/getServiceInfo | 当前包完整组件名，0/GET_META_DATA/GET_DISABLED_COMPONENTS；按应用/组件启用状态过滤，缺失 NameNotFoundException，同名 Activity 取最后声明 |
| getPermissionInfo | 仅 Manifest `<permission>` 定义，0/GET_META_DATA；仅请求/授予但未定义仍 NameNotFoundException |
| queryBroadcastReceivers | flags=0、非空 action、同一 filter category 子集，无需 DEFAULT；排除禁用项，同组件首匹配、API19 稳定排序 |
| resolveService | 非空 action，无 component/data/type/categories，0/GET_RESOLVED_FILTER；仅确定无候选时 null，投影不改匹配；潜在匹配/未知条件失败 |
| bindService/unbindService | 缺席查询沿原规则；主线程 BIND_AUTO_CREATE、component-only、当前 APK 启用的同进程服务（ADR-0084） |

广播查询保留真实 ArrayList/ResolveInfo/ActivityInfo 及 priority/match/isDefault/filter label/icon，
permission/exported 不限制声明查询。外部包、selector/component/data/type、未解析潜在 data、
未知 flags/缺 inventory 不伪装零匹配。服务查询不启动进程；正匹配 resolver/started service/跨进程
服务未实现。显式绑定在主 Looper 异步 construct/attach/onCreate/onBind 后交付原本地 Binder；
同组件共享，末次解绑 onUnbind/onDestroy，排队后解绑取消回调，null onBind 无连接回调。
连接早于 bind 结果登记、失败可解绑一次；Context 强边/session roots 保活连接/Intent/服务，退出清理；
定向反射不解析无关签名。

权限定义/requestedPermissions/granted 独立；checkPermission 仅 self PID/UID，外部/未授予 denied，
null permission IAE。checkCallingPermission 无 Binder 时总 denied，CallingOrSelf 查 self，enforce
拒绝抛 SecurityException；无 UID 数据库/Binder caller/运行时授权系统，wrapper 委托 base。
code/resource 共用 `/data/app/<package>-1.apk`，files/cache 经 VFS；getFileStreamPath/openFileInput/
openFileOutput 共用单文件名校验，前者不创建，后者 PRIVATE 覆盖/APPEND 追加。getDir(name,PRIVATE)
返回 `app_<name>`，mkdir 失败仍返回 File、实际 IO 由 VFS 判定；名称含 `/` IAE，null/空串沿 API19
拼接，缺 VFS/其他模式失败。getObbDir(s) 经 overlay 建 primary external 的 `Android/obb/<package>`；
无跨应用权限模式，wrapper 委托 base。

## 服务、设备与网络边界

| 能力 | 支持范围与所有权 |
| --- | --- |
| Settings/SystemProperties | 原 Java 协议/转换/moved-key，仅 overlay NameValueCache：Secure 读沙盒身份、System 进程隔离表、Secure/Global 特权写记账 false；SystemProperties 仅受审 native；无 Provider/跨用户/观察者/host 设置 |
| Notification | base Context 缓存 manager（独立无 Application 时用当前 Context）；字段/构造/from/重载归 Java。仅当前包取消已知空集合，不建表/保活通知/改 PendingIntent；未知不视为空，新增发布须更新 backing。发布及跨包 factory/取消/发布拒绝记账；值构造/extras 支持，模板/Parcel 分别 UOE、不改对象/contentView，null Context NPE；Parcelable 标记无传输，无系统展示/声振/点击/外部生产者/持久库存 |
| NFC | 正常构造/缓存原 NfcManager，构造器捕获 native 工厂 UOE 后保存 null，查询/参数错误沿 API19、无 NFC feature；越界记账失败，无监听/通信 |
| PendingIntent/AlarmManager | 当前 APK service/broadcast 令牌，创建不解析/启动目标；原 Intent 复制/filterEquals/replaceExtras 快照，extras 不参与身份，种类/包/requestCode/ONE_SHOT/fill-in flags 参与，NO_CREATE/CANCEL_CURRENT/UPDATE_CURRENT 控制查找，CANCEL_CURRENT\|NO_CREATE 返回旧已取消令牌。仅普通 Intent/BootDex Uri；selector/content MIME provider/未知 flags/Parcel/克隆失败。VM 锁串行 registry、非永久根，live wrapper trace Intent、GC 删除记录、包装同对象；闹钟条目为 session roots。alarm 单例，cancel(null/无匹配) 正常，移除闹钟不取消令牌；令牌 cancel 后不匹配。退出停止创建/清理；注册/调度/send/started service UOE |
| Connectivity/Wifi | core policy/transport 默认离线，仅注入事实，无 host 网络/DNS/代理/证书探测。默认断开的逻辑 WiFi，可声明类型缺席；独立原版 NetworkInfo=DISCONNECTED/不可用、活动网络 null/Wifi 禁用。查询需 ACCESS_NETWORK_STATE，缺事实/online policy UOE，不支持类型按 API19 null；值/枚举/Parcel/范围算法归 Java，native 校验并调用原构造/setter；无在线 provider/事件/无线控制 |
| WifiLock/MulticastLock | Java 客户端/native 租约；校验 WAKE_LOCK、mode 1/2/3 或 CHANGE_WIFI_MULTICAST_STATE（获取/释放均受检），每 manager 合计≤50，owner/GC/退出释放；不改变连接/网卡/组播，无 WorkSource/无线服务（ADR-0092/0093） |
| Sensor/Telephony/Location/Keyguard | Sensor 算法/缓存/Legacy 位掩码/监听映射归 Java，Context 执行原 LocalSensorManager 构造；native 读取与 NDK 共用的注入设备目录（当前仅空目录）、注册 false、无登记注销，Legacy 旋转为 managed viewport ROTATION_0，无 WMS/C++ 副本/伪回调。电话按 API 允许缺席，getSubscriberId 公开可覆盖、无订阅 null，Java/JNI 同源；listen(null) 沿 API19 捕获 NPE 返回。location 仅值/listener/facade，无 provider/历史，更新注册/移除失败。Keyguard 不缓存对象，三查询读进程 provider，未注入为桌面无锁屏；无 Sensor/SystemUI/WMS 事件源不伪造回调 |

## 资源、图像与存储

- Resources/AssetManager 为稳定应用对象对，mAssets 为 GC 强边；getSystem 独立且不回退 APK。
  只读 APK/ARSC/AXML，open 返回 ByteArrayInputStream；openFd/raw-resource FD 仅 STORED 文件，
  与 NDK 共用校验/逻辑 FD/区间，错误 IOException/NotFoundException。AssetFileDescriptor.close
  关 IO 并设 Java descriptor=-1。getText/getString 可覆盖，getString 虚派 getText→CharSequence.toString，
  Context 经 getResources 委托；配置/引用选择后字符串读原值，其他 simple value 用
  TypedValue.coerceToString，复杂/null/未知类型/无效引用 NotFoundException；无 spans/完整主题/locale qualifier。
- 系统 getString/getInteger 每 context 从封存 BootDex 的 META-INF/ogplay/system-resources.json
  加载一次（schema 1/API19、STORED、≤64 KiB/256 项），配方拥有名称/类型/消费者/配置，ID 取最终
  DEX、build/check 校验；无 host/APK 回退/旧 ID 别名。缺失/重复/畸形映射 ISE，未知 ID/非整数
  getInteger NotFoundException，映射整数可转文本。Configuration 稳定对象用 VM Locale.getDefault/
  原 setLayoutDirection，TextUtils 仅 ROOT/en/zh 的 LTR，其他在 likely-subtags 接通前失败。
- getDrawable 返回真实 BitmapDrawable/ColorDrawable；缺/无效资源 NotFoundException，XML Drawable/
  资源 NinePatch 对象 UOE、自定义 Drawable 绘制拒绝。背景及 ImageView 保持 Drawable 身份与
  View→Drawable→Bitmap 强边，Button 默认非空背景、普通 View 可 null；setImageBitmap 每次新建
  BitmapDrawable（含 null），经 Context 虚派 getResources/setImageDrawable，临时保活、异常保持身份。
  alpha 按实例，空对象不绘制，替换/清空解绑 callback、仅有效 node 重绘。
- openRawResource 两重载共享配置/引用选择，TypedValue 发布文件/ID/cookie/density，缺/非文件
  NotFoundException。BitmapFactory Java 管资源流关闭/default density，native 同源解码 stream/array
  为独立 BitmapPixels；支持 ARGB8888/bounds/inMutable/density scaling，其他选项/资源 NinePatch UOE。
  预算先验、超限 OOME、普通失败 null，默认不可变、density/mutability 属于 owner。getXml 的 AXML
  仅 getEventType/next/getName/getText/close，其他完整 XmlPullParser 方法 UnsatisfiedLinkError；文本 XML 用 KXml。
- Bitmap/getPixels/setPixels/Canvas.drawBitmap/GLUtils/JNI/UI 共用 BitmapPixels；首个 NDK lock
  转移权威至 guest 格式，末次 unlock 回写/退役地址，API19 四格式/stride/预乘/低位量化同源。
  recycle 禁止新访问、旧锁存活至 unlock/退出；布局/呈现前投影不可变 UiBitmap，native 锁期间
  读当前像素，不建第二份可变权威；无完整 Skia/颜色管理/未同步并发裸写。
- StatFs 字段/构造/restat/getter 归 Java，Posix.statvfs 只查 VFS 沙盒配额卷，其他卷失败、无 host
  空间采样，见 [ADR-0122](../../../../docs/adr/runtime.md#adr-0122)。SQLite 固定真实引擎，只经 VFS
  访问主库/journal/临时文件；Java 拥有事务/连接池/Cursor，native 仅 ABI/token/错误码/受审配置。
  损坏由引擎/原处理器判定，teardown 取消/关闭 host 资源；CursorWindow 有界且遵循
  requiredPos/countAllRows，时间经 Clock，journal=DELETE、无 WAL。SharedPreferences 按 context/package
  持久化 XML，editor/listener 遵循 owner/state-table 契约。
- Parcel 协议归 Java，平台提供 VM 隔离 backing/游标/本地 Binder identity，记录为 GC 强边；长度
  先验、含对象拒绝 marshall、无 FD/远程 Binder。Binder 线程策略保存 mask/GATHER 但仅记录；
  StrictMode 只查询/清理无 violation 状态，无检测/violation 编解码，异常编码仍归 Java。
- Log Throwable/getStackTraceString 共用 guest printStackTrace(PrintWriter)，flush 后取字符串、保留
  虚派/异常；null 或 cause 链含 UnknownHostException 子类返回空串，不按消息判断。EventLog 保留
  API19 类型/截断/返回值，无服务时读取失败；均用结构化日志。

## UI、Surface 与输入

live View 一一绑定 UiNode，hierarchy/id/visibility/layout/text/style/查找/attach/detach 共用 UiTree；
Java 字段修改不自动 traversal，getContext 是构造/inflation 的 mContext。addView(width,height)
虚派默认 LayoutParams、写原 Java 字段再统一 attach，不复制参数。

Window 在每实例 synthetic root 下持有稳定真实 FrameLayout，android.R.id.content/parent 保持身份且
不跨窗口；Activity 委托 Window，setContentView 替换、addContentView 追加，null 参数用默认值。
content_view 仅首个 View 投影、window root 是遍历边界；移除保留 View/参数/几何，实例退役才清理。
onContentChanged 虚派、异常保持身份，重入换代中止旧操作，不回滚已执行回调；无主题 Decor/ActionBar。
每 Window 独立 LayoutParams；API19 公开实例字段走 intrinsic 真实字段，setAttributes 按
copyFrom 复制，已赋 token/packageName 保留；亮度等只保存，不作用于宿主屏幕。
Decor.dispatchTouchEvent 虚派 owner Activity；DOWN onUserInteraction，随后 Window 内容树
分发，未消费回退 Activity.onTouchEvent。View enabled listener 优先再 onTouchEvent；
ViewGroup 先 intercept 再 reverse-Z 子树，接管以 CANCEL 取消子目标。宿主与 guest 共用
捕获 owner，DOWN 捕获目标接收 MOVE/UP/POINTER_DOWN/UP，不 split，其他 action 记账失败；
detach/隐藏或 Activity 切换清捕获（ADR-0149）。

LayoutInflater Context/from/clone/重载归 Java，基础 Context 用 guest 字段缓存服务，wrapper 缓存克隆，
Window/Activity 查同一 owner 服务。registry 独立构建并生成父类型 Java LayoutParams，再 addView：
root=null 不附着、attach=false 只生成参数、attach=true 返回 root；merge 要求 root 非空且 attach，
可空 merge。onFinishInflate 按文档顺序子到父、加入父容器前执行，GC/节点/总深度受检；失败仅清
本次节点/绑定、保留容器/旧窗口/Dialog。仅固定控件及登记属性/样式，无 parser 重载、完整
AttributeSet/theme、Factory/反射。属性按默认/textAppearance/ProgressBar→style→显式 XML 覆盖，
未知 style 失败；LinearLayout(Context,null) 默认水平、null Context NPE、非空 AttributeSet 构造失败。

removeViewInLayout 为 public virtual，复用普通移除的退役/层级/焦点；只失效绘制、不调
requestLayout/invalidate，普通移除仍虚派 requestLayout。ViewGroup 子节点 API 按 API19 public virtual，以 UiTree children
为 mChildren 唯一事实：getChildCount/getChildAt（越界 null）/indexOfChild（缺席 -1）；removeView/
removeViewAt（越界 NPE）/removeViews 走普通移除并虚派 requestLayout/invalidate，*InLayout 变体保留
layout，removeAllViews 虚派 removeAllViewsInLayout；区间越界 IndexOutOfBounds。内部查找不经可覆盖
getChildAt/indexOfChild；无 onViewAdded/onViewRemoved、addViewInLayout/detach/attach 系列。callback 后复核节点/parent，快照保活、
不销毁可复挂 View；无 disappearing animation/accessibility/hierarchy listener。尺寸在 root guest
线程按已解析 frame 交付 onSizeChanged，带上次尺寸、未变不重发，detached/GONE 不派发，destroy/reset
清缓存。RootScope 保活并逐项复核 binding/attachment，换内容停止旧遍历，重排≤8 轮、不收敛失败；
动态 attach/Surface creation 先尺寸再 Surface 回调，重入延后新 Surface，缓存只记已交付尺寸。
受管生命周期内 setContentView/addContentView/addView 新挂载子树登记 pending，于下一 owner traversal
统一 created/changed（API19 updateWindow）；首次启动使用相同 holder 激活/尺寸发布规则，
只消费已遍历子树的 pending，回调中新挂载子树等待后续遍历。

SurfaceView 构造时登记稳定 holder，getHolder 只返回对象、不激活 Surface。
NDK fromSurface 在 VM 锁内从 JNI Surface 映射有效 holder（含 surfaceDestroyed 回调期间），交同一
boundary window registry acquire；changed/销毁/GC 经显式通知同步几何/退役，销毁通知在
surfaceDestroyed 回调之后，销毁后不重建旧身份。
SurfaceHolder 按 attach/host generation 执行 created→changed→destroyed，detached 不提前收事件/
关闭 host surface。getSurfaceFrame 为稳定 Java Rect：初始零、回调前/late holder 写真实 managed
像素尺寸、destroyed 保留末值，旧 holder 不随新代更新；不从 View bounds 推导独立 buffer。
setFixedSize 仅物理托管尺寸、保存请求，setSizeFromLayout 解除固定，不匹配独立几何失败。
SurfaceView(Context,null) 复用 View 初始化，null Context NPE、非空 AttributeSet/三参数 defStyle
不支持；注册 XML 沿 Context 构造及既有属性投影。层序唯一归 UiTree，setZOrderMediaOverlay/
setZOrderOnTop 后调用覆盖前调用；compositor 即时消费，不承诺 WMS 重建/任意 GLES 多层。
View.bringToFront 虚派 UiTree 父 ViewParent.bringChildToFront，合成 root 按 ViewRootImpl 空操作；
ViewGroup.bringChildToFront 忽略非子节点/null，移至末尾后虚派 requestLayout/invalidate。
setKeepScreenOn(false) 清空请求、true 记账拒绝；Canvas/Bitmap 为 ARGB，post 发软件帧，Surface 返回限制见 session。

NativeActivity/NativeContentView 执行原 Java，private native 经 NativeActivityRuntime 接进程；
takeSurface/takeInputQueue 支持 null/切换，先销毁旧 owner，首次遍历前解除不收旧事件。
Callback2 changed 后 redraw，首次/dirty traversal 向 attached observer 发 global layout，
getLocationInWindow 用 UiTree screen frame。

焦点唯一归 lifecycle/UiTree，requestFocus 共用可聚焦/触摸模式/祖先状态/转移/清除/listener。
clickable 方法可覆盖；setOnClickListener 先虚派 isClickable/setClickable(true)，null 仍设 clickable
但解绑，setClickable(false) 不删 listener；触摸点击需 clickable、InvokeViewOnClick 不需，基础
onTouchEvent 只消费、不伪造点击/禁用子类。setText/Editable/host EditText 共用过滤/区间变更/
watcher 快照/同步回调/失效事务，BufferType.NORMAL 复用普通编辑，span/editable 模式受限。
clipChildren/clipToPadding 默认 true，改变只 draw dirty、不改尺寸/命中；绘制遵循祖先/输出/自身
clip，输入先 layout 再 clipped reverse-Z/deepest-first 虚派。滚动条样式仅 mViewFlags 的 0x03000000，
默认 INSIDE_OVERLAY；scroll-container/开关只存状态，无绘制/inset 重算。

InputMethodManager 无 InputConnection 会话；restart/show/hide 从当前窗口 owner/attached UiTree 焦点
投影 checkFocus，无候选时 restart 返回、show/hide false，有候选则连接创建记账失败（含无关
View/token），不按非编辑器跳过 dummy connection；无 served View/连接/软键盘状态/proxy/ResultReceiver/桌面 IME。
InputDevice/MotionRange 算法归 Java、目录启动前注入；未知 id/轴 null、空目录新数组，无 host/
InputManager/Binder 探测，hasKeys/震动/Parcel 失败，无完整反射/摇杆/触摸板/热插拔承诺。
KeyCharacterMap/KeyEvent 修饰算法归 API19，未知 id 回退虚拟键盘、无键盘失败；FULL 用 AOSP
Virtual.kcm，非键盘为空 SPECIAL_FUNCTION。native 字符/标签/数字/match/fallback 共用不可变 selector，
Unicode 同源且保留显式宿主 Unicode；动态布局/分类/重音/事件合成/物理键能力/system-key policy 失败。
SDL scancode 映射 keyCode/Unicode/meta/repeat；AndroidBoundaryInput 快照物化事件，MotionEvent 数组
归 VM/GC、保存触点/轴/offset/精度/纳秒时间/历史，getter 不读 host、API19 毫秒/纳秒转换、越界 IAE，
recycle 清引用后读取失败；KeyEvent 保留 down/event time、flags/source。

## GLES/EGL 与消息调度

Java GLES/EGL 经 session managed 冷入口共享 [native boundary](../../boundary/MODULE.md) 的 binding/
registry/current/error；参数错误 guest 锁存、host 契约故障硬失败。javax GL 接口归 Java，GLImpl
native 用生成 ABI/既有 GLES 编组，每 EGLContext 字段持独立 GLImpl/Buffer、正常初始化/构造；
目标以线程 current 为准，direct Buffer 用稳定 NIO 地址、heap 不存临时地址，按 targetSdk 报错/拒绝。
EGL10/EGL14 wrapper 对应真实 display/config/context/surface；config 可投影请求颜色位数但不换
native handle，current/延迟销毁/pbuffer/share/扩展/error 归 registry，offset 受检、只写指定切片，
未支持入口保留精确 EGL error。GLES30 类/常量/重载按 delta catalog，缺 handler 记账；String/sync long/
mapped Buffer/active/uniform-block/transform-feedback/String[] 专用编组。GLES20 glGetString 支持
SHADING_LANGUAGE_VERSION，GLUtils 按四种 Bitmap.Config 编码；GLU 用 API19 数学/GL10 虚派，不入 native 目录、失败不回写。

GLSurfaceView 版本/config/driver/事件/EGL wrapper 归稳定 per-View runtime，foreground 仅投影。
EGL/renderer 初始化等待所属 holder 已激活并发布正尺寸，暂停与可见性仍独立限制绘制。
GLThread 为真实 Java Thread，经显式 hook 接 session，不自动 prepare Looper；queueEvent 唤醒并
保活 Runnable，在 current GL 线程、renderer 前 FIFO 执行、不依赖绘帧。requestRender/WHEN_DIRTY
单次消费；PreserveEGLContextOnPause 默认 false、Java 字段/可覆盖方法、可先于 renderer 设置。
onPause/onResume 在所属线程确认、释放/恢复 EGLSurface，Context 按 preserve 保留/重建，
backend context-loss 按验证范围记账。

Handler/Looper/HandlerThread/Timer/AsyncTask 共用 scheduler，同 uptime deadline 按 sequence FIFO；
主 Looper 在 lifecycle safe point 泵送、子 Looper 在对应 host 线程执行，不同步伪装 post。
Clock 默认确定性 60 秒 boot-age，无 suspend 时 uptime/elapsedRealtime 同源。
prepare_native_looper 显式关联 Java/native 线程、主 Looper 仅首次关联，查询/消息泵不重 prepare；
Java scheduler 持消息，quit 不清存活线程 native Looper。root Thread 稳定、空泵不重入 Java/VM 锁，
myQueue 仅稳定队列身份；runOnUiThread 仅 root 同步虚派、worker 投主 Looper。worker join 后/root
JNI detach 前释放 token；AsyncTask DexVmError 保留故障并终止，不转 null/继续 onPostExecute，Thread/JNI
复用 core。ResultReceiver 本地协议归 Java，有 Handler 排队/无 Handler 同步，Parcel 保留本地 Binder identity。

## 媒体、Dialog 与 WebView

MediaPlayer/VideoView 仅受检资源/路径/逻辑 FD 区间，复用 decoder/mixer，无 host fd/路径反查/第二
播放器；EncodedMusicMixer 按实例、JavaSoundPoolMixer 按池隔离。音乐增量读 window/lease，短音效
才全量读取；来源原子捕获、关闭 FD 不影响区间、同路径替换不复用旧缓存。STORED 媒体完整 CRC
校验一次后借 payload，APK 随 context 保活且不变；Deflate 沿逐窗口校验、损坏失败。
MediaPlayer/SoundPool 的 FileDescriptor：平台登记者沿原来源；其余按 API19 jniGetFDFromFileDescriptor
读 int descriptor 为 guest VFS fd（含 NDK AAsset_openFileDescriptor），dup 式租约截取区间，offset≥size/
无效或非普通文件 IOException、length 截至文件尾。

普通 MediaPlayer prepare 优先纯音频 decoder，失败后交共享视频工厂，同一来源仅一个后端播放；
owner 为播放器，setDisplay/setSurface 仅绑定 active managed holder 的 View，未知/失效拒绝、双
producer 记账拒绝。Surface 销毁先撤销绑定/旧图像，音频/解码可继续，显式重绑恢复；reset/release/
GC/退出退役来源。事件经原 postEventFromNative/Handler，同步/异步 prepare 均发 prepared、视频给真实
size；loop/seek/pause/completion 共用 Clock/PCM。VideoView 主线程泵交付 prepared/error/completion，
guest 回调释放视频锁并重验 owner/代际，替换/释放取消旧事件；打开/解码失败不转 completion，
未处理错误对话框记账失败。MediaPlayer 阶段为 Idle/Initialized/Preparing/Prepared/Started/Paused/
Stopped/PlaybackCompleted/Error，stop 幂等且需重新 prepare，非法 transport 停音发 error、非法 prepare ISE。
volume/mute 按 stream，VideoView=MUSIC、普通播放器用已设 stream；网络 URI/subtitle/DRM/effects 失败。
AudioTrack Java 协议/native 边界，rate 来自 mixer、stream/static/通知/listener/transport 同源，
marker/period 默认及释放后为 0；回压释放全部 VM 锁、恢复复验 owner，host 音频线程不入 VM。
guest worker 静止后 ReleaseAndroidMediaResources 退役 decoder/状态/mixer player/pool/source lease，
不调 Java prepared/completion/destroy，decoder 退出等待在视频锁外。

Dialog/AlertDialog 协议归 Java，native 只投影 plain String 标题/消息/三按钮为 UiTree 临时模态
子树，active presentation 为 Dialog/Activity roots；按 geometry 捕获输入/取消旧捕获，listener 在
owner Looper 排队，owner 隐藏/恢复同步可见性，实例退役先 dismiss、进程停止仅退役投影。
不替换内容/退役 Surface/另建协议或层级权威；最多 8 窗口，title/message/button≤512/4096/128
UTF-16、density (0,8]、viewport≥48×48，须完整适配否则 UOE；theme/items/custom view/Window facade
拒绝。模态根为 direct content-root child，字体/布局/像素复用既有底座。

WebView.destroy 可覆盖、检查构造 Looper，targetSdk≥18 跨线程 RuntimeException；清理 settings/
client/interface、不重建、防御重复销毁，不承诺完整 AOSP 等价。配置/RenderPriority 仅状态/提示，
不改 host 调度，Java 字段/HashMap 保活引用。默认无浏览器：HTTP/数据加载异步
onReceivedError(ERROR_UNSUPPORTED_SCHEME)，javascript 仅日志，stop/destroy 取消回调、历史空；
严格诊断 UOE。外部 HTTP/HTTPS ACTION_VIEW 仅记录返回，同包显式 Activity 不变。

## JNI、诊断与验证边界

load/loadLibrary 经 process loader 携 application ClassLoader，失败为 Java 异常；soundpool/media_jni
仅平台库身份、不加载 ELF，固定 Conscrypt javacrypto 映射 ogplay_jni、其他名称不改写。JNI 对象按
真实 runtime class 原子幂等注册，数组元素不猜声明类型。Mac/MacSpi/HmacSHA1 SPI 归 BootDex/
Conscrypt，经 AOSP NativeCrypto ABI 调 guest ARM libcrypto、token 共用 GC/teardown；无 BKS/KeyStore/
BouncyCastle/TLS 承诺。

Dashboard AudioTrack/UiTree 仅 TryAcquire VM 锁、PCM 用 mixer try-lock，忙 nullopt；UI 计数含根、
不 layout/draw，视频快照 try-lock≤128 项、不调 decoder，base_position 不冒充实时位置。
Animation listener 仅类型、无执行器。定向入口为 tests/dexvm 的 android/widget/layout/scheduler/egl、
runtime JNI/native loader、frontend lifecycle，按实际范围覆盖 switch/threaded/架构约束；title 探索
不等于 Scenario 验收。外部 package/service resolver、ContentProvider、完整 UI/framework/传感器、
现代支付/社交/反作弊不在范围；能力与证据仅在 [capabilities](../../../../capabilities.toml)、
[CURRENT](../../../../docs/state/CURRENT.md) 及任务单维护。

## SurfaceTexture 与进程内生产端

SurfaceTexture 原版 Java 保留 Handler/WeakReference/监听与参数包装；native 字段仅是逻辑令牌。
构造可无 GL，允许完整 GLuint 位型；最多 64 consumer、256 Surface producer endpoint，
进程 CPU 图像 lease 共享 64 MiB 预算，RGBA 1..4096、紧密 bottom-origin 布局及显式 transform。
Surface Java 客户端普通锁/代际/释放归 guest，private native 携 owner；holder 创建时执行构造并
登记同源 token。Surface.release 不 abandon consumer，SurfaceTexture release/GC/进程退出使
队列永久失效。native 表只沿存活 owner trace weakSelf 包装，不成为 consumer 强根。

producer 不入 VM；主泵只把实际 enqueue 通知投给原版 postEventFromNative/Handler，异步 pending
替换不重发。update 在合法 current Context 上传/import/bind 后锁存图像/PTS/矩阵；空更新保留
图像且仍校验/bind。detach/attach 复用同一 ANGLE Context 身份，releaseTexImage 归还消费权并使用
固定 API19 debug bitmap；遗留 queued-count、媒体/NDK/EGL window producer、Camera 与 context-loss
恢复不在范围。默认 Metal 无 external，纹理更新记账失败；构造不触发后端切换。已排入 Java
Handler 的消息沿原版语义，不宣称 abandon 能撤销已交付消息。普通 MediaPlayer 仍只连接 holder。
