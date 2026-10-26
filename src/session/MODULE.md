# 模块：session

## 职责

编排 APK compatibility Profile、VFS、DexVM Activity 生命周期、存档、输入、
统一 Clock 和运行检查点。
Dex activity 每帧在 guest 回调前泵送主 Looper，到帧尾只通过
`AdvanceAndroidClock` 推进 Android 单调时钟；停止时先关闭 scheduler，再停止 guest 线程。
显式实时 VideoView 模式按统一 Clock 的步间墙钟补时，单次总推进上限 100 ms，
扣除该步 Java 阻塞已推进的时间；手动步进保留固定 16 ms，resume 重置墙钟锚点。
每帧及主消息泵前观察 process/session 的 native 异步首错；异常收敛时同样优先报告
原始 worker 故障，避免被退出阶段的 Looper 或图形二次异常遮蔽。

## 公共 API

- `AndroidAppProcess::Create/StartApplication/StartLauncherActivity/Stop`：拥有 Manifest、
  native inventory/ABI、rootless guest process、动态 loader、DexVM、Application root 与
  Activity lifecycle；Create 只准备 API 19 system ELF，不预载 APK `.so`。Manifest MAIN/LAUNCHER
  是唯一根入口；无 native inventory 保持 Java-capable
  process mode。DVM-77 把同一 sealed Manifest 的 package/version/target SDK/application
  label/icon/meta-data/requested permissions 注入 DexVM Android context；requested permissions
  是 bounded compatibility process 的显式 granted set，不宣称模拟 protection level。
  安装事实装配后显式封存单包清单，当前 guest 仅安装本次 APK；独立未装配 context
  不视为空安装清单，也不读取宿主或 Launcher 的应用列表。
  同一 sealed Manifest 的 activity/activity-alias 也注入 context，供 `getPackageInfo`
  在 `GET_ACTIVITIES` 下发布当前包 Activity 元数据；Activity 主题注册按组件名以后声明覆盖，
  alias 直接使用 loader 封存的继承主题。receiver 声明独立注入，供当前包
  `getReceiverInfo/queryBroadcastReceivers` 使用，逐过滤器事实不合并。`AndroidAppProcessRequest` 同时把显式 `GuestProcFacts` 原样传给 native process；session
  还接收前端已解包的 curated API 19 Boot DEX，并与应用 DEX 一起交给 bridge；session
  可接收受审 API19 fallback font bytes，交给 UI 校验并绑定进程 UiTree；不读取宿主字体。
  不选择 Boot 类、不读取宿主内存，也不从 Profile 隐式覆盖虚拟设备 `/proc` 事实。
  外部 guest slice observer 及其可选宿主线程限定原样传给 native process。呈现帧的 UI
  overlay 在自有 RGBA8 buffer 上合成；经 renderer 认证的全透明 overlay 省略扫描，
  仍先校验尺寸，混色算法与视频叠放顺序保持一致。
- `LoadTitleProfileText` / `LoadTitleProfile`：仅接受 v4 的 identity、quirks，
  旧 schema 和未知字段明确失败；Profile 是“仅 identity + quirks 的精确身份覆盖层”。
- `SelectApkCompatibilityProfile`：按 package 与可选 version/hash 约束选择覆盖；
  无匹配或纯 Java APK 可走 generic startup，ABI 只由 APK inventory resolver 决定。
- `TitleProfileCatalog::LoadDirectory`：稳定加载目录，校验非法及交叠身份；已删除旧的
  单根库 exact adapter，前端只投影 profile id 与 quirk。
