# 子模块：runtime/integration/dexvm_android

## 职责与装配

为 dex_activity 提供受限 `android.*`/`javax.microedition.*` intrinsic，并把 session 已有资源、
VFS、UI、ANGLE、媒体、线程与平台事实注入 DexVM。它是兼容边界，不是 Android 系统：不得引入
跨进程 Binder/system_server、安装包数据库、Play 服务或 title/厂商分支。

`catalog.cpp` 是唯一注册聚合点；每类唯一 `Declare_<类名>(context)`，shape/handler 在所属 API
family TU 同址。`shared.*` 只放跨类 helper/factory；禁止静态自注册、转发命名空间、字符串
handler id、单类 TU 或 misc 巨石。非 Android family 归 core，平台事实只经
`DexVmAndroidContext`/`AndroidCoreIntrinsicServices` 注入。

## 全局不变量

- 依赖只向下；资源、路径、线程、设备与会话身份来自显式 context，不读取 host 环境或游戏身份。
- classpath resource provider 只读取 context 中已解析并封存的 BootDex/APK archive；bootstrap
  不见 APK，application 按 parent-first 查询，资源字节不映射到宿主文件系统。
- 普通 Java 状态优先放 BootDex 字段/数组；host state 必须 owner-attached、具名 trace/sweep，clone
  policy 明确。session root、对象 owner、UiNodeId 与 native token 不得混用。
- guest 引用使用强类型包装；字段经 bound token，禁止裸 slot。flags 来自 `access_flags.h`；
  override 显式声明，不复制继承成员。
- 未实现必须记账并抛可捕获 Java 异常；禁止伪成功。日志使用结构化 logger，生命周期事件不得
  限流到丢失代际事实。
- 时间只经统一 Clock；一个 guest 线程对应一个 host 线程并共享 VM 锁。图形只走 ANGLE，窗口/
  输入只走 SDL3。

## 平台边界

`android.util.Log` 的 d/e/w Throwable 重载通过 guest `printStackTrace(PrintWriter)` 保留
Java 异常文本，再进入统一结构化 logger；不吞异常、不写裸 stdout/stderr。

### Context、Intent、PackageManager

- Context→ContextWrapper→Application/Service/ContextThemeWrapper→Activity 类型链固定。process
  Application、base Context、ClassLoader 与 descriptor 身份稳定；wrapper 只虚派委托 base。
- Intent/Activity 的 component/intent 是普通字段唯一事实。显式同包启动受检；隐式启动仅在
  sealed 当前 APK 内解析 action/category、无 data/type 且含 DEFAULT 的唯一 enabled Activity
  filter。零匹配抛 ActivityNotFoundException，多匹配或 data/type 潜在匹配明确失败；跨包、
  一般 resolver、chooser、Instrumentation/ActivityManager 不支持。activity-alias 保留组件身份，
  实例化其 target Activity。
- session 只创建顶层 Activity，因此 `Activity.isChild()` 返回 false；ActivityGroup/嵌入式
  child Activity 不在兼容边界。
- PackageManager 只发布当前 APK：manifest/path/label/permission/feature 来自 sealed facts；未知包、
  flags、跨包查询失败。DVM-142：PackageItemInfo/ApplicationInfo、Component/Activity/Service/
  Provider/ResolveInfo、PathPermission/PatternMatcher/Printer 及内部类归 BootDex，删除前两者 intrinsic；
  integration 只写受检字段。Application `meta-data` 按 API 19 区分 `android:value` 与
  `android:resource`：前者解析 ARSC typed value 后写入对应 Bundle 类型，后者保留 resource id。
- `Context.checkPermission(String,int,int)` 只回答 guest self PID/UID，并与 PackageManager
  共用 Manifest granted-permission 集合；外部身份和未授予权限返回 denied，null permission
  抛 IllegalArgumentException。无跨进程 Binder 时 `checkCallingPermission` 按 API 19 防泄漏
  语义始终 denied，`checkCallingOrSelfPermission` 检查 self；三种 enforce 复用同一判定并在
  拒绝时抛 SecurityException。ContextWrapper 全部委托 base；不建立 UID 数据库、Binder caller
  或运行时授权系统。
