# 子模块：runtime/integration/dexvm_android

## 职责

提供 dex_activity 生命周期使用的 `android.*` 与 `javax.microedition.*` 平台
intrinsic。`catalog.cpp` 是唯一注册聚合点；平台类按 API 家族聚合到一个源文件，
每个类仍导出独立的 `Declare_<类名>(context)`，返回已经直接持有 handler 的
不可变声明。Java handle 家族聚合文件用于控制翻译单元数量，不受项目通常的
800 行源文件上限约束。

声明与实现同址是唯一 handler 形态。`shared.h` 只暴露跨类共享 helper 与工厂；
跨类复用的 handler 以 `shared.cpp` 中的工厂函数（如 `ViewInitHandler(context)`、
`PrefsEditHandler(context)`）形式提供，捕获会话状态的工厂显式接收 context。
资源、VFS、音频、视频、widget、线程与设备事实全部来自显式传入的
`DexVmAndroidContext`，不得读取游戏身份或另建宿主状态。
通用 `java.io` handle 与流状态属于 DexVM core；integration 装配只通过
`DexVmIoVfsAdapter` 向 `IoRuntime` 注入窄文件接口。`java.util.zip` handle 与 archive
状态同样属于 DexVM core 的 `ZipRuntime`；不得恢复 context stream/output/zip map。
`android.os` 的 Handler/Looper/HandlerThread/CountDownTimer/AsyncTask 与 core Timer 只经
会话唯一 scheduler 排队：deadline 来自 `uptime_millis`，同 deadline 按 sequence FIFO。
主 Looper 只在 lifecycle 安全点泵送，子 Looper 只在 `VmThreadRuntime` guest 线程执行；
禁止同步调用伪装 post、读取宿主墙钟或为 Timer 建第二套队列。
API 19 `ResultReceiver` 仅闭合本地分支：Handler 非空时以内部 Runnable 投递同一 scheduler，
为空时同步虚派 `onReceiveResult`；receiver/Handler/Bundle 均由普通对象字段保持 GC 可见。
依赖 `IResultReceiver` 的 Binder/跨进程 Parcel transport 明确拒绝，不得返回伪成功。

DVM-88 的 ContentValues/Cursor/SQLiteDatabase/SQLiteOpenHelper 状态属于本 context 的
database side-table；guest 引用经具名 GC state table trace，死亡 owner sweep。数据库文件
使用确定性内部格式且只经注入 VFS 访问 `/data/data/<package>/databases/`，不得暴露宿主路径、
调用宿主 SQLite 或扩展成 ContentProvider/Binder。SQLiteHelper 的 schema version 随内部格式
持久化，首次创建与版本增长分别虚派 guest `onCreate`/`onUpgrade`；只把 `Stat` ENOENT 视作
新库，其余 VFS 错误明确失败。未登记 SQL/selection 明确失败。Bundle/SparseArray/Parcel 的
side-table object reference 必须作为 owner 的 GC 强边 trace。

javax EGL/GL façade 遵循 DVM-31：`android_gl.cpp` 聚合该
家族的 Java handle 声明、handler、唯一
display/config/window-surface/context/currency 状态机和 swap pacer，把 guest
`eglMakeCurrent`/`eglSwapBuffers`/`glGetString` 窄接到 session 已拥有的 managed
ANGLE surface；它不创建、替换或终止第二套 EGL surface。
DVM-83 在同一 family TU 发布 API 19 `GLES10/10Ext/11/11Ext/20/GLUtils/GLU`；surface
由固定 AOSP 源生成，Java 参数只经共享 `NioRuntime` 编组后调用 sealed native GLES
binding。`GLUtils` 读取 context 中既有 Bitmap backing；本层不拥有 GL object state。

## 不变量

- 每个类描述符只能由一个 `Declare_*()` 发布；同一 API 家族的多个
  `Declare_*()` 必须同址于一个聚合 cpp，`catalog.cpp` 不包含行为。
- 单类专用 handler 必须直接定义在对应 `Declare_*()`；禁止恢复按域填充函数
  或全量 handler 容器。
- catalog 返回的每个非抽象方法都直接持有 implementation；字符串 handler id 只
  允许存在于 DVM-37 将删除的兼容实现中，不能进入最终声明。
- 新类必须进入所属 API 家族的聚合文件；禁止新增单类翻译单元、misc、按 title
  或按厂商聚合的实现文件。
- 中性占位只能通过 `NeutralHandler(shorty)` 或 `PlaceholderString()` 显式生成；
  引用返回值不能擅自伪造对象。
- Android 平台 handler 的会话状态只从 `DexVmAndroidContext` 进入；core Java handler
  不得依赖该 context，迁移行为必须保持一致。
- `AndroidCoreIntrinsicServices(context)` 是 core 所需 Locale/Timer/SSL/SAX 平台事实的
  唯一适配入口；Android catalog 不得重新发布这些 Java family。
- DVM-77 的 PackageManager 只发布当前 APK：`getApplicationInfo/getPackageInfo` 消费
  sealed Manifest 事实，`getApplicationLabel` 解析 literal/resource label，
  `checkPermission/hasSystemFeature` 只读取显式 granted/feature 集合。未知包、未知 flags
  与跨包查询不得扩展成安装包数据库或 Binder 服务。