- `StartDexApplication` / `DexActivityLifecycle`：先按
  resolve→`<clinit>`→construct→attach base Context→虚派 `onCreate` 建立稳定的 process
  Application root，再实例化入口 Activity 并解释执行 onCreate/onStart/onResume、
  renderer surface/frame、输入、suspend/resume 与 surfaceDestroyed/onStop；实例 finish 执行 onDestroy；
  空 Activity 可继续消息泵和 Clock，finish 后由既有退出协议收尾；onCreate 内 finish
  的实例不接收 onStart/onResume/onPause/onStop。内容按对象与 UiTree 节点身份在
  owner-thread traversal 初始化尺寸，再交付 Surface/布局与已到达的窗口焦点；
  首次启动、延迟安装和切换共用该路径，不要求 onCreate 安装内容。
  未捕获 Java 异常携带具体 class descriptor、消息和解释器栈失败。每帧同时泵 VideoView
  与 AudioTrack position notification；音频回调只在生命周期解释器单写者线程执行，guest 异常使 lifecycle
  失败。首次 Surface traversal 前按 ADR-0024 冻结 onStart/onResume 后已存在的 worker
  context，并有限轮 yield，直到每个 worker 被观测到 park 或终止；期间新线程不追入，
  每轮 host progress 等待有 2ms wall-time 上限且不争抢 execution lock，超限写结构化 warn
  后 fail-open。初始 focus 仍留到下一 frame，不并入握手；窗口焦点
  事实只由 lifecycle 写入，先更新再虚派 Activity 与 attached View，Suspend/Resume、
  Activity 切换和 Stop 共用去重转换。输入按
  managed view 命中规则分发触摸与 click。pause 在 guest `onPause` 后调用持久状态 flush 回调；clean stop
  在线程停止后、guest finalizer 前再次调用，失败向上层传播。
  guest EGL swap 在 intrinsic 内 publish，不由 lifecycle 再次 present。lifecycle
  仅在 guest-owned GLSurfaceView 路径注册 driver 线程，并在每帧尾推进条件 swap
  pacer。进程 teardown 在 guest-owned 回调前只解除宿主帧 pacer 等待；在有界 root
  scope 中完成失焦、onPause、surfaceDestroyed/onStop 与前台 onDestroy，再永久退役 Java/native 图形与
  取消 renewable JNI frame。协作回调完成前允许 Java/native 线程与 AudioTrack 正常创建，
  音频消费者保持推进（手动步进退出时暂转实时消费），回调结束/超时后先停止音频消费者，
  再关闭线程准入并 join。
  Java join 与其他阻塞共享不可续期 Clock deadline；超时明确失败并记账，不冒充完成。
  回调期间可能新建等待，因此 join
  前再次 interrupt。该顺序是 OGPlay 进程退出策略，不伪称 AOSP 在
  `surfaceDestroyed` 回调前使 Surface 失效。
  注入诊断状态时，Stop 额外发布 begin、guest callbacks、scheduler、thread join、
  persistence、guest finalize、surface close 与 complete 阶段；阶段只供取证，不改变退出
  控制协议；退出清理准入与首错传播见 ADR-0095。Suspend 同样发布 begin/complete；Android app
  process 向统一快照注入 DVM trace/Java 栈、monitor owner/waiter、EGL pacer 与 GLES trace
  的不等待 provider，并在 provider owner 析构前清除回调。
  intrinsic-renderer 不安装 observer。driver 可运行时维持一帧一 swap，driver 停泊于
  guest 阻塞原语时由执行锁 observer 放行 GLThread。停止在 shutdown/join guest Java
  线程前唤醒 pacer。两种 GLSurfaceView 都释放打开线程 GL currency，交给各自 GLThread。
  pacer 停止仍维护真实 driver blocked 事实；有界协作 scope、Surface 尚存时可为
  worker 的 timed park 补统一 Clock，使暂停/Surface 销毁握手不依赖已停止的帧泵。
  intrinsic renderer 复用 VmThreadRuntime 建立独立 Java/host 线程，拥有 JNI/TLS、renderer
  回调、queueEvent 与 EGL 生命周期；不预先创建 Looper。生命周期等待绘帧时继续泵送主
  Looper；通过 `AndroidAppProcessHost.pump_host_events` 透传的显式回调在等待线程上、
  释放 worker/VM 锁后泵送宿主窗口消息，回调不得重入 guest 生命周期。空闲及暂停时
  queueEvent 仍可唤醒 GLThread。JavaTraversal owner 安全点也消费尚未附着 producer 的初始化
  事件（独立 GLThread，不要求 Surface/EGL）；内容挂载后才开放 EGL/renderer 回调，
  原生帧到达不要求 presentation epoch 改变。真正 detach/finish 或进程关闭先停止并等待
  GLThread/native detach 完成再清空引用；onPause 的渲染握手先于该停止。
  帧、呈现及内容同步统一经 integration traversal 入口，显式区分 reason/phase；
  呈现仅解析几何，内容同步先派发附着子树尺寸，再交付 Surface creation/global layout；
  未变尺寸不重复，callback 替换内容后仅在后续同步处理新 root，不启动旧子树。
  初次启动与动态挂载均在 holder 激活/尺寸发布后开放 intrinsic renderer 的 EGL 初始化；
  pending 由所属遍历消费，不清除其他窗口或回调中新挂载子树的请求。
  intrinsic renderer 消费已登记 EGL chooser/factory 或默认 config/client version，
  经 Java EGL10 facade 创建并绑定 registry Context/Surface；回调传真实 GL/config wrapper，
  绘制后由该 surface swap 发布。其 EGL wrapper 持有 GC roots，Activity 切换/退出时解除
  current 并销毁所拥有的 surface/context（自定义 factory 收到 destroyContext）；进程退出时
  此释放在图形入口永久退役前由 GLThread 完成。命令失败保留原异常，停止仍回收线程；
  创建失败明确失败，暂停保留和 context-loss 恢复未闭合。
  GLSurfaceView queueEvent 在 renderer callback 前由同一 current GL 线程排空；
  continuous 每帧绘制，WHEN_DIRTY 仅在初始帧或 requestRender 后消费一次绘制请求，事件
  即使不触发绘制也会执行。
  未捕获 Java 异常文本按失败阶段、exception、message、stack trace 分行输出；原始类描述符、
  消息、方法与 pc 不改写。
  AndroidAppProcessRequest 可注入固定 API19 tzdata，process 创建前以只读文件发布到
  `/system/usr/share/zoneinfo/tzdata`；CLI 必须提供随 payload 发布的原版归档，合成 fixture
  可省略。初始 guest TZ=UTC，与统一 Clock 分工为时区转换数据和时间源；不读取 host TZ。