- `resolveService` 只接受非空 action、无 component/data/type/categories、flags=0，并查询当前 APK
  inventory；确定无候选返回 null，未知条件/潜在匹配记账抛 UOE。仍不物化正匹配 ResolveInfo、
  本地服务生命周期或引入外部目录/跨进程 Binder。DVM-143 保证定向反射不解析无关签名。
- bindService 复用同一缺席判定：未知 inventory/潜在匹配明确失败。API19 连接登记早于绑定
  结果，false/失败后仍可解绑一次；ContextWrapper 委托 base，连接是 Context 的 GC 强边。
- code/resource path 指向同一只读 `/data/app/<package>-1.apk`；cache/files 只在 app VFS。
  getFileStreamPath 与 openFileInput/Output 共用单文件名校验和 files 路径；前者返回 BootDex
  File 且不创建目标，后两者按 MODE_PRIVATE 覆盖、MODE_APPEND 追加。
- `getObbDir(s)` 按 API 19 返回 primary external 下的
  `/Android/obb/<package>`，经 VFS overlay 建目录；ContextWrapper 只委托 base。
- Settings.System/Secure/Global 的公开 API、类型转换和 moved-key 路由来自 API 19 BootDex；
  integration 只 overlay 私有 NameValueCache 存储边界。Secure 读取稳定沙盒身份，System 使用
  隔离进程内表；Secure/Global 特权写入记账并返回 false。不实现 Binder SettingsProvider、
  跨用户、观察者或宿主设置。SystemProperties 只实现受审 native 边界。

### 资源、Parcel、数据库

- AssetManager/Resources 只读 APK/ARSC/AXML；open 返回 core ByteArrayInputStream，openFd 仅接受
  STORED entry 并发布逻辑 FD+区间。缺失映射为 Java IOException/NotFoundException，不泄漏路径。
- `Resources.getConfiguration()`的稳定对象以同一 VM `Locale.getDefault()`补齐 locale，并调用
  BootDex `Configuration.setLayoutDirection`。当前 TextUtils 只确认 ROOT/en/zh 为 LTR；其他
  locale 在 ICU likely-subtags 边界补齐前记账失败，不伪造方向。
- Parcel 的普通协议由 API 19 BootDex 执行；integration 只提供 VM 隔离的字节 backing、游标、
  Binder 对象记录和生命周期。Binder 记录是 owner GC 强边，marshall 拒绝对象记录；FD 与远程
  Binder 明确不支持。
- Binder 线程策略按 execution context 保存，接口头保留 mask 与 API19 GATHER 位；仅记录，
  不运行 StrictMode 检测。StrictMode 只提供 Parcel 所需的无 violation 查询/清理窄边界，
  violation 编解码明确记账失败；Parcel 异常编码仍执行 BootDex。接口长度先受检再分配。
- SQLite 状态归 context table，文件只经 VFS，使用确定性内部格式。首次创建/版本增长虚派
  onCreate/onUpgrade；只有 ENOENT 表示新库。未登记 SQL/selection 失败；不调用 host SQLite。
- SharedPreferences 按 context/package 持久化 app XML；editor 与 listener 遵循普通对象和具名
  state table 契约。

### UI、窗口、输入、图形

- 每个 live View 对应一个 UiNode；hierarchy/id/visibility/layout/text/style 写唯一 UiTree。
  动态 add/remove/update、findViewById、LayoutParams 与 RelativeLayout 使用同一树；Java 字段修改
  本身不触发 traversal。
- `View.getBackground` 按 API 19 继承形状发布；setBackgroundResource/Drawable 与 getter 保持
  同一 guest Drawable 身份。Button 构造和 XML inflation 建立非空默认背景，Drawable alpha
  通过仍为当前背景的 callback node 触发重绘；替换/清空会解除旧 callback，普通无背景
  View 返回 null。
- `TextView.setText`、`Editable.clear/replace` 与宿主 EditText 按键共用一个 integration
  文本事务；数字/长度过滤、变更区间、watcher 快照、同步回调和失效只有这一份权威实现。