- process `Application`、attached base Context 与 descriptor 由 context 持有；
  `Context.getApplicationContext` 和 `Activity.getApplication` 必须返回同一稳定 Java root。
  API 19 类型链固定为 Context→ContextWrapper→Application/Service/ContextThemeWrapper→
  Activity，IntentService 继承 Service；wrapper 只保存一个 guest base 引用，生命周期在
  `onCreate` 前完成一次性 attach，现有 Context 方法必须虚派委托 base，不复制资源或
  service 状态。ContextThemeWrapper 只承诺有界 theme id，不引入 Instrumentation、
  ActivityManager、service process 或完整 framework。
- `Context.getPackageResourcePath()` 遵循 API 19 同进程 `LoadedApk.getResDir()` 语义，返回
  `/data/app/<package>-1.apk` guest 路径；process 装配必须把 context 已持有的原始 APK bytes
  只读发布到该路径。ContextWrapper 只委托 base，禁止返回 frontend 宿主路径或把 `/apk`
  资源目录冒充 APK 文件。
- `AudioTrack.getNativeOutputSampleRate()` 返回 session 桌面输出的 48 kHz 事实；min/max
  volume、position notification period 与 listener 引用属于 track side-table，listener
  作为 GC root trace，不伪造当前尚无来源的 marker/periodic callback。
- `SurfaceHolder$Impl.lockCanvas` 返回 holder 稳定 Canvas；仅 locked Canvas 可绘制，
  unlockCanvasAndPost 发布软件帧并结束锁定。Bitmap/Canvas 均使用同一 ARGB word 语义。
- `System.load/System.loadLibrary` 只经 context 注入的 process
  `NativeLibraryLoader`，并始终携带稳定 application ClassLoader token；null 参数抛
  `NullPointerException`，resolver/linker/JNI_OnLoad 失败映射为
  `UnsatisfiedLinkError`，不得恢复 preloaded/no-op 成功。
- 每个 live guest View object 与一个 live UiNode 一一绑定；hierarchy/id/visibility/
  geometry 只写 `runtime/ui`，guest click/touch listener 只在本层以 UiNodeId 为 key 保存。
- API 19 `View$OnFocusChangeListener` 作为 public abstract interface 发布唯一
  `onFocusChange(View, boolean)` 方法签名，使 DEX `implements` 与 `invoke-interface` 走正常
  linker/virtual dispatch；没有具体焦点事件来源时不得伪造回调。
- `KeyEvent(action, keyCode)` 保存 guest 可见 action/keyCode/repeatCount；生命周期键输入必须
  传递该非空对象。`View.dispatchKeyEvent` 对 DOWN/UP/MULTIPLE 调用接收者实际 override；
  OnKeyListener、DispatcherState tracking/long-press 和字符映射未实现时必须保持显式缺口。
- drawable decode 的 intrinsic size 写入 UiNode；click adapter 只消费 `screen_frame`。
  旧 fullscreen/edge-row bounds 推导及 `LayoutViewFact/layout_views` 类型与存储均不存在。
- Java 动态 `ViewGroup.addView/removeView/removeViews/updateViewLayout` 与
  `Activity.setContentView(View)` 直接维护同一 UiTree；LayoutParams object 保存
  width/height/margin/weight 并在 attach/update 时复制到 node，View geometry getter 在 dirty
  traversal 后读取同一 resolved frame。已带非 content parent 的 view 明确拒绝。
- `TextView.setText/getText` 与 Editable mutation 共用 UiNode text；textColor/textSize/gravity
  mutation 分别推进 draw/layout dirty。当前 fixed-font backend 只接受单行受支持字形，
  多行、未知字形或非法 size 明确抛 Java 异常且不发布部分 mutation。
- RelativeLayout XML structural attrs 与 Java LayoutParams `addRule` 写入同一 typed rule；
  attached params mutation 会推进 UiTree layout dirty。只覆盖 Android 4.4 常用 0..15 rule
  中除 baseline 外的 parent/sibling 子集，baseline、RTL rule 与非法 anchor 明确失败。
- drawable resource 按 id 解码一次并缓存为 RGBA `UiBitmap`，renderer 不读取 APK、arsc
  或 guest object。
- pointer dispatch 在 dirty 时先 traversal，按 clipped reverse draw order 选择 topmost
  enabled/visible listener node；OnTouchListener 与 click listener 均以 UiNodeId 调 guest。
  gesture dispatch 分别返回 cumulative touch consumption、click eligibility 与 Activity
  fallback；touch-only false 不得尝试不存在的 click listener。
  `findViewById/getId/setId` 必须经双向 binding 返回/修改同一 object/node identity；
  subclass 不得重复声明 final 的 `View.setId/setPadding`，直接继承同一 handler；
  content generation reset 同时清空两向 binding 与 listener，旧 node 不得继续可见。