- `MapAndroidInput`：在 HAL 与 Android guest 边界把通用 USB HID/SDL 物理 scancode
  转为 API 19 keyCode，把左右 modifier/caps/num 转为 metaState，并保留当前布局 Unicode、
  repeatCount、scanCode 与 eventTime；未知物理键明确成为 `KEYCODE_UNKNOWN`。
  `AndroidInputTimeline` 在入队时以 session 发布的 guest Clock 重定时，保留手势/按键的
  首次 downTime；Java 与 NDK 消费同一快照。失焦、暂停或 Activity 退役前闭合剩余
  touch CANCEL 与 key UP，UI listener、深层 View 与 Activity 均接收完整触点快照。
  `MapAndroidTouch` 将平台无关 TouchSnapshot 转为 API19 的 DOWN/POINTER_DOWN/
  MOVE/POINTER_UP/UP/CANCEL 与 action index；坐标变换不在这里重复执行。
  `MapAndroidKey` 把 `ControlMapper` 的 Android keycode 输出投影为 key 事件；
  `MapControlOutput` 统一投影触摸、直通键与 keycode 三类输出。
  JavaTraversal 的有界 EditText 输入通过 integration 从 Java findFocus/父链取候选；
  DOWN 走真实 requestFocus，字符/左右/删除复用宿主编辑事务。输入不使用宿主焦点候选，Editor/IME/composition 与修改键编辑明确失败。
  JavaTraversal 的两轴拖动只持捕获/slop/delta；拦截时 CANCEL 单一旧目标，再调用 integration
  的 Java scrollBy/clamp 与投影发布接缝，不写 UiNode.scroll_x/y。不保留宿主写 guest 位置的分支。
  Activity 转换在 VM execution lock 下消费 FIFO 命令，以每实例返回栈保留调用方、
  Intent/普通内容树/焦点与待结果；旧内容 detach 后不参与 draw/input，结束时仅退役该子树。
  恢复原实例时 restart/start、结果虚派后 resume；栈/命令/结果由 session GC roots 保活。
  同包/default affinity/单前台窗口，task 以连续分组按前台历史排序；末尾为前台栈顶。
  standard 新建，singleTop 仅复用目的 task 顶部，singleTask 清除匹配实例的同 task 上方实例，
  singleInstance 独占 task，其启动的其他组件进入默认 affinity task。任务分组切换保留身份，
  onNewIntent 在 resume 前交付，getIntent 不自动改写；finishing 实例不复用。
  自定义 affinity 匹配/跨 task 结果/向复用实例请求结果/复杂 flags/options 失败；普通同 task
  启动不匹配 affinity，非默认 affinity 分组不被选作默认目的 task。每实例保留 Java 内容与
  窗口投影；后台节点不进入前台投影，Surface 按所属窗口派发。
  稳定内容容器下的全部兄弟子树共同参与 renderer/Surface 遍历、隐藏与恢复；
  content_view/record.content 仅作首个内容 View 投影。内容代次在尺寸回调后核对，
  重入变更延后新树交付。窗口焦点事实由 lifecycle 唯一持有；在状态转换时先更新可查询事实，
  通知 Activity，再向该窗口 Java 根虚派 ROM dispatchWindowFocusChanged，沿真实 Java 子树
  分发到 View；不按投影可见性过滤，不保存宿主逐节点焦点副本，布局/追加不重发焦点。
  模态输入在 Java View/NativeActivity 转发之前捕获，不进入底层游戏。实际宿主焦点请求
  与 Activity 有效窗口焦点分离，visible modal 暂使 Activity 失焦，不合成 pause/stop 或
  Surface 退役。合成顺序为普通媒体→媒体叠层→普通 UI→onTop 视频→模态 UI；
  实际 producer 的开孔事实在 UI 缓存渲染前刷新。两层缓存共用变更
  证据，不重复叠加 scrim，无 modal 时跳过额外合成。实例结束与进程关闭分别按 Dialog
  客户端及投影退役协议处理。
  holder/callback 保留，恢复同一实例；native window callback 与播放中的视频调用方
  返回仍受限。已有 startActivity+finish 可退役旧渲染实例。
  intrinsic renderer 按 View 持有稳定 runtime/worker，后台回调不读前台 renderer 投影。
  pause/resume 在所属 GLThread 确认，EGLSurface 可重建，Context 按 preserve 请求释放。
  最后实例结束才退出；重复/中间 finish 不错误退出，Stop 在协作回调后 join，再退役后台保留实例资源。
  入口 Activity
  实例化时把自身句柄发布为 `task_root_activity`（进程唯一 task 的根，
  `Activity.isTaskRoot()` 的判定依据），switch 到达的 Activity 不是根。
