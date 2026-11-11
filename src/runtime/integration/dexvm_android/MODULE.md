# 子模块：runtime/integration/dexvm_android

## 职责与装配

为 dex_activity 提供有界 android/javax.microedition intrinsic，将 session 的 VFS、UiTree、
ANGLE、媒体、线程及平台事实接入 DexVM。禁止 title/厂商分支；不实现完整 Android、
跨进程 Binder/system_server、外部包数据库、Play 或手机系统服务。移动宿主经 HAL 接入（ADR-0086）。

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
- 资源/设备/路径/身份仅取显式 context，不探测 host。classpath 由启动类路径 jar 与 APK 组成：
  bootstrap 不见 APK、application parent-first，返回归档 guest 路径/entry，校验来源及 ZIP/CRC。
  启动类路径按 `kBootClasspathJars` 顺序（ogplay.jar 在前，其后为原样 ROM jar），各 jar 身份为
  `/system/framework/<名称>`，APK 用 package_resource_path；无宿主文件映射/跨 VM 缓存。
  ROM 类调用未绑定 native 时记账并抛 UnsatisfiedLinkError。已绑定的显示降级由具体入口记账。
- 未实现记账并抛可捕获 Java 异常，禁止伪成功；结构化日志保留异常，回调保持身份/代际。
  Clock 唯一时间源，一个 guest 对应一个 host 线程，共用 VM 锁；图形 ANGLE，窗口/输入 SDL3。

IntentFilter 的 actions、categories、MIME、scheme、authority 和 Parcelable 协议归原版 BootDex；
Context 不持过滤器内容副本。宿主仅登记 receiver 身份，方法/签名中的类型引用使用同一 Java 类。
CountDownTimer 的状态和 tick/finish 算法归 BootDex，通过既有 Handler/Looper 与统一 Clock 调度，无宿主倒计时侧表。
Time 的 native 入口仅转发到 guest Calendar/TimeZone 实现；format 转发原版 LocaleData 和
系统资源字段，当前时间取既有 System.currentTimeMillis。

## Context、组件与进程

设备语言地区仅存 locale_language/locale_region，默认 en/US，严格校验小写两字母语言与大写
两字母地区。System 初始化从该事实写 user.language/user.region；bridge 通过 boundary
provider 发布同源的 DeviceLocale 投影，不向 core 注入 ISO3。Resources.getConfiguration 的
locale 每次取当前 Locale.getDefault；setDefault 不修改设备事实。设置 UI、persist.sys.*、
Bionic 属性区、旧 headless HLE 与资源 locale qualifier 不属于此边界。


ROM ActivityManager 通过本地 IActivityManager 获取内存和当前进程快照。栈顶未 stopped
（含 onCreate/onStart）为前台，否则任一可见为 visible，其余为 cached 后台。内存档位由
SystemProperties 读取本 VM 的常规 growth limit 与 maximum，largeHeap 不改变常规档位。

Context→ContextWrapper→Application/Service/ContextThemeWrapper→Activity 层级固定。
Context、ContextWrapper、PackageManager 整类执行 ROM；ContextImpl 和 ApplicationPackageManager
分别持有应用环境与本地 IPackageManager，构造、attach 校验和虚派委托由原版 Java 管理。
封存 APK 经 ROM ActivityThread.H 处理 bindApplication，LoadedApk/Instrumentation 创建 Application
及 ContextImpl。Application 和各 Activity 使用独立的 ROM ContextImpl，应用 loader 由 ROM
ApplicationLoaders 缓存，与 VM 应用类的 loader 同一对象。DexFile 仅接入已注册的当前 APK DEX。
Activity 仍由 session 驱动，仅顶层，isChild=false。兼容层窗口和输入法入口由保留的 Activity 适配。
Window/attributes/decor 按实例持有，decor 焦点由 owner 判定；Intent/结果字段及默认回调归 Java。
Application 生命周期监听注册和快照派发归原版 BootDex。Activity 的 base 回调从
getApplication 取得本进程对象并派发，时机遵循 guest 的 super 调用。

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
不得返回，外部进程拒绝。本地 AMS 查询不执行 LMK、任务管理或宿主内存采样。

| 内存查询 | 平台边界 |
| --- | --- |
| ActivityManager.MemoryInfo | ROM 值类，本地 AMS 写入注入快照；availMem=(free+cached)*1024，服务按 API19 阈值计算 lowMemory；缺内存事实 UOE，wrapper 共用 base |
| Debug.MemoryInfo | Debug 与 MemoryInfo 整类执行 ROM；native 写入快照，原版字段/构造/汇总/Parcel；仅当前进程，注入值优先 provider，全部校验后提交；缺事实/忙 UOE，非法数据 IAE，null NPE；无 PID 查询/trace/DDMS |

SystemClock 只绑定 native 时间源，sleep 执行 ROM；Process 的 myPid/myUid/myTid 经
Libcore.os 的 Posix native 读取本进程事实，优先级不映射宿主调度。sendSignal 仅允许本 PID
的 SIGKILL，沿不可返回退出协议；其他信号/外部 PID 按方法记账拒绝。Trace 的 native
只启用 view 调试标签并写结构化日志，不探测宿主 tracing。Environment 整类执行 ROM，
路径从 guest environ 读取，卷查询经本地 IMountService 提供唯一外置 VFS 卷。

## PackageManager、权限与路径

仅发布 session 封存的当前 APK Manifest/path/label/permission/feature，不从默认字段猜安装状态。
值对象来自 BootDex，Context 保持稳定 ApplicationInfo，查询返回独立值；FLAG_INSTALLED 是已知事实。
ApplicationInfo.theme 和 ActivityInfo.theme 发布各自的 Manifest 值，未声明为 0。alias 沿用目标快照，ActivityInfo.getThemeResource() 由 ROM 回落到应用主题。
meta-data 的 value 解码 ARSC 类型（含 float→Bundle.putFloat），非 RIGID 不支持类型告警忽略，
TYPE_NULL 缺 value/resource 仍失败；Manifest TYPE_STRING 转换失败保留 Bundle null string，
key 仍必须存在，NUL/孤立 surrogate 保留 Java code units；resource 保留 ID，application/组件分别物化；selected-ABI 库
在 `/data/app-lib` 只读挂 VFS，`/data/data/<包名>/lib` 为其别名；nativeLibraryDir 仍为 `/data/app-lib`。