- `UiWidgetRegistry` 是 XML tag → dex descriptor/UiClass 唯一目录；inflater 只解释 generic
  typed attrs，`<merge>` 只允许作为唯一 document root 且不创建 object/node。未知 tag、
  非法 parent/root、未知 `layout_*` structural attr 必须记账或明确失败，禁止跳过并
  re-parent children 后声称成功。
- `<include>` 在 inflation 前按 layout resource 递归展开，具体 root 支持 id/visibility/
  layout params override，merge root 直接接父级；depth 16、cycle、非法 child/override 明确失败，
  include 与 inline 最终进入相同 inflater。
- UI resource resolver 复用唯一 ArscTable/APK reader，最多 16 层解析 reference，并支持默认
  配置的 ASCII string、color、px/dp/sp dimension、bitmap/color drawable；session density 未知
  时显式使用 1.0 fallback。complex style bag、无命中 selector、非默认 qualifier 明确不承诺。
- ImageView XML/Java resource 与 scaleType setter 共用 UiNode/image cache；CENTER、CENTER_INSIDE、
  FIT_CENTER、FIT_XY、CENTER_CROP 已闭合，matrix/fitStart/fitEnd 明确失败。
- 动态 BroadcastReceiver 注册按发起调用的 Context 实例拥有；null receiver 只查询
  sticky broadcast，未注册、重复或跨 Context 注销抛 `IllegalArgumentException`。
  当前平台没有广播来源，因此不伪造 `onReceive` 派发。
- Intent extra 当前支持的 String/Int 类型共享逻辑 key 空间；`removeExtra` 从全部
  类型分表删除该 key，空表随即释放，不存在的 key 无操作。
- VideoView error listener 按 view 实例注册、替换或清除；没有具体异步错误事件时
  不伪造 `onError` 回调。pause/seek capability 只反映已打开 player；缺失 player
  的 completion 延迟到视频 pump，禁止从 `start()` 重入 guest。
- Activity 替换会关闭旧 SurfaceHolder generation 并清除其 holder/callback；新
  generation 注册完成后接收仍存活 managed surface 的 created/changed，禁止跨
  Activity 累积 callback。
- API19 Window policy 保留稳定 `WindowManager.LayoutParams` identity；flags 按
  mask 修改，soft-input mode/type 写入同一 guest-visible record。Activity requested
  orientation 按 receiver identity 隔离且默认为 -1；宿主无 Binder/IME/旋转系统时
  不得因为保存该状态而宣称宿主策略已生效。
- `Display.getMetrics/getRealMetrics` 写入 caller-owned API19 `DisplayMetrics`；尺寸
  来自唯一 managed surface，density 来自 context 注入，不读宿主私有显示器
  信息。无 system decor/兼容缩放时 app/real/noncompat 事实相同。
- EGL 规范内失败走 false/EGL_NO_* + last-error；单 surface、单 currency 模型被
  破坏时必须记账并抛出，未知入口同样记账明确失败。
- guest swap 发布帧后进入条件帧屏障：driver 可运行时等下一次 lifecycle generation；
  driver 经通用执行锁 observer 进入 guest 阻塞时立即放行；shutdown 同样唤醒。
  observer 按注册的 driver 宿主线程 id 过滤，GLThread 自己释放执行锁不改变状态。
  禁止退化为 publish+yield、墙钟超时或单次免停泊额度。
- Java GLES direct Buffer 必须传当前 position 对应的原 guest address；heap/view/array
  只能使用调用期 guest 临时内存，输出 copy-back 不移动 cursor。任何 Java 签名无法确定
  映射到 native catalog 时必须记账并明确失败，禁止 neutral return。
- scheduler side-table 持有的引用必须由 state-table trace 或 session scheduled-root 枚举；
  GC sweep 删除 owner 关联状态，session teardown 先 shutdown scheduler、唤醒 Looper，再
  join guest 线程。所有时钟推进必须调用 `AdvanceAndroidClock` 通知 waiter。
- DVM-86 的 SparseArray、Path、Parcel 与 WakeLock 状态只存在于具名 intrinsic state table；
  Parcel 只传输受检 typed atom，Bundle 在写入时快照 typed map。Power/Vibrator/Process 不得
  调用 Binder、宿主设备或外部进程；未知 transport/type/action 必须明确失败。

## 依赖

本目录属于 runtime/integration，可依赖 dexvm、loader、framework、VFS、音视频等
下层模块；任何下层模块不得反向依赖本目录。

## 测试

`tests/session/profile_entry_scope_tests.cpp` 锁定 catalog 唯一性、直接绑定及
Activity/Intent/Bundle/TextView 方法集合；`tests/dexvm/file_vfs_tests.cpp`、
`videoview_tests.cpp`、`widget_click_tests.cpp` 锁定主要集成行为。
`tests/dexvm/scheduler_tests.cpp` 锁定固定 Clock、FIFO/cancel、Timer/CountDownTimer、
HandlerThread 与 AsyncTask 的线程/回投语义。
`tests/dexvm/android_value_tests.cpp` 锁定 Base64/Sparse、graphics value/Path、
Parcel/Bundle snapshot 与有界 power/vibrator 状态。