- 全屏不透明视频按已解析 UiTree bounds/祖先可见性/alpha/裁剪认证覆盖。
  session 经显式 readback filter 向 boundary 提供证书；查询只 try-acquire VM 锁，
  忙或脏布局时保留读回。新视频帧发布软件基帧，在原入口合成一次；guest GLES 仍执行，
  隐藏/detach 后恢复底图，关闭前清除回调。
- frontend 取回最终 present frame 后必须交 session `ComposePresentedFrame` 与 cached UI
  overlay 做整数 source-over；纯 View Activity 在 UiTree dirty 且没有 guest renderer 时由
  lifecycle 发布不透明软件基帧，再走同一合成入口。screenshot/window 只读取返回结果，
  video 不依赖 UI。EditText 键盘编辑和 ScrollView 手势都更新 UiTree 的唯一状态，绘制、
  裁剪与命中使用同一次布局产生的 frame。
- JavaTraversal 的 Start/Resume/StepFrame 由 owner 建立首个完整快照；输入前开始逻辑 tick，
  Looper/媒体/音频/switch/focus 等 mutation 批后以同 tick 屏障重新确保遍历。
  内容与 Surface 同步使用 BeforeSurfaceCallbacks，不额外递增逻辑 tick；延期请求不被屏障偷跑。
  owner 刷新 producer/叶像素事实，纯绘制只冻结快照；presentation epoch 变化触发纯 View
  软件基帧或 intrinsic renderer draw request，空窗口与纯 modal 也可呈现。guest 仅使用此 owner 时序。
- JavaTraversal 的 `ComposePresentedFrame` 只读一次 AcquireAndroidPresentation 得到的共同快照，
  按 video下层→guest UI→video上层→可见宿主modal栈合成；不刷新输入、不调用遍历或 guest。
  session 持有有界 UI 平面缓存：完整投影/冻结 modal 身份、viewport 和 bitmap 输入未变时
  复用栅格结果；producer 单独更新不重绘 UI。新 producer 底图仍每帧按原顺序混合、上传，
  不缓存替代 guest 帧；缓存仅保留当前发布的平面，失败不替换完整缓存。
  新帧尺寸须与共同快照一致，失败保留发布序列。不保留 guest 刷新/宿主布局合成回落。
  modal 输入先由 integration 使用同一发布对象命中/捕获，再进入 Java Dialog 协议；
  每个输入事件从 modal、NativeActivity 队列、编辑/键、pointer/焦点/scroll 到 deep/Activity
  fallback 共享唯一 presentation lease；释放后才处理 mutation。scroll 范围取冻结投影。
  捕获绑定窗口/内容/父链挂载代次，失效取消一次并吞尾 UP，呈现线程不执行 guest。
  managed Surface 由 owner 屏障在尺寸完成后泵送；回调中新根/新挂载留下一轮。