- XML inflation 在应用显式属性前投影 API 19 TextView/Button/EditText 默认文本大小、最小
  尺寸、gravity/enabled/clickable，并解析 framework Large/Medium/Small textAppearance；显式
  `style` 先于 XML 显式属性应用，TextAppearance 基础样式与 ProgressBar horizontal theme attr
  属于已登记投影，未知 style 明确失败；textSize 和属性继续覆盖默认值，UiTree 保持唯一权威状态。
- Activity/DecorView/attached View 的焦点读取 lifecycle 唯一事实。无 Sensor/SystemUI/WMS 时不
  伪造方向、焦点或 system-bar 回调。
- SurfaceView holder 按 attach 与 host surface 形成 generation，严格 created→changed→destroyed；
  detached 子树不提前收事件也不能关闭 host surface。Canvas post 发布软件帧，Bitmap/Canvas
  统一 ARGB。
- pointer 在 dirty 时先 layout，按 clipped reverse-Z/deepest-first 命中并虚派 listener/
  onTouchEvent；键盘将 SDL scancode 转 API 19 keyCode/Unicode/meta/repeat。
- ViewGroup 的 width/height `addView` 重载虚派容器默认 LayoutParams，写入 BootDex 字段后委托
  唯一 index+params attach 路径；RelativeLayout 生成自身参数类型，不在 UiTree 复制参数对象。
- GLES/EGL 通过 session 的 managed 冷入口调用 Native EGL/GLES binding；不创建第二套状态。
  参数错误进入 guest error 锁存，host 内存/生命周期契约故障仍硬失败。Java EGL10 与
  API19 EGL14 的 display/config/context/surface wrapper 只映射 Native EGL registry 句柄；
  config 数组逐 native handle 建立 wrapper；桌面配置为请求颜色格式的超集时，wrapper 可在
  `eglGetConfigAttrib` 投影该次显式请求的颜色位数，但 context/surface 始终使用真实 native handle；
  current identity、错误、延迟销毁、pbuffer 与 shared context 均以 registry 为准。EGL14
  数组 overload 必须校验 offset 并只回写指定切片。pixmap、client buffer 与 texture pbuffer
  等 Native 已明确拒绝的入口保留精确 EGL error。WU-4 已从 AOSP 发布 GLES30 类、常量与
  overload surface；方法按 GLES3 delta catalog 记账，Native handler 未闭合项继续明确失败。

### Looper、线程、回调

GLES30 的 indexed string、sync long、mapped direct Buffer 与 transform-feedback String[]
使用专用返回/参数适配；GLES20 glGetString 接受 SHADING_LANGUAGE_VERSION。
GLSurfaceView 保存逐 View 的 Context version/config 请求；`queueEvent` 保活 Runnable 并由
lifecycle 在 current GL 渲染线程、renderer callback 前按 FIFO 执行，禁止同步伪装或空返回。
`requestRender` 与 WHEN_DIRTY 使用逐 View 单次消费请求，事件执行不依赖绘帧。
GLU 的 error string、look-at、ortho、perspective、project/unproject 采用 API19 专用数学与
GL10 虚调用适配，不进入 native GLES symbol 目录；数组 offset 与失败不回写均显式受检。
Java GLES 的 String 返回、active query、uniform block/transform-feedback 名称及 String[]
uniform index 使用专用适配；GLUtils 按四种 API19 Bitmap.Config 编码。EGL Java facade 的
extension string 与错误锁存以 native registry 为唯一事实。

- Handler/Looper/HandlerThread/Timer/AsyncTask 共用 scheduler；deadline 来自 uptime Clock，同
  deadline 按 sequence FIFO。主 Looper 只在 lifecycle safe point 泵送，子 Looper 在对应 guest
  host thread 执行；禁止同步调用伪装 post。Android 设备先于 APK 进程存在，统一 Clock 默认带
  确定性的 60 秒 boot-age；无 suspend 模型时 uptimeMillis 与 elapsedRealtime 共用该时间事实。