| 查询 | 支持与失败边界 |
| --- | --- |
| getInstalledApplications | 封存单包清单，0/GET_META_DATA/GET_UNINSTALLED_PACKAGES（单包清单下不改变结果）；原 ArrayList/独立 ApplicationInfo，含禁用应用；缺清单/身份/guest 路径或未知 flags UOE |
| getInstallerPackageName(String) | 当前直接加载 APK 无安装器记录则 null；未知包（含空串/system/null）IAE，不推测商店 |
| getResourcesForApplication(String) | 当前包共用应用 Resources/AssetManager；未知包（含空串/system）NameNotFoundException，null NPE；无跨包/其他重载 |
| getPackageArchiveInfo | ROM PackageManager/PackageParser 从 guest VFS 中的 APK 解析 Manifest，不复用安装清单；缺路径或损坏归档沿 ROM 返回 null。无包安装或外部包数据库 |
| getApplicationInfo | 当前包，0/GET_META_DATA/GET_UNINSTALLED_PACKAGES（单包清单下不改变结果）；外部包 NameNotFoundException |
| getPackageInfo | GET_ACTIVITIES/META_DATA/PERMISSIONS/UNINSTALLED_PACKAGES 组合（单包清单下 GET_UNINSTALLED_PACKAGES 不改变结果）；activities 仅请求时按声明顺序发布、保留 alias，默认排除禁用项，否则 null |
| getActivityInfo/getReceiverInfo/getServiceInfo | 当前包完整组件名，0/GET_META_DATA/GET_DISABLED_COMPONENTS/GET_UNINSTALLED_PACKAGES（单包清单下不改变结果）；按应用/组件启用状态过滤，缺失 NameNotFoundException，同名 Activity/receiver/service 取最后声明 |
| getPermissionInfo | 仅 Manifest 中 registered 的 `<permission>` 首个有效定义，0/GET_META_DATA；未知 group/重复后项不注册，仅请求/授予但未定义仍 NameNotFoundException |
| queryBroadcastReceivers | flags=0、非空 action、同一 filter category 子集，无需 DEFAULT；排除禁用项，同组件首匹配、API19 稳定排序 |
| resolveService | 非空 action，无 component/data/type/categories，0/GET_RESOLVED_FILTER；仅确定无候选时 null，投影不改匹配；潜在匹配/未知条件失败 |
| bindService/unbindService | 缺席查询沿原规则；主线程 BIND_AUTO_CREATE、component-only、当前 APK 启用的同进程服务；独立进程/isolated 返回 false，仍登记连接且可解绑，无回调（[ADR-0170](../../../../docs/adr/runtime.md#adr-0170)） |

广播查询保留真实 ArrayList/ResolveInfo/ActivityInfo 及 priority/match/isDefault/filter label/icon；
同名声明各自的过滤器与配置独立，跨声明按组件去重，首个匹配提供结果，不与按名查询的后项配置合并。
permission/exported 不限制声明查询。外部包、selector/component/data/type、未解析潜在 data、
未知 flags/缺 inventory 不伪装零匹配。服务查询不启动进程；正匹配 resolver/同进程 started service
未实现。独立进程/isolated 服务 start 返回 null、bind 返回 false，每次记账并限流 warn，
PackageManager 保留清单事实（[ADR-0170](../../../../docs/adr/runtime.md#adr-0170)）。显式绑定在主 Looper 异步 construct/attach/onCreate/onBind 后交付原本地 Binder；
同组件共享，创建、绑定、解绑和停止经 ROM ActivityThread.H 排队；回调及释放按 ROM 调度。
连接早于 bind 结果登记、失败可解绑一次；Context 强边/session roots 保活连接/Intent/服务，退出清理；
定向反射不解析无关签名。

权限定义/requestedPermissions/granted 独立；checkPermission 仅 self PID/UID，外部/未授予 denied，
null permission IAE。checkCallingPermission 无 Binder 时总 denied，CallingOrSelf 查 self，enforce
拒绝抛 SecurityException；无 UID 数据库/Binder caller/运行时授权系统，wrapper 委托 base。
code/resource 共用 `/data/app/<package>-1.apk`，files/cache 经 VFS；getFileStreamPath/openFileInput/
openFileOutput 共用单文件名校验，前者不创建，后者 PRIVATE 覆盖/APPEND 追加。getDir(name,PRIVATE)
返回 `app_<name>`，mkdir 失败仍返回 File、实际 IO 由 VFS 判定；名称含 `/` IAE，null/空串沿 API19
拼接，路径由 LoadedApk 的安装信息固定，不随宿主元数据改写。文件流和目录操作使用 VM 已绑定的 VFS；
getObbDir(s) 经 ROM Environment 与挂载服务取得 primary external 的 `Android/obb/<package>`。
权限设置保留现有 VFS 的未实现记账，返回值按 ROM 处理。

## 服务、设备与网络边界

ROM ServiceManager 经 BinderInternal 取得进程内服务目录。注册表和 Stub 归当前 VM，
Stub.asInterface 返回 queryLocalInterface 的本地对象，缺席按服务名记账并返回 null。
calling identity 固定为进程 UID/PID，StrictMode mask 按执行上下文隔离。无跨进程 Binder。

| 能力 | 支持范围与所有权 |
| --- | --- |
| Settings/SystemProperties | ROM NameValueCache 通过本地 Settings provider 读取沙盒 Secure 身份和进程内 System/Global 表。System 有效值变更递增版本并由 IContentService 通知观察者。Secure/Global 特权写记账 false。SystemProperties 从显示和 VM 配置读取密度、常规堆上限和最大堆；无跨用户、同步适配器或宿主设置。 |
| Notification | ROM Notification/NotificationManager 持有字段、构造、extras 与本地 Parcel 值；本地 INotificationManager 只取消当前包空库存，发布和跨包访问记账拒绝。无系统展示、声音、点击或持久库存 |
| SMS | SmsManager 和 SmsMessage 来自原样的 telephony-common.jar。PDU 由 ROM 解析。本地 ISms 的 14 个入口按签名记账拒绝，无 Phone/RIL、SIM 存储或短信传输 |
| NFC | ROM NfcAdapter 经 IPackageManager 查询 NFC feature，缺席时 NfcManager 捕获 UOE，默认适配器为 null；无监听或通信 |
| PendingIntent/AlarmManager | 当前 APK service/broadcast 令牌，创建不解析/启动目标；原 Intent 复制/filterEquals/replaceExtras 快照，extras 不参与身份，种类/包/requestCode/ONE_SHOT/fill-in flags 参与，NO_CREATE/CANCEL_CURRENT/UPDATE_CURRENT 控制查找，CANCEL_CURRENT\|NO_CREATE 返回旧已取消令牌。仅普通 Intent/BootDex Uri；selector/content MIME provider/未知 flags/Parcel/克隆失败。VM 锁串行 registry、非永久根，ROM 包装按本地 IIntentSender 的 Binder 身份比较，活令牌追踪 Intent，GC 删除记录；闹钟条目为 session roots。ROM AlarmManager 经本地 IAlarmManager，cancel(null/无匹配) 正常，移除闹钟不取消令牌；令牌 cancel 后不匹配。退出停止创建/清理；注册/调度/send/started service UOE |
| Connectivity/Wifi | core policy/transport 默认离线，仅注入事实，无 host 网络/DNS/代理/证书探测。默认断开的逻辑 WiFi，可声明类型缺席；独立原版 NetworkInfo=DISCONNECTED/不可用、活动网络 null/Wifi 禁用。NetworkInfo 查询需 ACCESS_NETWORK_STATE，缺事实/online policy UOE，不支持类型按 API19 null；ROM ConnectivityManager 通过本地 IConnectivityManager 查询，服务调用原 NetworkInfo 构造/setter。getConnectionInfo 需 ACCESS_WIFI_STATE，离线返回独立 WifiInfo（networkId=-1、UNINITIALIZED、MAC=null）；无在线 provider/事件/无线控制；同一离线拓扑派生只读 lo 内核视图与本地地址 ioctl，不受 transport IO 授权限制 |
| USB/备份 | ROM UsbManager 使用本地 IUsbManager，设备列表为空，配件缺席，打开设备返回 null，权限查询 false，其他操作按签名拒绝。ROM BackupManager 保留服务缺席语义，备份请求不排队，恢复请求返回 -1、无恢复会话。 |
| WifiLock/MulticastLock | ROM WifiManager 计数、held 和每 manager 50 锁上限；本地 IWifiManager 校验锁模式及 WAKE_LOCK/CHANGE_WIFI_MULTICAST_STATE 权限，不改变连接、网卡或组播。 |
| Sensor/Telephony/Location/Keyguard | ROM SystemSensorManager 从与 NDK 共用的空设备目录枚举。ROM LegacySensorManager 经 IWindowManager 查询固定 ROTATION_0。ROM KeyguardManager 的三项查询经本地 IWindowManager 读取进程 provider，缺席表示桌面无锁屏。ROM TelephonyManager 经本地 IPhoneSubInfo 读取注入的设备标识、软件版本和号码，无订阅时 subscriberId=null；ITelephony 报告 PHONE_TYPE_NONE。SIM/运营商查询由 ROM 读取进程 SystemProperties；ITelephonyRegistry 保存无事件的弱注册，PhoneStateListener 原版持有 callback，listen(null) 由 ROM 捕获 NPE。位置无 provider 或历史，更新注册/移除失败。 |
| Vibrator | ROM SystemVibrator 经本地 IVibratorService 查询硬件缺席；单次请求及取消更新进程事实，模式请求按签名失败，不驱动宿主硬件。 |

本包启用的同进程 provider 由 ROM ActivityThread 按 initOrder 安装，AMS 按 authority 登记。
本地 IContentService 按 authority 与路径匹配观察者，使用 ROM ContentObserver transport 分发。
同进程 Service 的创建、绑定、命令及销毁通过 IApplicationThread 投递；动态 Receiver 通过 ROM dispatcher
处理进程内无序广播。受权限保护的 sticky 保留未定向的 action 快照；电池快照声明无电池。

## 资源、图像与存储

- API19 ROM Java 管理 Resources、AssetManager、Theme、TypedArray、XML 与流的状态和调用协议。
  native 使用 [资源核心](../../resources/MODULE.md)，按类型分配非零 32 位令牌，销毁后拒绝旧令牌。
  每个 manager 的 Java/NDK 操作共用锁，XML parser 和独立 StringBlock 由 VM 串行化。
  表与 XML 的 StringBlock 借用池，独立 StringBlock 及 nativeCreate 的输入副本由对应句柄拥有。
- 默认 manager 映射只读 framework-res，应用 manager 通过 guest VFS 路径追加当前 APK。
  framework/app cookie 为 1/2，字符串表 block 从 0 开始，内联 XML 值 cookie 为 -1。
  资源选择使用 managed 显示尺寸、density、方向、区域和 API19 配置。
- AssetInputStream 直接读核心流。STORED FD 是完整 APK 的只读 guest 描述符，加受检的
  起点与长度，压缩条目抛 FileNotFoundException。ROM AssetFileDescriptor 持有 PFD。
  媒体在调用时捕获同一 VFS 区间，调用者随后可关闭描述符。
- 编译 XML 使用核心 parser，Java inflation 负责构造对象。软件位图经 ROM BitmapFactory
  选择源、密度和关闭协议，Skia 解码直接借用 STORED 字节并处理采样、缩放和 NinePatch。
  UI 投影读取 View 当前 Drawable 的 Java 状态及 P1 Bitmap/NinePatch/Paint backing。普通 BitmapDrawable（含 ROM 缓存的副本）
  从其 Java Bitmap 发布既有 UI 位图快照，投影键为内部身份，不对外充当资源 ID。布局和绘制支持仍由 UI 层决定。

- ROM Bitmap 的 mBuffer 是计入 Java 堆的 byte[]。Skia 直接在借用的宿主跨度读写像素，
  NDK lock 返回同一数组的 guest 地址。Java、Canvas 与 GLUtils 共用这一存储，UI 发布不可变快照。
  RGBA 默认预乘。Canvas 的 Java mBitmap 与 Bitmap.mBuffer 形成强边，native 像素借用随句柄退役。
  VM 锁串行化 Canvas 绘制和 NDK lock/unlock。NDK 持锁期间可能在 VM 锁外写像素，Canvas 拒绝
  读写该 Bitmap（包括图层合成），抛 IllegalStateException。回收后已有 NDK 锁可正常归还。
  最后一次 NDK unlock、Canvas 绘制及图层合成推进像素版本；Canvas 绘制到回收目标抛 RuntimeException。
  不支持 WebP 编码和共享内存 Parcel blob。
- StatFs 字段/构造/restat/getter 归 Java，Posix.statvfs 只查 VFS 沙盒配额卷，其他卷失败、无 host
  空间采样，见 [ADR-0122](../../../../docs/adr/runtime.md#adr-0122)。SQLite 固定真实引擎，只经 VFS
  访问主库/journal/临时文件；Java 拥有事务/连接池/Cursor，native 仅 ABI/token/错误码/受审配置。
  损坏由引擎/原处理器判定，teardown 取消/关闭 host 资源；CursorWindow 有界且遵循
  requiredPos/countAllRows，时间经 Clock，journal 模式取 ROM 资源配置、无 WAL。SharedPreferencesImpl/EditorImpl 整类执行 ROM，
  应用数据根的 XML 经同一 VFS 持久化；缓存、监听和 apply 排队由原版 Java 管理，无宿主偏好表。
- Parcel 由 ROM 初始化，native 提供 VM 隔离 backing/游标及本地 Binder 记录。GC 通过 Java token 追踪记录并回收 backing。长度
  先验、含对象拒绝 marshall、无 FD/远程 Binder。Binder 线程策略保存 mask/GATHER 但仅记录；
  StrictMode 只查询/清理无 violation 状态，无检测/violation 编解码，异常编码仍归 Java。
- Parcel 的 open/dup/close/clearFileDescriptor 经 guest VFS；clear 只解除登记并置 -1，不关闭 fd。
  nativeWrite/ReadFileDescriptor 仍记账失败；不提供 pipe/socket/跨进程 comm 通道。
- Log Throwable/getStackTraceString 共用 guest printStackTrace(PrintWriter)，flush 后取字符串、保留
  虚派/异常；null 或 cause 链含 UnknownHostException 子类返回空串，不按消息判断。EventLog 保留
  API19 类型/截断/返回值，无服务时读取失败；均用结构化日志。

## UI、Surface 与输入

### 预备的进程内窗口服务

现行 session 显示和输入仍使用下述 JavaTraversal 协议。独立的本地 AMS、WMS、input 和
input_method 接口供 ROM 窗口切换使用；它们不读取或修改投影树，不构造宿主 View 层级。
AMS 保存进程内 Activity token、任务与结果，经 IApplicationThread 的 schedule 系列排队执行
ROM ActivityThread；pause、stop、销毁确认与启动完成分开记录。退出 deadline 使用统一 uptime，
待销毁的实例不能被计作退出完成。宿主挂起/恢复只提交状态请求，不直接调用 Activity 回调。
目前生产 session 尚未接管到这条 Activity 路径。
预备服务与 native 绑定在进程组装时显式启用。现行生产装配保留原 ServiceManager 目录、
Activity 和 window 服务方法及 native 绑定；没有 CLI 选项或运行时显示模式切换。
正式切换时这道阶段门随旧路径一起退役。

WMS 的单显示不提供系统装饰，系统 inset 为零；应用窗口、媒体/媒体叠层子窗口、面板与 Toast
按 API19 层级排列。ROM 已完成的 PopupWindow 锚点坐标和 gravity 是窗口布局输入。窗口 frame
与 producer buffer 尺寸独立；同一 Activity token 控制其窗口组可见性。未知服务方法按完整签名
记账并抛异常。窗口动画不执行，多显示、系统窗口及非单位 SurfaceControl 矩阵不支持。

Surface native 使用分配的进程内句柄，引用不携带宿主地址。软件 Canvas 持续保存 backing，
首次锁定或尺寸/格式变化要求完整重画，其余锁定裁剪 dirty 并保留外部像素。post 发布独立、
不可变的完整 RGBA 帧，绘制中的 backing 不交给合成线程。SurfaceControl 事务整体发布属性；
合成器只消费 WMS 顺序与共享像素，按 format、alpha、crop 和 transparent region 合成，
不消费 Java View 或 UiTree。未 post 的窗口不提供图层；透明证书与全屏不透明借用路径不在每帧
扫描像素。空输出由 FrameService 的黑底标记表达；生产 FrameService/GL/视频接线留待切换，
现行呈现顺序不变。

窗口输入按 frame、触摸区域、flags 和焦点选择目标，DOWN 捕获到 UP/CANCEL；隐藏和移除取消
捕获。InputChannel 持有进程内队列，InputEventReceiver 在其 MessageQueue 所属线程的
nativePollOnce 内执行 ROM 私有派发入口；MOVE 通过 Choreographer 的批处理请求消费，完成确认
释放分发中的事件。InputQueue native 经显式 NDK 队列投递和完成回调桥接，宿主完成回调不进入 VM，
而是唤醒 Java 队列后在原线程交付。MotionEvent native 保存独立快照，VelocityTracker 使用 API19
默认 lsq2 的二次最小二乘、采样窗口和停止检测；其他速度策略明确失败。

IMMS 保存 client 与 InputContext，show/hide 驱动注入的宿主文本输入开关，文本和编辑中文本分别
调用 commitText 和 setComposingText；失焦关闭输入。没有宿主开关时 show 明确失败；不创建输入法
应用、服务进程或软键盘界面。ROM InputMethodManager/EditText 与 SDL 的实际连接待窗口切换。
宿主帧源可只排队统一 Clock 的 vsync，不泵送消息或执行 guest；现行驱动保留其原有安全点。

宿主协议回归见 `rom_window_system_tests.cpp`、`rom_window_services_tests.cpp` 和
`rom_activity_service_tests.cpp`。它们不能代替 ROM ViewRootImpl、真实 UI APK 和游戏显示的切换验收。

### 现行 JavaTraversal 路径

`EnsureAndroidUiTraversal` 是 integration/session 的唯一 guest 布局调度入口，调用方显式提供 VM、
reason 与 phase。owner 在 context 构造时固定为 JavaTraversal，无用户配置、运行期切换或
Legacy guest 遍历。生产启动类路径为 ogplay.jar 与原样 API19 ROM jars；制品由 payload 校验锁定。ROM 执行 hierarchy、参数、measure/RTL/缓存及
layout/setFrame/sizeChange，Java 唯一拥有 measured、局部 bounds、旧 specs 与相关 flags。
onSizeChanged 由 ROM 同步调用，先于 onLayout；getter 读当前 Java 字段，重入不再遍历。
`ReadAndroidUiGeometry` 在 owner 回调内从当前 Java 字段建立临时 const 投影，
只在副本校验/解析坐标，不发布、不准备叶光栅、不派 Surface 或执行 guest。
VM 主 guest context 的状态表装配绑定唯一宿主 owner，不由首个 View 构造绑定。
OwnerTick 开始一个逻辑 tick；BeforeInput 是同 tick 读屏障，不消费第二 pass 的延期。
每个稳定窗口根独立保存请求、延期/watchdog 与最近成功身份/metrics；clean 复用完整快照，
不调用 measure/layout、不推进 epoch。纯绘制变化只重新冻结投影，不调用 measure/layout。
View.setBackgroundColor、invalidateDrawable、verifyDrawable 执行 ROM；既有 invalidate 接缝标脏。
owner 投影保留 ROM skin 路径，ColorDrawable（含子类）每次虚派 getColor 读取已含 Drawable alpha
的 ARGB，经统一转换为宿主 RGBA，背景 alpha 为1；零 alpha 或 null 清空背景。其他非空 Drawable 不绘制，记账
`dexvm.view_background_draw` 并以 first_and_count 发出 `ui.background.unreviewed` warn（含类描述符）。
受审 ColorDrawable 外，catalog 子类未准入的状态重写显式记账并抛 UOE，不能继承
Drawable.mutate 的默认返回；ROM skin 的既有 opacity 查询边界保持不变。
OwnerTick/BeforeInput/BeforePresent/BeforeSurfaceCallbacks 的 ResolveOnly 与 ResolveAndNotifySizes
在 owner guest token、绑定宿主线程与 VM 锁下执行根
EXACTLY managed viewport measure→layout。requestLayout 沿真实 Java parent 虚派传播，首轮仅剩余 FORCE_LAYOUT、仍连到当前根且祖先非 GONE 的请求触发本帧
第二轮；第二轮请求不置 FORCE_LAYOUT，延至下一 tick。连续跨 tick 请求超过8次明确失败，
保留队列并记录 requester/epoch。系统接缝不构造 AttachInfo/ViewRootImpl/Binder。
初始化/requestLayout/invalidate/无变换矩阵及 ApplicationInfo 接缝逐项受审；invalidate 只标记
本进程 dirty 并维护软件 Java flags，已有硬件/动画/变换/附着系统对象记账拒绝，不能当 null。
`CommitAndroidMeasuredRootProjection` 在同一受检批次经绑定 token 读取 Java parent/children/count、
ID、measured/bounds/padding/clip，检查双向父子、数组计数、环与深度，再发布完整 hierarchy/id/geometry
同一 epoch；合成根使用 viewport。mParent、mChildren 数组与 mLayoutParams 引用均为 Java 唯一权威，
其强边由 VM 正常追踪。绑定/请求/watchdog 仅保存弱身份，不作为 JavaTraversal GC roots；
GC sweep 仅退役构建缓存身份，最后不可变投影保持到下一完整发布，发布期间使用 RootScope。
guest 根 mParent 为真实 null，不存在 ViewRootImpl。回调异常保留原异常和已发生的 Java 修改，
不发布半成品；换根、binding、内容/窗口代际或 managed metrics 改变取消旧发布。
C++ 不写 measured/bounds，不重排外部树；ui 不获得 VM、Java 字段或引用。
Java flags/listener/focus 以 Java mViewFlags、mListenerInfo、PFLAG_FOCUSED 与 ViewGroup.mFocused
为唯一权威。ROM setter/getter 和 requestFocus/clearFocus/父链转移执行字节码；integration
通过逐字段绑定 token 读取 ListenerInfo 强边；不保留宿主 listener/focus 镜像。visibility、
enabled/clickable/focusable/touch-mode 与 hierarchy/geometry 在同一投影事务提交；ui 不持 listener。
输入沿既有单目标、不 split 协议，捕获仅保存服务身份；提交后发现 detach、隐藏、禁用或换根
即清捕获。InputMethodManager 每次服务调用从 Java findFocus 捕获候选，不建立宿主焦点 owner。
测试容器显式注入 disabled accessibility provider，singleton 与 enabled 值仍在 Java；未配置、
非 null AttachInfo、动画/硬件、accessibility focus、背景状态等未支持服务明确记账失败。
不宣称构造完整 AccessibilityManager 服务；除受审 getInstance/isEnabled 外其成员均 failure。
输入重入未完成 traversal 明确拒绝；GeometryRead 仅允许进行中的回调读取临时投影。
HostDialog 与回调外 GeometryRead 明确失败并记账；Surface 生命周期完整代次校验与 E2E 待后续 WU，
尺寸 phase 由 ROM layout 同步完成 onSizeChanged；Surface 消费遍历后的本轮 pending。
managed-window 坐标查询读取临时 Java 投影，含父链/祖先 scroll；全屏窗口 screen 原点为0，
不创建 AttachInfo。窗口可见性与空 scrollbar cache 使用 ROM；focus adapter 取消软件捕获、
清 Java pressed/prepressed、虚派 ROM onFocusLost 并刷新 drawable state，不创建 IME。
TextView 的窗口焦点及 SurfaceView 的可见性接缝精确调用 View 父实现，排除 Editor/WMS。
GLSurfaceView 在配对精确调用 SurfaceView 父构造，保留 requested=-1、willNotDraw 与真实 holder。
Activity 栈的窗口根关联真实 Java 内容 FrameLayout；创建前仅查询空投影根，不附着第二棵宿主层级。
来源表只开放所选 ROM 闭包与受审接缝。容器构造初始化真实 Java 数组/默认 flags，
空 touch/hover target 允许原空路径，非空输入目标及 disappearing animation 记账拒绝；
View 树 AttachInfo/dispatchAttachedToWindow、transition 与系统 accessibility 不伪造服务。
Activity 的 Window.Callback 附着/分离由 session 单独编排，Dialog Callback 附着未支持。字段接收者
failure 虚派按 ADR-0159 封存 a/b/unproven 信息，运行期仍明确失败；支持路径须动态零命中。
JavaTraversal 的 Dialog 使用各自独立 HostLayout 树；ShowAndroidDialog 只在未发布宿主树上
按 managed viewport 统一计算 show/resize：消息 ScrollView 收缩、偏移夹紧，成功才替换对应
presentation 并推进宿主 revision。
失败保留旧 modal、guest 投影/epoch、焦点与 revision；纯 modal 操作不请求 guest 遍历。
UiNodeId 只在所属 modal 树内有效，presentation 的 partition/generation/revision 区分相同原始 ID。
按钮焦点仅归宿主树；prior_guest_focus 只存真实 Java View/root 身份与窗口 generation，
从绑定字段读取，不执行 guest findFocus 回调；guest 焦点未转移，hide/dismiss 不向外部投影请求焦点。
共同 PresentationSnapshot 以 shared_ptr 原子发布：presentation_epoch、guest_window_epoch_refs、
host_modal_revision、metrics_revision、viewport 与可见 modal_stack。guest 部分只引用完整窗口
投影，不复制几何；modal 部分冻结独立宿主树，发布后不共享构建树的可变焦点/可见性。
owner 完成 Java measure/layout 后先准备全部待 resize 的宿主分区，再提交 guest 投影；
resize 失败不发布新 guest epoch，Java 已执行状态不回滚。尺寸/density 与 guest 测量事实匹配才
发布。设备metrics待齐时，旧modal的隐藏/退役可沿最后完整guest/metrics发布移除；新几何
不得混入该旧序列。非法 metrics/文本等失败保留旧共同快照及宿主集合，不回滚已经发生的Java测量或独立guest投影。
纯 modal 变更不测量/布局 guest、不推进 guest epoch；owner 可见性/退役先撤销捕获再发布集合。
输入持共同快照，最上层可见 modal 优先吞输入；捕获含 partition/generation/Dialog 身份。
更新/隐藏/取消/owner退役使捕获失效，已取消序列仅消费尾事件，不对新 modal/guest 触发动作。
按钮/Back/Tab/Enter 进入既有 Java 协议及 owner Looper；回调改树只请求下一 owner 遍历。
宿主消息区内起始的拖动超过 8dp 才滚动，按钮起始不滚动；DPAD 上下经 Java OnKeyListener
优先处理后按半视口滚动，无可滚动内容返回 false。滚动推进树修改计数，冻结快照保持不可变。
普通触摸也持有事件快照租约；几何命中读该快照，不在嵌套分发中重测。
Compose 只读同一共同快照，video/raster/hit 共用 guest 投影与 modal screen_frame；帧尺寸
不匹配明确失败，不把新 viewport 与旧几何混合。Dialog 必须有独立 HostLayout 树，缺树明确失败。
UI 下层不获 VM 或 Java 引用。

View 以 ROM 类为准（MergesBootClass）：ROM 唯一拥有完整形状及 clinit；带 handler 的声明覆盖对应方法。
未迁移、仍由宿主维护的 ROM 实例字段在 guest 访问时明确失败；integration 的受审 adapter
可访问自身绑定槽，不以这些槽宣称 Java 已拥有整组状态。mContext 由构造/inflation 写入；
SDK 兼容静态 flags 从注入的当前进程 targetSdk 按 API19 条件初始化。
非 ROM decor owner 与叶文本服务引用由 integration 的 owner-attached 表保存，
从 View trace、随 View sweep，脱离 UiTree 后仍保持引用；不向 View 添加额外字段。
mResources 是构造时 Context.getResources 的 Java 强引用；Context 预绑定只写 mContext，
构造每次解析资源且异常透传，失败不提前置位 SDK 兼容标志。padding 四边、minimum 两维及有界 absolute user-padding/defined
字段由 Java 唯一拥有；构造、XML、已支持 widget defaults/背景 adapter 写 Java，不写宿主权威。
UiNode.padding/minimum 对 guest 仅为输入投影；
仅在真实请求/窗口变化触发遍历时导入。干净帧不比较直接字段写入；
直接写字段或修改派生缓存不等于 Java requestLayout/invalidate。只读导入不改变几何 owner。
getResources、上下 padding 与 minimum getter 执行 ROM；左右 absolute padding getter 和
minimum setter 保留受审 adapter，后者真实虚派 requestLayout。setPadding/internalSetPadding、
resolvePadding 与 scrollbar inset 执行 ROM；mPrivateFlags2、真实 ApplicationInfo.targetSdkVersion
决定 RTL，mUserPadding 保留原输入，只有实际变化才虚派 requestLayout。未选 relative-padding API 仍失败。
ViewConfiguration 的原 ROM Context 构造器前，受审实例输入接缝只写两个 private 菜单键字段；
Resources 读取同 ROM override 与 showNavigationBar，再由现有 qemu.hw.mainkeys 属性覆盖，
读失败透传。显示事实采用 framework-res 的实际配置，不引入 Binder 或新快照字段。
Display.getRealSize 使用现有真实显示配置。Interpolator native 只用参数时间维护关键帧/repeat/mirror；
Shader、混合、颜色/遮罩滤镜与路径效果通过分型 token 持有 Skia 对象，Paint 的 ROM Java 引用
参与 GC 追踪；native 复制共享 Skia 引用。局部矩阵生成新的不可变 Shader 包装，
BitmapShader 保留初始像素引用并跟随后续写入。HWUI post-create peer 按软件构建返回 0。
可见 scrollbar 仅记账不改像素，
ROM awaken/cache.run 经已有 Handler 与统一时钟推进；无 AttachInfo 时投影只向既有 ViewRoot Handler 队列转发真实 fade deadline。
ui 不读取 Java 字段；guest measured/bounds 与遍历权威唯一归 Java。

Window 在每实例 synthetic root 下持有稳定真实 FrameLayout，android.R.id.content/parent 保持身份且
不跨窗口；Activity 委托 Window，setContentView 替换、addContentView 追加，null 参数用默认值。
Window 背景由 Java 强引用保存并套用到唯一内容根；不模拟窗口格式变化或主题 windowBackground 默认背景。
content_view 仅首个 View 投影、window root 是遍历边界；移除保留 View/参数/几何，实例退役才清理。
onContentChanged 虚派、异常保持身份，重入换代中止旧操作，不回滚已执行回调；无主题 Decor/ActionBar。
每 Window 独立 LayoutParams；API19 公开实例字段走 intrinsic 真实字段，setAttributes 按
copyFrom 复制，已赋 token/packageName 保留；亮度等只保存，不作用于宿主屏幕。
Decor.dispatchTouchEvent 虚派 owner Activity；DOWN onUserInteraction，随后 Window 内容树
分发，未消费回退 Activity.onTouchEvent。View enabled listener 优先再 onTouchEvent；
ViewGroup 先 intercept 再 reverse-Z 子树，接管以 CANCEL 取消子目标。宿主与 guest 共用
捕获 owner，DOWN 捕获目标接收 MOVE/UP/POINTER_DOWN/UP，不 split，其他 action 记账失败；
detach/隐藏或 Activity 切换清捕获（ADR-0149）。

LayoutInflater inflate/Factory/Filter/cache/clone 与 include/merge 执行配对 ROM；Context
直接构造同 ROM PhoneLayoutInflater。旧 native registry 构建树接口不再提供 guest inflater。
Window XML 通过未发布父容器隔离失败，再将成功 Java 子树安装到稳定内容根。

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
frame 尺寸（AOSP 4.4 mWinFrame）、destroyed 保留末值，旧 holder 不随新代更新。
Rect 与 surfaceChanged 的 buffer 尺寸可不同。View holder 的正请求使用独立
buffer，各非正轴回到 View frame；window holder 保留窗口尺寸。每轴上限 4096（RGBA ≤64 MiB），
超限抛 IllegalArgumentException；未知 holder 记账拒绝。尺寸改变由下一 owner 安全点派 changed，
同值不重发，激活前的请求在 created/changed 直接生效。
SurfaceView 保留完整 ROM shape，onMeasure/isFixedSize 执行 ROM；三构造受审调用对应 View
构造并初始化请求为 −1、setWillNotDraw(true)，getHolder 继续返回真实 managed holder。setFrame
仅精确调用 ROM View 父协议，不执行 WMS updateWindow；WMS 相关字段/方法保留 failure。
SurfaceView setVisibility 先精确调用 ROM View，再维护派生的窗口/View/requested 可见性，
requested 变化调用 requestLayout 并登记既有 holder 对账。窗口事实来自现有 owner/window；
普通 owner 隐藏在返回前走原 DispatchHolderCallbacks/DestroyScope 销毁，显示等下一个安全点
created→changed。layout/Surface 回调/input lease/worker 中延后销毁，避免重入；祖先隐藏的
现有 IsVisible/mount 判定不变。managed attach/detach 复用该协议，不开放 View AttachInfo/WMS。
draw/dispatchDraw 委托父 View，当前父 Java Canvas 协议仍失败并记账；gatherTransparentRegion
仅支持 media/overlay+willNotDraw+null Region 的格式 opaque 返回；Region 写入、自绘/onTop
父 gathering 记账失败。setWindowType 仅映射既有 PANEL/MEDIA/MEDIA_OVERLAY，其他类型失败；
setSecure(true) 因无安全显示/截屏排除而记账失败，false 无保护请求。
holder 接缝把非正宿主请求（0 表示从布局）规范为 ROM −1；先校验双轴再原子写 Java 请求，
改变时真实 requestLayout。setSizeFromLayout 恢复两轴 −1。ROM onMeasure 保留读取请求的
原版行为；View frame 与 buffer 几何分别发布，回调前更新真实 Rect。
请求由 Java mRequestedWidth/Height 唯一持有，不建宿主请求镜像。
SurfaceView(Context,null) 复用 View 初始化，null Context NPE、非空 AttributeSet/三参数 defStyle
不支持；注册 XML 沿 Context 构造及既有属性投影。层序唯一归 UiTree，setZOrderMediaOverlay/
setZOrderOnTop 后调用覆盖前调用；compositor 即时消费，不承诺 WMS 重建/任意 GLES 多层。
View.bringToFront 虚派 UiTree 父 ViewParent.bringChildToFront，合成 root 按 ViewRootImpl 空操作；
ViewGroup.bringChildToFront 忽略非子节点/null，移至末尾后虚派 requestLayout/invalidate。
setKeepScreenOn(false) 无需宿主动作；true 仅在宿主窗口报告已阻止显示休眠时成功，否则记账拒绝。
SurfaceHolder 委托自研 Surface 的 lockCanvas/unlockCanvasAndPost。软件锁仅接受 active managed
holder，直接在 buffer 尺寸的 Skia raster device 绘制；dirty 扩展到全 buffer。post 退役该 Canvas，
把像素转为合成器的非预乘 RGBA8 快照。View holder 合成到 View frame，窗口 holder 发布软件基帧。
created/destroyed 与 Surface.release 撤销旧的锁定及节拍状态。
context `frame_period_ns` 非零时，生命周期驱动线程以外的 unlockCanvasAndPost 在发布后按 holder 的
`hal::FramePacer` 释放 VM 执行锁等待槽位；驱动线程由宿主帧循环节拍，不再等待。

NativeActivity/NativeContentView 执行原 Java，private native 经 NativeActivityRuntime 接进程；
takeSurface/takeInputQueue 支持 null/切换，先销毁旧 owner，首次遍历前解除不收旧事件。
Callback2 changed 后 redraw，首次/dirty traversal 向 attached observer 发 global layout，
getLocationInWindow 用 UiTree screen frame。

guest 焦点归 Java；纯宿主 Dialog 的焦点由其独立 UiTree 拥有。
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
MotionEvent.obtain(MotionEvent) 分配独立 InputEvent 与完整快照，保留全部 pointer/axis/offset/历史；
原件和副本可独立 recycle/批量追加，null 按 API19 抛 IllegalArgumentException。

JavaTraversal 的 Window 根从 Activity.mWindow→Window.mContentParent 的受审只读 token
取得真实 FrameLayout，content_view 仅保留首个内容身份；owner 屏障即使空 Activity 也建立该根，
不能用内容 child 代替。无 Activity 的独立测试根沿用原夹具；有 Activity 却无根不回退。
生产 MeasureSpec 取 managed surface 与配置；显式 viewport 参数仅用于受控 CPU 夹具，
保留其 managed viewport 与实际 holder buffer 分离的测量控制。
Window 的默认参数、增添/替换内容沿真实 Java hierarchy；替换复用 ROM removeAllViews。
FrameLayout 的 Context 构造（含 Rect/ArrayList）、measure/layout、默认/复制参数、foreground
padding 算法与 MarginLayoutParams 方向解析、Gravity 归 ROM。未启用容器的 own
onMeasure/onLayout 在配对表面显式 failure，不能继承基础算法取得实现信用。
仅有界 managed 窗口；foreground Drawable 设置/绘制与系统附着仍明确失败。
完整窗口树成功后只提交一个 VD-02 投影事务；UiTree 不回填几何，
LinearLayout 的 Context 构造、weight/baseline/largest-child 状态、双方向测量/布局、参数生成/
复制与 Gravity/RTL 解析归 ROM；共享 View/Group adapter 只初始化既有基础字段，不写
LinearLayout 的算法状态或几何。Context-only 默认 weightSum=0，与属性构造的 -1 不混用。
非空 divider 属性/绘制与动画接缝继续记账失败；divider 测量
helper 只读取 Java 输入，测试直接写尺寸不代表 Drawable 服务。叶类型不扩充。
RelativeLayout 的非属性构造、双轴依赖图排序、size/position/center/baseline、onMeasure/onLayout
及 LayoutParams addRule/removeRule/getRules/resolveRules/resolveLayoutDirection 归固定 ROM；
MarginLayoutParams 方向解析继续执行 ROM。缺失锚点与 GONE 链按原算法回退，依赖环由 ROM
抛 ISE；不套用 HostLayout 的缺失锚点拒绝。SDK 测量兼容字段由原构造查询真实 ApplicationInfo，
宿主只扩大初始化类型守卫，不写 RelativeLayout 算法字段。系统
accessibility 入口保持 failure，Window 整树仍经同一事务投影。

属性构造沿原 ROM Context 四个 obtainStyledAttributes、ContextThemeWrapper 的主题初始化/
覆盖与 Resources/Theme/TypedArray。基础 Context 的进程主题 adapter 只缓存真实 Java Theme
身份，采用 Manifest application theme 与 ROM 默认主题选择；wrapper 保留真实虚派与 setTo。
ROM Context 没有 mOgplayInflater，catalog 不声明额外字段；基础进程 Context 服务
缓存本地 ROM inflater 子类，ContextThemeWrapper 沿 ROM 每实例 cloneInContext/缓存。
不再维护旧 inflater 字段或缓存。
View、具体测试 ViewGroup 与 Frame/Linear/RelativeLayout 的两/三参属性构造、五组 LP 的
属性构造与 generateLayoutParams 执行 ROM；width/height 缺席或类型不符由 getLayoutDimension
抛异常。宿主不补尺寸或重写属性几何，遍历仍只读 Java 输入。应用主题可显式 @null 覆盖
基础主题 scrollbar 引用；thumb 从 framework-res 加载，非零标志初始化执行 ROM，绘制仍仅记账。LinearLayout 属性构造缺席 weightSum=-1，与 Context-only 的0保持区分；
null divider 设置沿 ROM，绘制仍失败。

LayoutInflater 的 inflate 各重载、递归、include/merge、Factory/Factory2 链归原 ROM；
createView 为受审反射 adapter，真实虚派 guest ClassLoader.loadClass/Class.asSubclass/
getConstructor/Constructor.newInstance。sConstructorMap、mFilterMap、mConstructorArgs 为唯一
Java 状态，参数恢复及 onFinishInflate 时序归 ROM；异常类型/文本/cause 沿 ROM 分支。
Context 服务直接构造同 ROM policy 的 PhoneLayoutInflater，前缀顺序与克隆执行原版；
仅准入该类，独立 policy reference/基线及成员、父成员、clinit 审计均须通过。
不加载 Policy/PhoneWindow/PhoneWindowManager 或 Binder。
Resources.getLayout 在当前 APK 选择已知 layout，返回既有受检 XmlBlock.Parser；不读系统 XML。
Window XML 先膨胀到未发布 FrameLayout，完整成功后转移到稳定 mContentParent；失败保留
旧 Java 内容及旧代投影。View/LP 重载仍沿真实 Java hierarchy，无第二份几何写入。
仅五类基础容器及应用 View/ViewGroup 子类可成功构造；其他控件、特殊 blink 动画记账失败。
View tag trace begin/end 仅转结构化 debug 日志，其他 tag 明确失败，无系统 atrace。

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
保活 Runnable，在所属 GLThread、renderer 前 FIFO 执行、不依赖绘帧或 EGL 已初始化。
JavaTraversal 在 owner 安全点接通未附着 producer 的排队初始化；native window holder 的
帧继续借用完整 presentation lease，空内容不省略快照或尺寸校验。requestRender/WHEN_DIRTY
单次消费；PreserveEGLContextOnPause 默认 false、Java 字段/可覆盖方法、可先于 renderer 设置。
onPause/onResume 在所属线程确认、释放/恢复 EGLSurface，Context 按 preserve 保留/重建，
backend context-loss 按验证范围记账。

Handler、Looper、Message、MessageQueue、HandlerThread、AsyncTask 由 ROM 执行。
MessageQueue native 等待按统一 uptime，Clock 推进唤醒等待。同步屏障、异步消息与 IdleHandler
由 Java 队列排序。主 Looper 在 guest owner 线程连续执行；帧安全点保留有界到期消息泵用于现有遍历顺序。
View.post/postDelayed 以 Java mParent 链到达存活受管窗口根（非零附着代次）判定附着，
不读取 ui_tree 投影；附着时进入单一 ViewRoot Handler。detached 按 guest 线程暂存，
存在该窗口后在相同线程 lifecycle safe point 转入，delay 从转入起算。负 delay 为零、
null Runnable 抛 NPE、始终异步；pending/scheduled Runnable 为 session roots，shutdown 清空。
不构造 AttachInfo/ViewRootImpl/RunQueue；其他 View posting API 仍按配方明确失败。
ROM SystemClock.sleep 走 Java Thread.sleep；Clock 默认确定性 60 秒 boot-age，无 suspend 时 uptime/elapsedRealtime 同源。
MessageQueue native 与 NDK ALooper 使用同一线程的 boundary Looper、wake 位和 poll 等待。
Java 的延迟/屏障仍归 ROM MessageQueue，native 用统一 uptime 判断期限；Clock 推进、队列唤醒及退出
通过显式接口唤醒同一个 native Looper。独立 VM 单测没有 native process 时复用条件变量等待。
Java 队列持消息，quit 不清存活线程 native Looper。root Thread 稳定；
排队工作只在队内是 session root，有到期工作时从出队到调用持 VM 锁，防止其他 Java 线程在此间回收；
myQueue/getQueue 共享稳定队列身份；runOnUiThread 仅 root 同步虚派、worker 投主 Looper。worker join 后/root
JNI detach 前释放 token；AsyncTask DexVmError 保留故障并终止，不转 null/继续 onPostExecute，Thread/JNI
复用 core。ResultReceiver 本地协议归 Java，有 Handler 排队/无 Handler 同步，Parcel 保留本地 Binder identity。

## 媒体、Dialog 与 WebView

MediaPlayer/VideoView 仅受检资源/路径/逻辑 FD 区间，复用 decoder/mixer，无 host fd/路径反查/第二
播放器；EncodedMusicMixer 按实例、JavaSoundPoolMixer 按池隔离。音乐增量读 window/lease，短音效
才全量读取；来源原子捕获、关闭 FD 不影响区间、同路径替换不复用旧缓存。STORED 媒体完整 CRC
校验一次后借 payload，APK 随 context 保活且不变；Deflate 沿逐窗口校验、损坏失败。
MediaPlayer/SoundPool 的 FileDescriptor：无真实 handle 的平台 apk_entry 沿原来源；已登记真实
handle 与未登记 fd 均按 API19 jniGetFDFromFileDescriptor
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

Dialog/AlertDialog 协议归 Java，native 只投影纯文本标题/消息/三按钮为 UiTree 临时模态
子树，active presentation 为 Dialog/Activity roots；按 geometry 捕获输入/取消旧捕获，listener 在
owner Looper 排队，owner 隐藏/恢复同步可见性，实例退役先 dismiss、进程停止仅退役投影。
不替换内容/退役 Surface/另建协议或层级权威；最多 8 窗口，title/button≤512/128 UTF-16，
message≤kMaxFixedTextUnits；非 String、超预算、density 不在 (0,8] 或 viewport<48×48 明确失败。
标题最多两行，消息独立 ScrollView；按钮超宽时等分且最多两行，顺序不变。消息超高收缩并滚动，
resize 保留并夹紧偏移；chrome 超高夹紧并记录 UI 降级，不因容纳失败抛异常。
无滚轮、滚动条、fling 或 overscroll；items/Window facade 拒绝；非 String 文本转 toString、超长文本截断，均记录 UI 降级。
非零 Dialog theme 记录 UI 降级。setContentView(View) 使用居中 FrameLayout 作为 Activity 应用附加窗口，
显示/隐藏/关闭及 Back 走 Java 协议，隐藏时撤销输入/焦点资格；没有可用 Activity 明确失败，布局资源重载仍拒绝。每个 modal 为独立 HostLayout，
字体/布局/像素复用既有底座，经共同呈现快照合成与命中。

WebView.destroy 可覆盖、检查构造 Looper，targetSdk≥18 跨线程 RuntimeException；清理 settings/
client/interface、不重建、防御重复销毁，不承诺完整 AOSP 等价。配置/RenderPriority 仅状态/提示，
不改 host 调度，Java 字段/HashMap 保活引用。默认无浏览器：HTTP/数据加载异步
onReceivedError(ERROR_UNSUPPORTED_SCHEME)，javascript 仅日志，stop/destroy 取消回调、历史空；
严格诊断 UOE。外部 HTTP/HTTPS ACTION_VIEW 仅记录返回，同包显式 Activity 不变。

## JNI、诊断与验证边界

load/loadLibrary 经 process loader 携 application ClassLoader，应用调用 loadLibrary 先走 ROM
PathClassLoader.findLibrary 的 VFS 搜索，失败为 Java 异常。soundpool/media_jni
仅平台库身份、不加载 ELF，固定 Conscrypt javacrypto 映射 ogplay_jni、其他名称不改写。JNI 对象按
真实 runtime class 原子幂等注册，数组元素不猜声明类型。Mac/MacSpi/HmacSHA1 SPI 归 BootDex/
Conscrypt，经 AOSP NativeCrypto ABI 调 guest ARM libcrypto、token 共用 GC/teardown；无 BKS/KeyStore/
BouncyCastle/TLS 承诺。

Dashboard AudioTrack/UiTree 仅 TryAcquire VM 锁、PCM 用 mixer try-lock，忙 nullopt；UI 计数含根、
不 layout/draw，视频快照 try-lock≤128 项、不调 decoder，base_position 不冒充实时位置。
Animation listener 仅类型、无执行器。View 的 setAnimation/startAnimation/clearAnimation 执行 ROM（DVM-371），mCurrentAnimation 为惰性 Java 状态：不推进、不回调 listener，invalidate 记录 UI 降级。定向入口为 tests/dexvm 的 android/widget/layout/scheduler/egl、
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

Context 服务派发与 dump-surface 共用 platform_tables.h：未知名称返回 null，注册但
未提供的服务记账并抛 UOE。DisplayManager 只查询既有 managed Display；不提供
外接/虚拟显示及监听。属性来源经显式 core 回调注入；有 ICU 导出时查询 guest，
无导出时保留随附 manifest 元数据生成的默认值，不隐式加载或访问宿主 ICU。

## 图形几何 native

几何类与枚举执行 API19 ROM Java。矩阵、路径、区域及迭代器的对象操作绑定 Skia CPU，
经分型 int 令牌寻址；ROM finalizer、owner sweep 和会话退出幂等退役。区域迭代保有独立快照。
投影读取同一 native 矩阵的 identity 状态，继续使用现有不变换的 UI 几何。
路径布尔运算沿 API19 的五种 Op 映射至 Skia PathOps，失败时保留目标路径。硬件 Canvas 查询返回不可用。

## ROM Paint、Typeface 与文字

ROM 构造、参数检查、兼容缩放及 Java 效果强边执行原字节码。Paint 的分型 int 令牌持有
SkPaint、SkFont、字体引用、文字属性和效果引用，native reset/set/复制与 ROM 字段同步。
终结及 GC sweep 幂等退役会话句柄。系统字体由 context 注入，按 API19 family/别名选面，
APK asset 经 AssetManager 读取，文件只走 VFS。无效字体按 ROM 构造器抛 RuntimeException。
Paint 和软件 Canvas 使用同一字形、度量及回退，未整形或未重排的文字按逻辑顺序处理并记显示降级。
几何、位图、网格/顶点及文字使用同一 Paint 效果，PorterDuff 模式共用统一映射。
Blur 半径转换为 API19 sigma，颜色矩阵平移按 0～255 换算，阴影半径非正时清除。
DrawFilter 在绘制副本上清除和设置 flags，native 状态随 Canvas save/restore 保存。
Rasterizer setter 记录显示降级，其他排除的返回值能力由未绑定 native 明确失败。
NinePatch chunk 由 ROM Bitmap 持有；副本共享 chunk 时最后一个 Java owner sweep 后退役。
Camera 的变换、位置、矩阵与 Canvas 应用使用同一 Skia 投影。

## JavaTraversal TextView 构造输入

ADR-0161：仅配对 JavaTraversal 的三个 TextView 构造为受审 adapter，精确调用 ROM View
属性构造与 Theme/TypedArray；默认 defStyleAttr、appearance/style/theme 优先级真实消费。
UiNode 的文本、gravity、行策略及四绝对位图槽是单一文本输入；字号/字体样式是 TextPaint 的投影。
每 View 的 Java mTextPaint 身份稳定。TextView setter 写穿 native Paint 并清测量缓存。
投影从该 Paint 读取字号、字体样式和 flags，继续使用 runtime/ui 的固定字体度量和栅格。
TextView/ImageView 未实现的展示类 XML 属性与无返回值 setter 经 `RecordUiDegraded` 忽略，按 id
首次写 warn、累计 `ui.degraded.*` 账本警告，运行结束汇总；样式 CharSequence 退化为纯文本；
密码类 password/inputType、超长文本与缺字仍明确失败。
颜色身份归 Java 字段，UiNode 仅持派生 RGBA；
View adapter 状态另存独立 singleLine 位，并保活 Drawable 身份及其不可变 backing，随 View trace/sweep。
mHorizontallyScrolling 由 Java 字段唯一拥有，getter 执行 ROM；setter 值变化才清测量缓存并请求布局/失效。
setSingleLine 同时设置该字段；测量和光栅计划都传独立 no_wrap，只有 singleLine 转换换行为空格。
不实现滚动偏移或 marquee。测量 adapter 不写 mText/mLayout/mEditor。
mHint/mFilters 归 Java，getter 及资源 hint setter 执行 ROM；setText 按序真实虚派 InputFilter，null filters 抛 IAE。
输入类型归有界 adapter，经 setSingleLine 同步行策略，密码变体显示 U+2022；
空正文时测量及光栅计划选择 hint 与当前 hint 色。无 KeyListener、IME 选项或数字/邮箱键盘。
ROM 状态色的 TextView 消费：mTextColor/mHintTextColor/mLinkTextColor/mHighlightColor 保存
真实 Java 值；getTextColors 保持列表身份，hint/link/highlight getter 执行 ROM。
getCurrentTextColor 与叶 RGBA 按真实 getDrawableState→ROM getColorForState 派生，
drawableStateChanged 先执行 View 协议再刷新颜色；GC 通过 Java 字段保活列表。
View 背景按 ROM isStateful 跳过非有状态背景；仅 ROM 皮肤可更新状态，其他有状态背景明确失败。
颜色变化仅失效绘制，不改变测量几何。link 的布局与 Editor 能力仍未开放。

minWidth/minHeight 按样式/XML 优先级消费；像素 getter/setter 执行原版 Java，保留 requestLayout/invalidate。
最小 ems/非零行数与最大尺寸仍未准入。
未知/不支持请求明确失败；全部解析与校验后才发布宿主文本输入，inflater仍原子替换整树。

TextView onMeasure 为 overlay：读取宿主输入及虚派 Java compound padding/suggested minimum，
合并独立 Java TextView 像素最小宽高（不覆盖 View 最小值），调用纯文本叶服务，再虚派 setMeasuredDimension；不写 measured/bounds/flags、不遍历子树。
setText/getText、字号、Typeface、ROM 颜色列表、gravity、singleLine/maxLines、四位图 compound
按逐签名 review 开放，setter 经 Java requestLayout/invalidate，owner tick 重测后完整发布。
每输入版本按最终宽度保存 immutable plan，以兼容 ROM measure cache/补测；baseline 读取同一服务结果，
附加 Java 高度下的 gravity offset。构造/输入与 plan 随 View sweep 退役；无宿主全局强根。
投影中的 text/style/plan/geometry 同 epoch 冻结，presenter 不再测量；失败保留旧发布快照，不回滚 guest 回调。
隐藏祖先下仍收集 Java 层级/几何，但不准备背景、图像或文本行计划；可见分支仍要求有效测量计划。
子类 onMeasure 不调用 super 时保留其 Java 自定尺寸，owner 发布阶段只按最终尺寸准备光栅 plan，
不进入 adapter、不写 guest 几何；没有叶测量结果时基线沿 View 缺席值 -1。
有限 plain text 的 onLayout/setFrame/RTL/drawable-state/padding 回调执行真实 View 父协议；RTL、Editor/状态 Drawable
等未覆盖语义仍失败，ROM 原 TextView 方法不获实现信用。

TextView.setShadowLayer/getShadowRadius/Dx/Dy/Color 以 UiNode.TextShadow 为唯一权威；
按 API19 保存 radius/dx/dy/color，setter 只 invalidate，不重测。radius <= 0 关闭绘制，
实际模糊 radius 上限 32 px 但 getter 保留原值；非有限值或 |dx/dy| > 4096 通过现有文本
失败路径记账并抛 UnsupportedOperationException，失败前不修改状态。XML shadow* 仍不开放。

## JavaTraversal ImageView 叶输入与光栅

JavaTraversal 配对保留 ImageView/ImageButton、八成员 ScaleType 及 Matrix 的完整 ROM 形状和
原 clinit。ImageView 构造接缝执行真实 View/Theme/TypedArray；Context-only 沿源码只初始化，
属性构造保留 defStyleAttr 优先级；与 ROM 一致，只读 View/ImageView styleable，其余 XML 属性
（如 gravity、父容器布局参数）忽略而不拒绝。ImageButton 三构造执行 ROM，真实 imageButtonStyle 和
setFocusable(true) 不被删除；系统 selector/ninepatch 背景拒绝，应用显式受支持主题可成功。
Java mDrawable/mScaleType/mAlpha 唯一拥有图片身份、缩放和 drawableAlpha；BitmapDrawable 的
mBitmap 是 Java 强边。ui_view_images 在此路径仅为弱派生索引，不参与 GC 保活或 setter 读取。
宿主只缓存对象拥有的不可变像素/版本；measure 与发布阶段从 Java 字段和真实 intrinsic 重建
叶输入。像素改变建立新 backing，回收 bitmap 明确失败，旧发布 epoch 仍可消费。
ImageView onMeasure 接缝调用 MeasureImageLeaf 并真实虚派 setMeasuredDimension；读取 Java
padding/suggested minimum，不写 measured/bounds/flags，不递归。无 adjustViewBounds 时按
固定源码 resolveSizeAndState 保留 TOO_SMALL；非空 Drawable 非正 intrinsic 只在测量时取1，
ColorDrawable 原 intrinsic=-1，光栅为内容区纯色填充。
FIT_XY/FIT_CENTER/CENTER/CENTER_CROP/CENTER_INSIDE 复用 ImageDestination；投影同时冻结
intrinsic、像素版本、scale、alpha 和几何，CENTER/INSIDE 使用 density 后 Drawable 尺寸，
像素采样仍用原 backing 尺寸。
Matrix 接缝只初始化实际 identity 状态并提供 isIdentity/getValues/reset/finalize，随 Java 对象 sweep 退役；
原 Matrix.IDENTITY_MATRIX 单例由 ROM clinit/Matrix$1 构造。native_instance/变换操作仍失败。
其余 scaleType、matrix、tint、adjustViewBounds/maxWidth/maxHeight/baseline/crop 等非中性
请求、任意 Drawable/custom draw 继续记账失败；原方法不因 adapter 获得 ROM 算法实现信用。

## JavaTraversal Button 文本路径

JavaTraversal 配对保留 Button 的完整 ROM 形状；三个薄构造执行 ROM，保留非零 buttonStyle
及调用方 defStyleAttr，委托已有 TextView 构造和同一文本/字体/compound/叶测量服务。
Button 与 TextView 同步冻结 line-plan；focus/click 沿真实 View 协议，不增 Button adapter。
plain 文本的 TextView.onFocusChanged 为受审父协议接缝；Editor/spans/marquee/
transformation 不在支持输入内，不读取相应 failure 字段或伪初始化其状态。
Button 不取得旧宿主默认背景、padding、clickable 或行数投影；属性仍通过真实
Theme/TypedArray 解析。默认 selector 由 ROM 选择当前叶；投影读取支持的 NinePatch/Bitmap 或纯色，
应用 style/主题可显式覆盖。accessibility 两个 Button 覆写仍 failure。

## JavaTraversal EditText 有界编辑

JavaTraversal 配对保留 EditText 完整 ROM 形状；三个薄构造及默认 editable/movement、
EDITABLE 强制与 covariant bridge 执行 ROM。editTextStyle 与 defStyleAttr 真解析，默认
系统及应用皮肤由 Resources 加载，selector、padding 和叶尺寸来自当前 ROM 对象；投影读取
NinePatch/Bitmap 的原生 backing。复用同一 TextView 文本与冻结测量服务，完整 guest 绘制仍留待 P3。
TextView.setOnEditorActionListener 执行原版方法，监听器保存在 Java Editor.InputContentType。
有界文本输入不产生 editor action。TextView.onTouchEvent 的受审 overlay 将非编辑 TextView
交给 View.onTouchEvent；EditText 或带 movement 的 TextView 触摸（光标、链接、IME）记账并明确失败。
宿主 UiNode 唯一拥有可变文本；View adapter 仅保存有界 selection 与 Editable/movement 身份，
不写 ROM mText/mEditor/mLayout。Editable 复用既有的
length/toString/clear/replace 服务；其身份从 View 强边保活，反向保活 View，循环随 GC 回收。
编辑使用已有 ApplyTextEdit；提交接缝提交 selection、失效测量缓存并调用 Java requestLayout/
invalidate，先全部 before、提交、全部 on、全部 after，after 可重入。
边界为与 TextView 共用的 ui::kMaxFixedTextUnits（65536）UTF-16单位、已支持固定字体字符、普通字符串与下标 selection；EditText 不另设默认长度，长度语义只来自 InputFilter；完整 Spannable、
IME/composition、Editor 编辑动作、布局对象、修改键移动/删除及未支持字形请求均记账失败。
输入按同一 Java findFocus/父链定位 EditText，消费选区替换、左右移动、删除与文本键；
不使用 focused_edit_text 或 UiTree 焦点作owner。失败校验不改 selection；提交后回调
异常保留已发生的 guest 动作，下一成功 owner tick 才发布新 text/line-plan/geometry epoch。

## JavaTraversal Scroll 构造与双轴滚动

JavaTraversal 配对保留两 Scroll 完整 ROM 形状；构造、Rect/默认字段、fillViewport 属性
消费归 ROM。initScrollView 的配置/scroller 接缝保持真实 Context/metrics 与 ROM OverScroller。
两轴 onMeasure、measureChild/WithMargins、四个单 child addView、requestLayout、onLayout、
onSizeChanged、scrollTo/clamp 及直接依赖执行 ROM；各自主轴先 UNSPECIFIED，fillViewport
补测沿 API19 原式，不额外扣 margin。第二个直接 child 抛 ROM IllegalStateException。
非空 SavedState 恢复、focus 驱动滚动、fling/smoothScroll、scrollbar Drawable 继续明确失败。

默认 overScrollMode 可创建 idle-only EdgeEffect。受审构造保留 Java idle 状态、真实 Rect/
DecelerateInterpolator、density 派生最小宽度；glow/edge Drawable 与资源派生尺寸字段为
failure，标识未支持 glow 资源。isFinished/finish/setSize/setPosition 执行 ROM，idle onRelease
只清 pullDistance；非 idle release、onPull/onAbsorb/draw 记账失败。没有动画或绘制成功信用。

HorizontalScrollView.onLayout 执行原 ROM layoutChildren(forceLeftGravity)：超宽 child 强制左对齐，
首次 RTL 按 scrollRange−mScrollX 初始化并 clamp，后续布局不重复反转；不以纵向换轴近似。
Java View.mScrollX/Y 唯一持有位置；collector 读取 mScrollX/mScrollY，按同 epoch 的 parent screen +
child local − parent scroll 计算 screen/clip。ScrollAndroidViewBy 只允许 owner 线程、已发布且
仍在根内的两轴 Scroll 对象，经 Java scrollBy→scrollTo/clamp 后完整发布新投影，不 measure/layout、
不 requestLayout、不写 child measured/frame。异常保留旧发布快照。
View.onScrollChanged 接缝只接受未系统附着且 accessibility 显式 disabled 的软件路径，
真实置 mBackgroundSizeChanged；ROM scrollTo 保留 parent cache/invalidation 与 guest 覆写。
宿主仅持拖动捕获/slop/delta，拦截时向单一旧目标发 CANCEL。两轴 onInterceptTouchEvent/
onTouchEvent 接缝只在宿主单指分发作用域内开放：前者委托 ROM ViewGroup 父协议，后者
接受有内容的捕获供宿主 delta 路径使用；作用域外/null/多指明确失败，不写 Java 手势镜像。

## JavaTraversal VideoView 测量与 metadata

JavaTraversal 保留有界 VideoView/VideoMediaPlayer Java 播放协议与媒体 native owner，
不迁 ROM VideoView 字段、构造或比例算法。onMeasure 为逐签名受审 adapter，读取 owner
已交付 intrinsic 与虚派 Java padding/suggested minimum，复用 MeasureImageLeaf 的 constrained
结果（原 ClampSize，不携带 TOO_SMALL），真实虚派 setMeasuredDimension；不写几何。
源打开仅保存 decoder metadata；owner 交付同时核验 native 与 Java mGeneration 后更新
intrinsic 输入并 requestLayout。换源、测量与 metadata 交付只接受 UI owner；释放接受任意
guest 执行线程，按代际立即撤下 PCM/decoder，锁外 join decoder。非 owner 释放仅登记
具备 GC root 的 metadata 撤销请求，owner 视频泵每 tick 处理（源表为空也执行），清输入并
requestLayout；已成功重开的 decoder 不受旧请求影响，重开失败仍撤销旧尺寸。owner 释放同步
清理；退出丢弃待办。旧事件不能恢复尺寸。
回调期间不持视频锁；解码/音频工作线程不进 VM、不写输入或发布快照。Java owner tick 完整提交后
才发布新几何，视频合成与 raster/hit 读取同一 PublishedProjection 的 frame/clip。
原播放状态、字幕/网络/MediaController/未处理错误 UI 的明确失败边界保持。

AssembleAndroidContext 在 catalog 注册/VM 建立前解析全部启动类路径 jar，缺 jar 或缺
classes.dex 明确失败；UI 皮肤投影从 ogplay.jar 读取。不存在 guest Legacy 选择或回落。
frontend 三入口共用此装配，显式注入平台无 accessibility 服务事实，生产不设置测试 bool。
宿主 Dialog 必须拥有独立 HostLayout 树；缺失时拒绝，不向 guest 构建树回落。
`setClickable` 按原 setter 虚派 ROM `setFlags(II)V`，仅在真实 CLICKABLE 位改变时标投影
绘制脏；下一 owner 屏障发布输入资格，不写几何、不改当前输入租约。
AbsoluteLayout 及其 LayoutParams 的构造、参数转换、测量与布局执行固定 ROM Java；
其 guest 子类沿同一构造准入，FrameLayout host kind 不决定 guest 几何。WebView 经 Java
setter 设置焦点能力，自有 onMeasure 因缺浏览器后端记账失败，onLayout 继承 ROM。

## Owner 事件事务

JavaTraversal 的 AndroidInputEvent 在 owner 线程冻结共同 presentation，内部命中、焦点、
scroll、编辑及 fallback 借用同一 lease；公开输入重入明确失败。事件内不推进遍历/发布或
Surface，释放后由 session 屏障消费 mutation。捕获保存窗口/root/内容/树代次与 Java 父链
挂载代次；移除 adapter 增代防 detach/reparent ABA，失效发送一次 CANCEL 并吞终止尾事件。
Java 原字段决定可见/启用，几何始终来自租约；临时坐标查询不触测量。

PumpAndroidSurfaceEvents 只在 owner 帧屏障执行 size→created→changed；冻结本轮 holder/
挂载集合，每次 guest 回调后重验窗口/root/holder/挂载与 buffer 尺寸。换根/新增挂载留下一轮，
同尺寸不重发；迟建/迟挂 holder 由下一轮发现，addCallback 只更新列表。View holder 校验
SurfaceView 请求，Window.takeSurface holder 只用真实 managed window buffer，不反查 View。
Rect 身份稳定，销毁后 retire native surface 并清除 producer 像素；完整 WMS/updateWindow 未启用。
Canvas、NDK/EGL 与 MediaPlayer 使用 buffer→View frame 最近邻缩放；MediaPlayer 先在 buffer
内保留比例，VideoView 仍走原 letterbox。呈现快照冻结像素及缩放策略，alpha/ZOrder/输入不变。
Java EGL swap 在 VM 锁内解析句柄，在锁外同步调用 boundary，图形锁释放后恢复 VM 锁。
EGL producer 经显式 hook 投递纯像素，与 surface 退役串行；owner 安全点共同发布像素与几何。
View holder 和视频生产者发布共享不可变帧，快照 lease 保活其像素直到最后一个读者释放。
holder 的 buffer queue 事实由 created 复位、destroyed/退役清除；Canvas post、媒体实际帧及
纯像素发布置位。Java/native 共用 EGL 成功 window swap 通过显式 holder 身份回调上报，
只操作独立同步的 producer 状态，不在 boundary worker 获取 VM 锁。EGL 创建/Canvas lock
不算 queue；未 queue 的 Surface 按 API19 不提供合成层，owner 可发布黑底叠加 Dialog，
已 queue 且共同快照无像素时保留 producer 的底帧权威。

## 窗口与呈现事实

JavaTraversal每个Activity保留真实Window Java根与最后完整投影；后台几何不依赖当前构建树。
共同发布仅在前台完整投影匹配身份/viewport后更新；Activity 主窗口与额外可见 handoff、
应用附加窗口按各自稳定根保存独立分区，后到前合成；输入按快照中的窗口 flags 选择。后台退役使用保留层级清绑定。View焦点由Java拥有，窗口焦点事实由lifecycle拥有，禁止外部投影RequestFocus。
owner收集前刷新开孔/叶像素，发布冻结bitmap缓存版本及视频节点/像素；发布只允许owner，
输入lease内延期。消费者显式共享同一PresentationSnapshot，禁止再读播放器、可变缓存或
反查guest绑定；无效producer像素明确失败。未知窗口/metrics保留最后完整快照。
不扩大 managed 窗口/Surface 支持范围。

JavaTraversal的destroyed回调完成后永久撤销旧managed endpoint的holder有效性；
holder恢复需新Surface，新active代次不能复活旧Surface。旧owner token只保留给release/GC，
不再可作为 managed producer 绑定。

## Managed-window observer 与基础引用

JavaTraversal 每窗口稳定 Java 根拥有一个 ROM ViewTreeObserver，沿 owner-attached
GC 边保活；首次 owner traversal 建立窗口附着身份后，存活子树的真实
mFloatingTreeObserver 才经 ROM merge/kill 转移并清空。首次遍历前 setContentView/addView
只建立 Java 父链；被替换内容保留 floating observer，不参与窗口 global layout。已附着
窗口后续挂载仍即时合并；真正未附着坐标查询继续明确失败。owner global layout 通过该窗口 observer 的 ROM dispatchOnGlobalLayout
一次派发；不按每个 View 重复派发。其他 observer 系统能力仍明确失败。
setSystemUiVisibility adapter 写真实 Java 字段，ROM getter 读取；附着改变调用父
recomputeViewAttributes adapter，managed 平台无系统栏/WMS，不造 callback 或 AttachInfo。
WebView 经 ROM AbsoluteLayout 父构造；保留 WebView 自有 onMeasure 的浏览器后端失败边界。
Surface 新 created 代次撤销旧尺寸去重事实，created 后必派 changed；同代同值仍去重。


DVM-315：JavaTraversal 的 managed 全屏 Window 以稳定 Java 内容根同时提供 decor，
共享窗口 observer 与真实父链坐标。Surface 回调事务以
实际 owner/root/holder/mount/buffer 核验连续性，不以无关 sibling 的 content revision
变化销毁仍存活的 holder；新挂载延后，换根/重挂及 callback owner 替换仍撤销旧派发。
owner 直接调用 ROM observer 和 Surface 事务在 VM execution lock 下捕获与复核。

JavaTraversal 的 `ui.surface.*` 结构化诊断记录 producer 注册、holder 激活/拒绝原因及
Surface 回调的 holder 分型和真实尺寸；轮询决策按对象限流，实际生命周期派发不合并。
诊断不参与准入判断，不改变代次、buffer 或 ADR-0147 失败语义。

JavaTraversal 的 `ui.traversal` 按弱窗口根/原因保存计数，首次输出结构化 root、epoch、
身份及 metrics；后续仅累计，窗口退役/GC/UI 重置清理。覆盖屏障跳过、clean、延期、
回调身份取消及投影提交/异常；不改变失败、延期或旧完整发布协议。

DVM-320：JavaTraversal 的 producer 重评估只读 Java 父链、绑定、可见性、测量尺寸及窗口，
原因或事实变化时输出 `ui.surface.producer_state`，绕过默认 first-and-count；相同事实不刷屏。
进程级原因累计保存 count/最后 epoch/tick，窗口退役不清除；producer 历史只保存值，
不持有 guest 强根，已退役对象仅报告 last_observed。owner 退出边界调用幂等汇总，
输出 `ui.diagnostics.summary/reason/producer`，不执行 guest、不改变选择或失败语义。
按弱根的遍历计数清理协议保持。

DVM-321：JavaTraversal managed-window 的 visible 只由 Activity 生命周期记录拥有；
Window 内容根的创建不改变它。collector 将当前 Activity 的事实显式提交到 synthetic
partition root，子节点不继承构建缓存窗口位。producer 与 View/window holder 消费 owner
完整投影的 IsVisible；逐回调 Java mount/visibility/代次重验仍保留。getWindowVisibility
读取所属 managed Activity 事实；诊断包含完整投影祖先窗口位，未附着初始化不取得 Surface。

纯窗口可见变化可由 owner 重发最后完整投影的窗口事实，更新调度 published 引用而不
消费 Java requestLayout。隐藏窗口的 Surface 泵仅退役 holder，不为销毁回调触发遍历。

## 进程内 managed-window token

JavaTraversal 的 getWindowToken/getApplicationWindowToken 沿实际 Java 父链查 owner-attached
窗口根；首次遍历前或脱离后返回 null。每存活窗口持有稳定 token，内容替换与调度刷新不
改变身份；窗口退役/GC/UI reset 撤销有效性，新窗口不能复活旧 token。窗口根通过
owner-attached GC 边保活 token，token 仅弱记根身份。isBinderAlive/pingBinder 查询本地有效性；
transact、Binder interface/death/dump 操作及 Parcel marshalling 均记账失败。
JavaTraversal showSoftInput/hideSoftInputFromWindow 与 restartInput 使用真实窗口/附着
焦点检查；无被服务候选返回 false，需要建立 InputConnection 时记账并明确失败。
不创建 InputConnection 或 IME Binder。

DVM-328：TextView 构造按同 ROM getResourceId 消费选择手柄、光标和建议项布局，
只将真实 id 保存至原版五个 Java int 字段；读取不加载内容、不取得 Editor/绘制信用。

DVM-329：owner 屏障先检查窗口请求/Java 根 FORCE_LAYOUT、首次附着与窗口输入变化；
干净帧不调用 guest getter/measure/layout，不遍历或比较各节点投影输入。requestLayout
与 invalidate 的既有 ROM/受审接缝登记真实工作；不靠跳帧或投影过期策略调度。
宿主输入消费冻结快照前验证节点存在；构建绑定不能证明节点属于所持快照，guest 失败
边界及 UiTree 严格 stale 检查保持。


窗口焦点的 dispatchWindowFocusChanged 在 View/ViewGroup 执行固定ROM：View虚派
onWindowFocusChanged，ViewGroup按真实mChildren/mChildrenCount递归虚派。生命周期仅从窗口
Java根进入，window_has_focus/window_focus_activity仍是既有宿主窗口事实，不建立逐View状态。


DVM-335：每个受管 Activity 拥有稳定 WindowManagerImpl，getWindowManager 与
getSystemService("window") 同实例，getDefaultDisplay 仍共用进程 Display；普通 Context 的
manager 没有 app token，应用窗口添加抛 BadTokenException。Activity app token 补入缺省
LayoutParams.token，其他 owner/finishing/destroyed token 拒绝；缺省标题使用应用标签/包名。
addView/updateViewLayout 支持 type 1..99、MATCH_PARENT、flags 8/16/32/128/256/1024，保存
format；非默认尺寸/坐标/gravity、权重/边距、其他 type/flags 记账抛 UOE。先校验再虚派
setLayoutParams；重复添加 ISE、未知移除/更新 IAE，remove/Immediate 在 owner 屏障退役。
View 自身是稳定 Java 根，mParent=null，无额外包装 ViewGroup/ViewRootImpl；沿用 ROM
measure/layout 和受审叶像素投影。与 Dialog 共用最多 8 个附加窗口名额。
窗口按添加序处于 owner 主窗口上方，跟随 owner 可见性；快照冻结根、flags、代次、几何，
viewport 不匹配保留上一完整快照。最上层可见且可触窗口接收整次触摸，全尺寸下
NOT_TOUCH_MODAL 不穿透；NOT_TOUCHABLE 跳过。按键只到焦点子窗口根 dispatchKeyEvent，
未消费不回落 Activity；窗口集合更新取消旧手势并吞尾。生命周期独占焦点写入，先失焦
后获焦；View.hasWindowFocus 沿真实 Java 父链读取所属窗口事实。
owner onDestroy/onDetachedFromWindow 后 closeAll 并结构化 warn；进程关闭只释放附加记录，
不合成子树附着/分离回调。View AttachInfo/dispatchAttachedToWindow、Dialog Callback、
窗口过渡仍不支持；属性动画边界见 DVM-337。


DVM-336：Choreographer 与 DisplayEventReceiver 普通 Java 均取固定 API19 ROM；单例、
callback 队列/延时/移除、doFrame 及 input→animation→traversal 回调排序归 Java。
DisplayEventReceiver 仅 nativeInit(receiver,MessageQueue)I/nativeDispose(I)V/nativeScheduleVsync(I)V
为平台接缝：登记弱 receiver 的不复用 int token、owner trace 队列/Looper；队列无效/已退出
RuntimeException，必须在队列所属 Looper 线程注册，跨线程队列记账 UOE；非主 Looper 可用。重复 schedule 合并，一次请求只消费一帧；
dispose 幂等，原 CloseGuard/finalize 与 GC sweep 同样退役，排队后失效不交付。
实际 lifecycle StepFrame 入口在所有 guest owner traversal/projection 前消费到期消息、
冻结挂起请求并向接收者所属 Looper 投递 dispatchVsync，再泵消息交付 ROM doFrame；后续布局/
输入屏障、Start/Resume/只读呈现不产生脉冲，回调中再 post 只到下一帧。
timestampNanos=统一 Clock uptime×1e6，与 System.nanoTime 同基，displayId=0，帧计数单调；
停止即清登记/禁派发，无 hotplug、无 debug.choreographer 属性注入或 vsync=false 回退。
Application 回调前已准备主 Looper。Message.obtain(Handler,Runnable) 保存原 callback，
Handler 队首先入后出、普通同 deadline 先入先出，Message.obtain(Message) 不复制异步标记。
ROM DisplayManagerGlobal 经本地 IDisplayManager 查询唯一逻辑显示，ROM Display 使用同源尺寸、
density 与刷新率（默认 60）；未知 ID 返回 null。服务接受 ROM 内部回调注册，固定显示无事件。
WindowManagerImpl 与过渡期 DisplayManager 缓存共用默认 Display。Wi-Fi 与虚拟显示入口记账失败。
属性动画通过 DVM-337 另行准入。

DVM-337：ValueAnimator（AnimationHandler）/ObjectAnimator/PropertyValuesHolder 及原版数值
Evaluator 沿 API19 ROM Java 算法执行；动画只消费上述主 Looper Choreographer CALLBACK_ANIMATION，
不增加计时器，停止/owner 退役不额外合成动画 cancel/end。PVH 四个 native 精确绑定，按实例
方法签名（含继承）生成进程内稳定非零 int ID，无宿主指针；缺方法 NoSuchMethodError 供 ROM
反射回退，调用重验 ID/类型并虚派，原 Java 异常传播。无效 ID/类型 IAE。
View setAlpha/getAlpha/ensureTransformationInfo/onSetAlpha/hasOverlappingRendering 取 ROM，
mTransformationInfo.mAlpha 是唯一事实，PFLAG_ALPHA_SET 尊重子类自绘；mDisplayList 恒 null，
软件 invalidate 仅 draw dirty，无 measure/layout。非 alpha 几何变换按 UI 降级忽略并记警告（投影保持未变换几何）。投影节点保存自身
与祖先有效 alpha 的乘积，绘制按命令相乘；未实现离屏层，存在“重叠子树的组不透明度差异”。
软件光栅平面是预乘 RGBA，呈现合成不得二次乘 alpha；应用子窗口与 holder 共用 API19 PixelFormat.formatHasAlpha 判定
（含 TRANSLUCENT/TRANSPARENT 及已知含 alpha 具体格式），按像素 alpha source-over，
下层贡献保留；不透明格式按平面覆盖下层（未绘区域为黑）。
不开放 LayoutTransition、ViewGroup transition、View.postOnAnimation、ViewPropertyAnimator 或
AnimatorInflater XML；硬件 display list、非主 Looper 动画帧与跨平台行为未验。

属性动画所需 Trace.isTagEnabled 仅接受 View tag=8 并返回已连接；asyncTraceBegin/End
将名字和 cookie 写结构化 debug 日志，其他 tag 继续记账拒绝。软件 dirty 请求按稳定窗口根
登记，主窗口发布不能吞掉附加窗口的绘制请求；重绘完成后清理，不因此请求重新测量。

ROM 基线只保留 AOSP 4.4.4 声明；CM 的 Typeface 缓存重建/清理、MediaPlayer suspend/resume
及 FileUtils 卷 UUID 扩展不提供，不登记额外成功入口；查询/调用遵循缺失成员的明确失败。

DVM-344：会话主树保存注入的 API19 Roboto-Regular → DroidSansFallback 不可变链，
CreateHostUiTree 从同一来源建立 Dialog HostLayout；投影和模态复制保持资源身份。
TextView 构造/setText 与 EditText 实际输入复用 UI 的 RequireTextGlyph 覆盖判定，
各接缝保留原记账/Java 异常转换；宿主 raster 的缺字也报告码点。


DVM-346：宿主 modal 的可见性只由 Dialog 与 owner 的状态推导，HostLayout 根始终可见；
快照筛选、输入、焦点与捕获清理共用该推导。冻结树复用必须匹配 partition、revision 与
UiTree 修改计数。计数单调递增、复制保留、溢出明确失败；可变 Get 是 owner 修改入口，
在交出属性指针前登记修改，指针不得跨发布保留；只读消费使用 const Get。
结构、属性、布局结果、焦点及窗口事实的修改均进入计数，dirty 消费本身不改变呈现结果。


SurfaceView 的 holder 默认请求 API19 RGB_565（不透明）；
默认格式沿 Surface 重建保留，不因帧缓冲 alpha 改变图层不透明性。显式格式请求仍可覆盖默认值。
SurfaceHolder.setFormat 保存逐 holder 请求，created/changed 应用到 active 格式并交付 changed；
同尺寸格式改变也交付，销毁不丢请求，holder 退役清除。支持 OPAQUE(-1)、TRANSLUCENT(-3)、
TRANSPARENT(-2) 与 RGBA_8888/RGBX_8888/RGB_888/RGB_565/RGBA_5551/RGBA_4444；
其他请求记账抛 IAE。软件 Canvas 使用 RGBA8888、RGBX8888、RGB565 或 RGBA4444 栅格存储。
RGB888 与 RGBA5551 缺少 Skia raster device，软件锁定记账并抛 UnsupportedOperationException。
完整 dirty 区内容按 Surface 协议未定义，此实现取确定初值：透明格式0、不透明格式黑。
格式 alpha 判定与 application window 共用 PixelFormat 规则；共同快照冻结 alpha 属性，
透明 holder 按非预乘像素 alpha source-over 合成，不透明强制 alpha=255，保留 View alpha 与层序。
不改变 holder queue 事实、视频解码或 SurfaceView 开孔规则。


Spinner 的 DIALOG 构造、AdapterView/AbsSpinner 状态、ArrayAdapter 布局与选择通知归固定 ROM。
子项通过 Java addViewInLayout/removeAllViewsInLayout 进入原父链，由 JavaTraversal 完整投影；
选择通知复用 View.post/主 Looper，不建立宿主选择状态。弹窗点击、dropdown 构造、触摸选择
与 guest draw 保持记账失败。ViewGroup 附着父协议归 ROM；View 附着接缝仅接受无系统
AttachInfo/IME焦点/无障碍服务、无动画/硬件且背景为静态 ROM Drawable 的软件路径，保留原 flags
清理与 scrollbar 唤醒，不制造 AttachInfo 或全系统附着信用。
系统及应用 getLayout 均通过真实 Resources/XmlBlock.Parser 加载；缺资源仍抛 NotFoundException。TextView 保存 MARQUEE 解析状态，实际动画不启动并记录降级；textAlignment=inherit
沿 View 原属性与解析流程，不授予其他对齐/RTL/Editor/Canvas 支持。


ProgressBar 构造、indeterminate/progress/max 字段、异步刷新、叶测量与生命周期归固定 ROM；
非 UI 线程刷新复用 View.post 与主 Looper，不设宿主状态副本。Drawable XML、嵌套标签、
状态、level、测量及帧集合由 ROM 经真实资源读取，不按封存名单准入。AnimatedRotateDrawable
及 AnimationDrawable 的动画调度仍受现有 UI 边界限制；ProgressBar 投影只发布叶几何，内容
按 View 身份一次记账不绘制。DrawableContainer 的查询/回调只读其 Java 字段，不保留皮肤侧表。
Shape 尺寸与 ShaderFactory 执行 ROM，Paint 原生效果用于软件 Canvas。
ImageView 投影仍拒绝非默认 bitmap alpha/filter/mirroring。
View 分离清理执行 ROM，removeCallbacks 只取消既有主 Handler 与当前 guest 线程待转移 action，null 不取消其他任务；
Drawable stop 的 unschedule 回调不创建帧任务。BitmapDrawable 原 BitmapState/Paint 保存
filter/anti-alias 状态；投影遇到非默认 Paint flags、shader/xfermode 明确失败，不吞掉效果。
系统及应用 getAnimation 使用真实 XML；资源加载成功不授予动画调度或 guest draw 能力。
WebView initial scale 保存 API19 原始 int 请求，WebSettings wide viewport 按实例往返；
前进查询 false/操作无历史变更，均检查线程与存活；附着转发 ROM ViewGroup 父协议。
这些配置不授予页面加载、历史、缩放渲染或系统 AttachInfo 支持。