- 内容树触摸由 integration 的共享分发器负责；session 仅映射输入、处理滚动与 Activity
  fallback，不保存独立 listener/deep capture。guest Decor→Activity→Window 入口使用同一
  reverse-Z、intercept/CANCEL 与捕获 owner；非 split 多指沿 DOWN 目标派发。
- ScrollView 仅在可滚动且累计位移超过 `8dp` slop 后接管；接管向原 listener/deep target
  发送 `ACTION_CANCEL` 并撤销 click/capture。纯 View 软件基帧发布前确认不存在 renderer、
  active SurfaceHolder、VideoView 或 holder Canvas producer。
- `BuildControlConfig` / `LoadControlOverride`（ADR-0142）：只合并“内置默认 → 用户
  input.toml”。内置仅鼠标/触摸直通与 Esc→Back，不提供官方区域或游戏键位。
  用户层只允许 zone/binding；schema 1、UTF-8、不超过 200 行，旧 template 字段明确失败。
  后层绑定替换同模式（或 `*`）共享输入源的默认绑定，none 解绑，区域按名称替换；
  未放置区域明确失败。`FormatControlLayer`/`SaveControlLayer` 保留 TOML 往返与原子替换，
  `DecodeControlLayerJson`/`WriteControlLayerJson` 共用同一 zone/binding 校验。
  `LoadControlOverrideIfPresent` 把缺失文件视为空用户层，不读取 Profile 或 bundled 模板。
- `QuirkRegistry::Load/LoadPackaged/Validate`：严格加载 `data/quirks.toml` 并交叉验证
  Profile 引用；源码模式额外验证测试文件/用例存在，发行模式保留测试引用形状验证。
- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：确定性会话原语。
- `AudioOutputPump`：会话拥有的音频消费；实时 worker 按设备队列补 PCM，离线路径按
  统一 Clock 差值换算帧数并保留不足一帧的余数。frontend 只注入 HAL 输出。设备
  Submit/水位查询失败时置位 `DeviceFailed` 并停止继续提交。设备、mixer 和 callback
  异常统一先调用显式 FailureInterrupt 唤醒 producer，再保存原异常并交还主循环。
  未注入 `sound_resource_loader` 时默认走 `LoadEncodedAudioWindow`。
  MCP 手动步进按 lifecycle Clock tick 差推进离线混音；许可 step 正在执行且 PCM writer
  被队列背压阻塞时，独立消费者可先混音释放 writer，避免主 Looper 写入与 step 后消费互等。
  提前消费帧从后续 Clock 额度扣回，不重复推进音频；无 step 或无背压不消费，不提交 HAL。
  消费者错误先 interrupt writer，再交还主循环，不能静默绕过写入。
  宿主协作停止期间恢复消费并暂启实时 worker，在生命周期回调结束、guest 线程退役前停止。
  mixer/callback 异常保留原异常并交还主循环，设备提交失败单独记录。进程按 stream 初始化 native gain，
  视频混音底座按每播放器 stream 应用增益；VideoView 固定 MUSIC，普通 MediaPlayer
  按已设置 stream，不在 session 再次缩放。

## 不变量

- Title Profile 仅接受 schema 4，root 只允许 schema/identity/quirks；identity
  只允许 package/version_code/so_sha256。文件 UTF-8、纯数据、不超过 200 行。
- Profile 只能覆盖注册 quirk，不拥有运行时、资源、入口或音频策略。
  API 19 与 VM/guest call 默认值由通用实现决定；逻辑表面尺寸由用户设置提供；Manifest 和 APK inventory
  提供入口与 ABI，前端按标准 Android 路径装配 OBB 和外部目录。
- exact-key table 校验共用 TOML 私有入口，各 table 声明允许键集合与上下文。
- 身份必须精确匹配声明条件；`src/` 不出现标题、厂商或包名分支。
- enabled quirk 必须有注册定义和可定位测试；源码/CI 在打包前验证测试文件与用例存在，
  packaged runtime 至少严格保留其引用形状；未注入注册表时不得进入匹配目录。