- `Activity.runOnUiThread` 仅在 root context 同步虚派 Runnable；guest worker 投递进程唯一主
  Looper。teardown 在 worker join 后、root JNI detach 前释放 guest native token。
- AsyncTask worker 的 `DexVmError` 保留原始线程故障并终止该路径，不转换成 null 结果，
  不继续调用 `onPostExecute`。Dialog/Web 内容尚未实现的 presentation 明确失败并记账。
- ResultReceiver/IResultReceiver 普通协议来自 BootDex：有 Handler 排队、无 Handler 同步虚派，
  Parcel 往返保持本地 Binder 端点身份；不创建远程 Binder scheduler。
- Thread/JNI native 入口复用 core runtime 与同一 catalog/context；不得恢复第二套线程或服务表。

### 媒体、网络、设备、JNI

- MediaPlayer/VideoView 只消费受检资源、路径或逻辑 FD 区间并交给唯一 decoder/mixer；不创建
  host fd/第二播放器。回调只来自真实生命周期/播放进度。
- AudioTrack rate 来自 mixer；stream/static、marker/period、listener、pause/flush/release 使用
  唯一状态；notification marker/period getter 返回同一 setter 状态，默认 0，释放或未初始化
  时抛 IllegalStateException。回压等待完整释放 VM 锁，恢复后复验 owner；host 音频线程不得进入 VM。
- 网络只用 core 注入 policy/transport，默认离线；Connectivity/Wifi 仅发布已配置事实，不读取
  host 网络、DNS、代理或证书。无传感器/电话来源时返回 API 允许的缺席结果，不伪造硬件。
- location 仅发布 API 19 listener/值类型形状与稳定 manager facade；无 provider、无历史位置，
  更新注册/移除明确记账失败，不接入宿主坐标、Binder 服务或产生回调。
- KeyguardManager 是不缓存的 API 19 系统服务 facade；三项只读锁屏查询读取进程级
  `AndroidKeyguardStateProvider` 快照。未注入 provider 时明确表示桌面兼容层没有锁屏，后续
  宿主接入只能替换 provider，不得把 Binder、WindowManagerService 或宿主查询散入 Java handler。
- System.load/loadLibrary 只经 process loader 并携带 application ClassLoader；失败映射 Java 异常，
  禁止 no-op 成功。JNI 对象出口按真实 runtime class 原子幂等注册；数组元素不得猜声明类型。
- JCA `Mac/MacSpi` 与 HmacSHA1 SPI 均来自 API 19 BootDex/Conscrypt；当前 AndroidOpenSSL 仅
  注册已接通算法，计算经 AOSP 签名的 `NativeCrypto` 进入 guest ARM libcrypto，native token
  复用统一 GC/teardown 清理。BKS/KeyStore、BouncyCastle 与 TLS 不在当前边界。
- native token 只存普通 Java long 字段；GC/teardown 经登记 cleanup 清理，不保存 host pointer，
  不因浅 clone 提前释放共享 token。

## BootDex、文件与测试

Bundle、Build、Uri、Configuration、OrientationEventListener、ResultReceiver、布局参数、framework/
PM 值类等普通算法归 BootDex。catalog 只保留 native/平台事实边界，不重复发布 class 或普通方法；
Java 工厂必须先初始化类再调用原版构造器。

实现按 content/os/view/graphics/gl/media/database/device 等既有 family TU 分工；新增类进入既有
family。明确缺口包括跨进程 Binder/system services、外部 package/service resolver、ContentProvider、
完整 framework/UI/传感器、现代支付/社交/反作弊及手机端运行。

定向测试位于 `tests/dexvm/android_*`、widget/layout/scheduler/egl、runtime JNI/native loader 与
frontend lifecycle。行为变更覆盖 switch/threaded 及架构门禁；title 探索不等同 Scenario gate。


DVM-150：shared 的 PlatformRuntimeNativeExitHandler 仅绑定 Runtime.nativeExit，发布会话
退出标记并调用 VM 的不可返回 Exit；不能再把 System.exit 实现为设置标记后返回。
