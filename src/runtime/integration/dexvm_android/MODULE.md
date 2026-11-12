# 子模块：runtime/integration/dexvm_android

## 职责与装配

为 dex_activity 提供 Android native 与进程内服务绑定，将 session 的 VFS、窗口服务、
ANGLE、媒体、线程及平台事实接入 DexVM。禁止 title/厂商分支；不实现完整 Android、
跨进程 Binder/system_server、外部包数据库、Play 或手机系统服务。移动宿主经 HAL 接入（ADR-0086）。

`catalog.cpp` 唯一注册；每类一个声明函数，命名规则同 [intrinsics](../../dexvm/intrinsics/MODULE.md)，shape/handler 在所属
content/os/view/graphics/gl/media/database/device family TU 同址。`shared.*` 仅跨类 helper；
禁止静态自注册、转发命名空间、字符串 handler id、单类 TU/misc 聚合。非 Android family
归 core，平台事实由 `DexVmAndroidContext` / `AndroidCoreIntrinsicServices` 显式注入。

## 共用不变量

- 普通 Java 字段/数组/算法归 BootDex，catalog 仅 native/平台边界，不重复类或普通方法。
  工厂先初始化类再执行原构造器；host state 必须 owner-attached，具备 trace/sweep/clone policy。
  session root、object owner、window ID、native token 不混用，浅 clone 不提前释放共享 token。
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
Activity、PhoneWindow、WindowManagerGlobal 和 ViewRootImpl 执行 ROM。AMS 通过 IApplicationThread
调度启动、暂停、停止、结果、恢复及销毁。当前 APK 的封存 Manifest 提供组件身份、主题和 alias。
同包 Activity 由封存清单解析；task 与实例复用遵循四种 launchMode，支持本地 for-result。
token、Intent 与结果由 AMS 持有至对应确认。进程退出与宿主协作关闭使用独立状态，截止时间由统一
Clock 驱动。跨包解析、跨进程 Binder 及系统任务管理超出支持范围。