- 生命周期清理顺序固定，状态推进由固定帧步进驱动，不依赖 sleep。
- app process 发布逻辑键盘（id=-1）与鼠标映射触摸（id=0）；触摸轴范围采用逻辑窗口坐标。
  QueueInput 将宿主设备编号归一到这两个 id，Java 与 NativeActivity 输入队列共用该映射。
- app process 状态只允许 DexVmReady→ApplicationStarted→ActivityResumed→Stopped；
  frontend 不得跳过 Application 或直接选择/初始化 APK ELF root。
- Application 初始化失败必须先清除临时 root，且不得打开 surface、初始化或构造 Activity。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。
- 不恢复 Profile 声明的 JNI 调用序列或 Java handler 映射。

## 测试

`tests/session/` 覆盖 v4 身份/拒绝旧字段、quirk、生命周期、输入与确定性；
标准入口/音频见 application_startup 与 audio_track 测试，标准挂载见 run_apk_vfs 测试；
`ui_compositor_tests.cpp` 锁定透明 overlay 基线不变和局部合成 exact pixels；
`tests/dexvm/widget_click_tests.cpp` 锁定 touch/click ownership 四组合、无 listener 的深层
View override、reverse-Z fallback、capture 及取消路径；
`tools/validate_title_profiles.py` 提供独立目录门禁。

DVM-105：纯 Java APK 也建立 NativeLibraryLoader，用于平台 Java 类加载 bundled JNI。
APK selected ABI 仍为空，application ELF 数仍为零。内部空 ARMv7 库视图只用于 API 19
系统进程，不表示 APK 自带 native 库。

DVM-107：根启动向 lifecycle 分别传实际 launcher descriptor 与 manifest component name，
alias 只影响实例化目标，Activity 的组件身份保留 alias。根启动和切换均在 attach base 后、onCreate 前附加 ComponentName/Intent；
旧实例保留自己的引用，不从进程 current_intent 动态读取。当前 APK 内受限隐式解析与显式
非根 alias 切换同样分别携带 Manifest 组件名和实际 target Activity descriptor。

DVM-112/128：AndroidAppProcess 把 sealed Manifest 的 application enabled、service 过滤器
及独立 `AndroidGuestPlatformConfig.android_id` 复制到同一 DexVmAndroidContext，并显式标记
inventory 已就绪；独立 VM 未装配时保持
未知，不能把缺少信息当作服务不存在。服务信息不参与 Activity 启动或产生绑定状态。
同一平台配置的 `strict_webview_errors` 只控制 Web facade 的开发诊断策略；默认关闭，
session 不据此创建浏览器、网络或 Activity 能力。

DVM-121：从 sealed Manifest 发布 application theme 与逐 Activity theme；alias 继承
目标 Activity，0 回退 application。integration 在 Activity.onCreate 前应用对应资源 id，
session 不解析 style/颜色或创建完整 Android Theme。


DVM-150：显式 guest Runtime/System.exit 先完成 BootDex hook 协议，halt 跳过 hook；VM 退出码
已发布的生命周期调用展开进入 Stop，不计为 guest fault。Application 内退出后不再启动
launcher；已退出 VM 的 Stop 不再调用 Java 生命周期回调。宿主 Stop 仍是已有取消/回收路径，
不隐式运行 hook；没有最后 non-daemon 线程结束自动退出或 hook 超时成功语义。挂起的原版
hook 可阻塞正常 exit，强制取消不能宣称正常 hook 完成。详见 ADR-0056。

DexActivityLifecycle 成功启动与恢复后向已连接 DiagnosticState 发布 running 阶段；无需启动写盘 coordinator，阶段 generation 仍由 DiagnosticState 维护。

退出前记录并退役已失败调用的 root JNI 异常；本地服务在 scheduler 关闭前解绑。
native 清理失败仍继续关闭进程和 surface；前端保留运行首错，独立报告清理错误。

本地视频在最终 frame handoff 按 UiTree 的 SurfaceView 层事实合成（ADR-0094）；
GLES/Canvas producer 活跃时视频泵不覆盖底图。纯视频窗口使用空软件底图触发统一合成。

DVM-218/236、ADR-0095/0115：进程 Stop 显式区分 host shutdown、guest exit 与运行 fault，
退出事实优先。健康宿主关闭在既有预算内独立尝试前台失焦/pause、Surface retire/stop、
前台 onDestroy；期间 VM 仍可创建 Java/native 线程，音频消费保持推进。回调后先调用显式
`before_guest_shutdown` 停止并回收音频消费者，再 BeginTeardown、VM shutdown/join；
超时传播原错并记 `session.lifecycle.cleanup_timeout`，到期后不继续 guest 回调。
后台实例资源随进程退役；普通 Activity.finish 仍按实例执行 destroy 并传播异常。
进程关闭拒绝新转换/输入，关闭 scheduler、join native/Java worker 后由 owner 退役媒体/图形
资源，再执行仍允许的 native cleanup。退出预算覆盖重入且不续期，首错保持、重复 Stop 幂等。
进程 Stop 不执行模块卸载式 DSO fini；底层显式 permitted 卸载/停机的执行与失败传播保持。
Java/native 进程退出事实发布后不再调用 guest 生命周期或 JNI 清理；原生
exit/exit_group 的真实退出码与展开规则保持。Stop 失败仍 join、flush、detach、关闭 surface；
取消不冒充卸载，native finalization 失败仍报告。

AndroidAppProcess 在 native 创建前由封存 Manifest 的 supports-gl-texture 决定默认厂商
压缩族：明确的单一已支持 ATC/PVRTC 声明（可伴 ETC1）收窄发布，无/未知/多厂商声明
保持 all。记录声明及策略来源，Profile 不提供纹理格式覆盖，不按名称猜资源。

AndroidAppProcessRequest 的原生单次调用默认预算引用 runtime/common 的统一看门狗常量
（100 亿 tick），与 AndroidGuestProcessRequest/AndroidGuestCallSessionRequest 一致；
Profile 和用户设置均不覆盖。

AndroidAppProcessRequest.skeleton_bytes 与已解包 BootDex 一并显式交给 bridge；session 不解析骨架或读取宿主类路径。

Application 类初始化前从 Context.getCacheDir 取得缓存路径改写 java.io.tmpdir，
对齐 API19 ActivityThread 绑定时机；没有缓存目录时保留绑定前默认值。


JavaTraversal窗口切换保留每窗口Java根及完整投影，前台输入分区与额外可见handoff分区
同次发布；恢复焦点走Java软件adapter，不向 guest 投影调用 HostLayout RequestFocus。
Compose的一次lease显式交给video上下层、UI与modal，bitmap版本/producer像素属于快照；
呈现线程不读取当前播放器/绑定表或消费dirty，不为输出像素大小重测Java。

DVM-315：startup/frame/suspend/resume 中，宿主发起的 owner/observer/Surface/窗口
adapter 若在 vm.Call 外抛出 VmJavaThrow，转为 DexActivityLifecycleError，保留明确
Java 类型、消息与宿主阶段；仍标 failed、保留原退出及异步 fatal 优先语义，不伪造成功。

DVM-316：生命周期 Java 故障与宿主 guest 边界追加只读 cause/target 的类型、消息和首栈；
既有主异常行、失败状态与退出语义保持。

JavaTraversal 的 `ui.surface.*` 诊断记录 producer 投影可见性、所属 GLThread 启动与 EGL
创建请求；初始化线程启动不代表已有可绘制 Surface，真实 EGL 与呈现仍需独立验证。


JavaTraversal 在 owner 投影屏障及内容遍历后重评估已注册 intrinsic producer，先于该屏障的
Surface 交付；仍要求可见且属于当前内容根。首条选择/拒绝原因按窗口记录，后续计数。
GLThread 初始化不等于可绘制 Surface。

DVM-320：JavaTraversal 在每次 producer 重评估比较只读父链事实；正常与失败 Stop
均在 teardown 回调前经显式 integration 接口汇总一次。汇总保留清理前的最终状态，
重复 Stop 不重发；失败清理仍沿原 attempt/首错协议。

JavaTraversal 的窗口可见事实由 Activity 生命周期单独拥有：resume/restore 显示，
suspend、stop 与 handoff 隐藏；即使尚无 Java Window root 也记录事实。变化在 owner 上
重发最后完整几何及窗口事实，保留待布局请求；producer 仅从完整投影选择。退役空窗口
不隐藏共用 synthetic root。

DVM-329：producer 每次选择按 Java View 身份重新查询绑定，并检查该 NodeId 存在于
本次读取的完整 projection 后才查询可见/祖先；未发布或退役的绑定本轮不激活，下次
owner 发布后重新查询。不能将构建树中新节点当作旧投影节点，也不放宽 guest stale 失败。