运行期固定方向请求在主队列安全点更新单显示的宽高与全局 Configuration。AMS 经
IApplicationThread.scheduleConfigurationChanged 交付进程配置。AMS 保存全局及每个 Activity 上次交付的独立快照，恢复可见前核对自身快照，不能把 Resources 的可变 Configuration 当旧值；可见且未结束的 Activity 按
Configuration.diff 与 ActivityInfo.getRealConfigChanged 判定 relaunch 或 activity configuration
通知。保存状态、销毁、恢复和应用回调归 ROM ActivityThread；资源及 DisplayMetrics 归 ROM
ResourcesManager.applyConfigurationToResourcesLocked，宿主只发布显示事实及新的配置值对象。
密度和用户显示设置只有启动入口，不提供宿主旋转、缩放或运行期密度设置。

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
| getInstalledPackages/getInstalledApplications | 封存当前 APK 安装清单，PackageInfo 与 getPackageInfo 同源，ApplicationInfo 与 getApplicationInfo 同源。通过 ROM ParceledListSlice 返回独立列表。缺清单/身份/guest 路径或未知 flags UOE |
| getInstallerPackageName(String) | 当前直接加载 APK 无安装器记录则 null；未知包（含空串/system/null）IAE，不推测商店 |
| getResourcesForApplication(String) | 当前包共用应用 Resources/AssetManager；未知包（含空串/system）NameNotFoundException，null NPE；无跨包/其他重载 |
| getPackageArchiveInfo | ROM PackageManager/PackageParser 从 guest VFS 中的 APK 解析 Manifest，不复用安装清单；缺路径或损坏归档沿 ROM 返回 null。无包安装或外部包数据库 |
| getApplicationInfo | 当前包，0/GET_META_DATA/GET_UNINSTALLED_PACKAGES（单包清单下不改变结果）；外部包 NameNotFoundException |
| getPackageInfo | GET_ACTIVITIES/META_DATA/PERMISSIONS/UNINSTALLED_PACKAGES 组合（单包清单下 GET_UNINSTALLED_PACKAGES 不改变结果）；activities 仅请求时按声明顺序发布、保留 alias，默认排除禁用项，否则 null |
| getActivityInfo/getReceiverInfo/getServiceInfo | 当前包完整组件名，0/GET_META_DATA/GET_DISABLED_COMPONENTS/GET_UNINSTALLED_PACKAGES（单包清单下不改变结果）；按应用/组件启用状态过滤，缺失 NameNotFoundException，同名 Activity/receiver/service 取最后声明 |
| getPermissionInfo | 仅 Manifest 中 registered 的 `<permission>` 首个有效定义，0/GET_META_DATA；未知 group/重复后项不注册，仅请求/授予但未定义仍 NameNotFoundException |
| queryIntentActivities/resolveIntent | 当前 APK 显式组件及隐式过滤器。ROM IntentFilter 匹配 action/category、MIME、scheme、authority 与路径模式，按优先级、DEFAULT 与匹配分排序；resolveIntent 取首项 |
| queryBroadcastReceivers | 支持显式组件；隐式查询要求非空 action、同一 filter category 子集、无 selector/data/type。flags 为 0 或 MATCH_DEFAULT_ONLY，后者仅保留有 DEFAULT category 的 filter。排除禁用项，同组件首匹配、API19 稳定排序 |
| queryIntentServices/resolveService | 支持显式组件；隐式查询沿用非空 action、无 data/type/categories、0/GET_RESOLVED_FILTER。仅确定无候选时返回空列表或 null；潜在匹配/未知条件失败 |
| bindService/unbindService | 缺席查询沿原规则；主线程 BIND_AUTO_CREATE、component-only、当前 APK 启用的同进程服务；独立进程/isolated 返回 false，仍登记连接且可解绑，无回调（[ADR-0170](../../../../docs/adr/runtime.md#adr-0170)） |

显式查询优先使用 Intent 的 component，其次取 selector 的 component。移除 MATCH_DEFAULT_ONLY 后按对应
getXxxInfo 的 flags 和启用状态查询。命中时返回仅设置 activityInfo 或 serviceInfo 的 ResolveInfo，
缺失时返回空列表（resolveService 返回 null）。外部包、缺 inventory 和未知 flags 记账并失败。
隐式广播查询保留真实 ArrayList/ResolveInfo/ActivityInfo 及 priority/match/isDefault/filter label/icon；
同名声明各自的过滤器与配置独立，跨声明按组件去重，首个匹配提供结果，不与按名查询的后项配置合并。
permission/exported 不限制声明查询。隐式广播查询遇外部包、未解析的候选 data 条件、未知 flags 或
缺 inventory 明确失败。服务查询只读取声明，启动与绑定另按各自的进程边界处理。
独立进程/isolated 服务 start 返回 null、bind 返回 false，每次记账并限流 warn，
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
| Settings/SystemProperties | ROM NameValueCache 通过本地 Settings provider 读取沙盒 Secure 身份和进程内 System/Global 表。System 有效值变更递增版本并由 IContentService 通知观察者。System 写需 WRITE_SETTINGS，Secure/Global 写抛 SecurityException。URI 查询返回已建模表的游标；按表或名称读取，支持投影与简单名称筛选。SystemProperties 从显示和 VM 配置读取密度、常规堆上限和最大堆；无跨用户、同步适配器或宿主设置。 |
| Notification | ROM Notification/NotificationManager 持有字段、构造、extras 与本地 Parcel 值；本地 INotificationManager 按当前包 tag/id 保存通知并支持取消。展示记 UI 降级，跨包访问检查 UID；无系统展示、声音、点击或持久库存 |
| SMS | SmsManager 和 SmsMessage 来自原样的 telephony-common.jar。PDU 由 ROM 解析。本地 ISms 对获准查询返回无 SIM、无 IMS 状态，获准发送请求记账拒绝。无 Phone/RIL、SIM 存储或短信传输 |
| NFC | ROM NfcAdapter 经 IPackageManager 查询 NFC feature，缺席时 NfcManager 捕获 UOE，默认适配器为 null；无监听或通信 |
| PendingIntent/AlarmManager | 当前 APK 单 Activity/service/broadcast 令牌，创建不解析/启动目标；原 Intent 复制/filterEquals/replaceExtras 快照，extras 不参与身份，种类/包/requestCode/ONE_SHOT/fill-in flags 参与，NO_CREATE/CANCEL_CURRENT/UPDATE_CURRENT 控制查找，CANCEL_CURRENT\|NO_CREATE 返回旧已取消令牌。仅普通 Intent/BootDex Uri；selector/content MIME provider/未知 flags/Parcel/克隆失败。VM 锁串行 registry、非永久根，ROM 包装按本地 IIntentSender 的 Binder 身份比较，活令牌追踪 Intent，GC 删除记录；闹钟条目为 session roots。ROM AlarmManager 经本地 IAlarmManager，cancel(null/无匹配) 正常，移除闹钟不取消令牌；令牌 cancel 后不匹配。退出停止创建/清理；注册/调度/send UOE |
| Connectivity/Wifi | core policy/transport 默认离线，仅注入事实，无 host 网络/DNS/代理/证书探测。默认断开的逻辑 WiFi，可声明类型缺席；独立原版 NetworkInfo=DISCONNECTED/不可用、活动网络 null，Wifi 默认开启。NetworkInfo 查询需 ACCESS_NETWORK_STATE，缺事实/online policy UOE，不支持类型按 API19 null；ROM ConnectivityManager 通过本地 IConnectivityManager 查询，服务调用原 NetworkInfo 构造/setter。getConnectionInfo 需 ACCESS_WIFI_STATE，离线返回独立 WifiInfo（networkId=-1、UNINITIALIZED、MAC=null）；初次登记可收到断开状态的粘性广播。Wifi 开关写入会话状态；无在线 provider 或真实无线控制；同一离线拓扑派生只读 lo 内核视图与本地地址 ioctl，不受 transport IO 授权限制 |
| USB/备份 | ROM UsbManager 使用本地 IUsbManager，设备列表为空，配件缺席，打开设备或配件返回 null，权限查询 false。USB 管理权限不授予普通 APK，设备权限 PendingIntent 投递显式失败。ROM BackupManager 保留服务缺席语义，备份请求不排队，恢复请求返回 -1、无恢复会话。 |
| WifiLock/MulticastLock | ROM WifiManager 计数、held 和每 manager 50 锁上限；本地 IWifiManager 校验锁模式及 WAKE_LOCK/CHANGE_WIFI_MULTICAST_STATE 权限，不改变连接、网卡或组播。 |
| Sensor/Telephony/Keyguard | ROM SystemSensorManager 从与 NDK 共用的空设备目录枚举。ROM LegacySensorManager 经 IWindowManager 查询固定 ROTATION_0。ROM KeyguardManager 的三项查询经本地 IWindowManager 读取进程 provider，缺席表示桌面无锁屏。ROM TelephonyManager 经本地 IPhoneSubInfo 读取注入的设备标识、软件版本和号码，无订阅时 subscriberId=null；ITelephony 报告 PHONE_TYPE_NONE。SIM/运营商查询由 ROM 读取进程 SystemProperties；ITelephonyRegistry 强持有已登记回调，按事件掩码推送一次当前状态，注销后移除，listen(null) 由 ROM 捕获 NPE。 |
| Vibrator | ROM SystemVibrator 经本地 IVibratorService 查询硬件缺席；单次请求及取消更新进程事实，模式请求检查权限和 UID，不驱动宿主硬件。 |

### 模拟设备状态

`DexVmAndroidContext::device_state` 保存会话级模拟设备事实，读写由 VM 执行锁串行。本地服务、
`LocalSettingsProvider` 和系统属性从同一状态投影已纳入模型的字段，不另存副本，
也不访问宿主的真实设备服务。API19 的定位设置通过 Secure 读取。

定位设备具有 gps、network 和 passive。Secure 的 provider 允许列表为空，定位模式由 gps/network
的设置推导为 OFF，mock 关闭。LMS 的始终启用规则使 passive 可用，Secure 的允许列表查询仍返回
false。权限和定位精度从当前 APK 的清单权限推导。服务按 Binder 或 PendingIntent 身份登记，每个
provider 保留最新请求；已登记的回调和请求由会话强持有。禁用通知沿 ROM listener transport 派发，
需要通过 PendingIntent 投递的禁用通知显式失败并记账。网络 provider 按 4.4 时期 NLP 建模，依赖网络和基站，无卫星、费用、海拔、速度或方位，功耗低且精度粗略。设备无历史位置、无 geocoder，
不会产生位置、GPS 状态或围栏事件。

电话初始无 SIM、无服务、无可用信号、空闲且数据断开。网络保持离线，Wifi 和移动数据开关
是会话状态。电源记录活跃 WakeLock Binder token，屏幕常亮。音频策略维护 API19 流音量、
铃声模式、焦点栈和路由观察者，实际播放沿已有 AudioTrack/混音链。无障碍与拼写检查关闭，
外部存储沿 VFS 事实，无 OBB，备份与 USB 无可用后端。同步初始关闭，无账户或适配器。
可变的剪贴板、通知库存、回调注册和设备开关均归会话；回调及有效 Binder token 由
会话根保活。进程与任务查询沿 ROM Activity 栈及注入的内存事实。

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
  ROM View/Drawable 执行布局与软件绘制，Bitmap/NinePatch/Paint native backing 提供像素与图形状态。
  ViewRootImpl 的软件 Surface 经 WindowSystem 合成，不保留宿主 UI 树或逐 View 位图快照。

- ROM Bitmap 的 mBuffer 是计入 Java 堆的 byte[]。Skia 直接在借用的宿主跨度读写像素，
  NDK lock 返回同一数组的 guest 地址。Java、Canvas 与 GLUtils 共用这一存储，合成器发布不可变帧。
  RGBA 默认预乘。Canvas 的 Java mBitmap 与 Bitmap.mBuffer 形成强边，native 像素借用随句柄退役。
  VM 锁串行化 Canvas 绘制和 NDK lock/unlock。NDK 持锁期间可能在 VM 锁外写像素，Canvas 拒绝
  读写该 Bitmap（包括图层合成），抛 IllegalStateException。回收后已有 NDK 锁可正常归还。
  最后一次 NDK unlock、Canvas 绘制及图层合成推进像素版本；Canvas 绘制到回收目标抛 RuntimeException。
  不支持 WebP 编码和共享内存 Parcel blob。
- StatFs 字段/构造/restat/getter 归 Java，Posix.statvfs 只查 VFS 沙盒配额卷，其他卷失败、无 host
  空间采样，见 [ADR-0122](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/runtime.md#adr-0122)。SQLite 固定真实引擎，只经 VFS
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

WMS 在单逻辑显示上管理应用窗口、子窗口及 Toast 的 frame、层级、焦点、可见性和输入通道。
同 app token 的 base application 放在既有 Dialog 之下，子窗口沿所属根窗口排序。
本地服务前台通知按 token 和 ComponentName 验证，非零 id 配 null 通知抛 IAE；通知展示调用 RecordUiDegraded。
AMS 结束 Activity 时先隐藏其 app token，再调度 pause、stop 和 destroy。会话退出先隐藏栈中
全部 token，再调度生命周期消息。WMS 仅向仍登记的窗口派发焦点变化。
ROM 自己测量、定位和绘制 View。PopupWindow 的锚点与 gravity 是 WindowSession 的布局输入。
系统 inset 为零，窗口动画缩放为零。多显示、系统装饰和非单位窗口矩阵超出支持范围。
显示宽高变化先取消当前触摸捕获，再更新 WMS 几何并经 IWindow.resized 请求 ROM relayout。
DisplayManager 的本地回调失效缓存后，资源管理器读取新尺寸；新帧尺寸更新前端触摸映射。
重建窗口沿已有 remove/add 与 Surface 退役路径，EGL backing 随 ROM Surface 生命周期重建。
带 RELAYOUT_DEFER_SURFACE_DESTROY 的隐藏保留旧 Surface 有效性，ROM 完成 surfaceDestroyed
和 transferFrom 后，由 performDeferredDestroy 退役；窗口移除同时清理延迟持有的引用。
合成器在同一锁内取得显示矩形及图层快照，像素处理期间不持有窗口锁。

ROM Surface 和 SurfaceControl 的 native 使用分型句柄。Skia 软件 Canvas 保持 backing；首次锁定或尺寸变化
请求全量重画，其余保留 dirty 外像素。每次 post 发布不可变图层，合成器使用 crop、透明区、format
和窗口 alpha；满足条件的全屏不透明图层可直接借用像素。SurfaceView、GLSurfaceView、NativeActivity
与 MediaPlayer 经同一窗口标识发布生产端图层。View 树和输入分发在 ROM 内，合成器仅读图层快照。

WMS 分发触摸 DOWN 并锁定手势目标，按窗口 flags 交付 OUTSIDE，按键交给焦点窗口。InputEventReceiver
消费排队的事件和批次，MotionEvent 与 NDK AInputQueue 共用输入样本。IMMS 使用活跃 IInputContext
提交 SDL 文本与 composing，通知前端开启和关闭文本输入。Toast 经本地 INotificationManager 排队，
调用 ROM ITransientNotification.show/hide，LENGTH_SHORT 为 2000 ms，LENGTH_LONG 为 3500 ms。

硬件加速不可用。依赖硬件的显示功能经 `RecordUiDegraded` 记录。

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

GLSurfaceView 使用 ROM GLThread、EGLContext 和 EGLImpl。Surface 与 SurfaceHolder 解析出对应
窗口端点，EGL window surface 使用现有 ANGLE Context、swap 节流和资源退役。
SurfaceTexture 继续使用 SurfaceStream。底层保留独立的 EGL 错误和 context-loss 语义。
会话每次放行显示帧后，在再次推进 Clock 前等已登记的 Java EGL 窗口生产者完成 swap，
或进入执行锁观察到的真实阻塞、解绑、退役或退出。线程运行时等待状态补齐解除阻塞通知到重新取得 VM 锁之间的窗口，不能将排队取得锁误认为仍在渲染。JNI 渲染与 swap 的图形读回虽释放 VM 锁，仍算生产中；JNI 内的 futex waiter 为明确阻塞。Java 线程退出即退役其生产者；
pacer 自身的等待也不作为普通 guest 阻塞。主线程进入 guest 等待时解除 swap 节流，避免
生命周期等待 GLThread 的反向依赖；队列正常空闲不解除节流。WHEN_DIRTY 的 monitor.wait
允许继续步进。原生 EGL 边界线程沿自身 FramePacer；原生拥有的 JNI 附着线程调用 EGL 时保留原有单向 pacer，均不纳入 Java 线程的双向同步，避免其进程级循环反向依赖 Clock。只有活的 EGL window surface 登记生产者，pbuffer 不参与帧等待。
等待生产者时不持 VM 锁。到达等待预算后，按生产者记录一次等待时长并放行 Clock，
该生产者在下一次 swap 或可观测阻塞后重新参与双向同步。

Handler、Looper、MessageQueue 和 Choreographer 由 ROM 执行。主 Looper 持续派发；宿主推进 Clock，
排队 vsync 和设备请求，并唤醒 MessageQueue native 等待。同步屏障、异步消息和 idle 回调由 ROM 排序。
Java 与 NDK ALooper 共用同一线程的 boundary Looper 与 wake。独立 VM 测试可使用条件变量等待。

## 媒体、Dialog 与 WebView

ROM VideoView 经 MediaPlayer.setDisplay 把有效 Surface 绑定到解码器。MediaPlayer.setSurface 同样
使用 ROM Surface 句柄，播放由统一 Clock 驱动；本地文件经 VFS 打开，发布的视频帧成为窗口图层。
音频通过既有 PCM mixer；字幕、网络源与完整系统媒体服务超出支持范围。
Dialog/AlertDialog/Toast 的内容及回调由原版 ROM 管理。原版 ROM WebView 通过固定工厂接入
[OGPlay guest Java provider](../../../guest/webview/MODULE.md)，由 ROM View 层级测量和布局。
provider 经 `WebEngine` 发起操作，`NoWebEngine` 在 WebView 的 Looper 异步交付页面事件。
网络请求回调 `ERROR_HOST_LOOKUP`，本地内容成功时记录一次 UI 降级并绘制背景。JS 请求只记账。
现有 ROM Log native 接收 provider 的固定诊断事件，转入 `RecordUiDegraded` 或未实现账本。
WebView 状态和进程内 WebKit 服务由 guest Java 持有。

## JNI、诊断与验证边界

load/loadLibrary 经 process loader 携 application ClassLoader，应用调用 loadLibrary 先走 ROM
PathClassLoader.findLibrary 的 VFS 搜索，失败为 Java 异常。soundpool/media_jni
仅平台库身份、不加载 ELF，固定 Conscrypt javacrypto 映射 ogplay_jni、其他名称不改写。JNI 对象按
真实 runtime class 原子幂等注册，数组元素不猜声明类型。Mac/MacSpi/HmacSHA1 SPI 归 BootDex/
Conscrypt，经 AOSP NativeCrypto ABI 调 guest ARM libcrypto、token 共用 GC/teardown；无 BKS/KeyStore/
BouncyCastle/TLS 承诺。

Dashboard 的音频、视频和窗口快照使用 try-lock，繁忙时返回空值。
窗口快照提供窗口数和焦点标识；读取诊断不执行绘制或解码。
能力范围与验证入口由 [capabilities](../../../../capabilities.toml) 维护；未关闭项见 KNOWN-ISSUES，过程留在提交说明中。

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
