# 12 · API 19 能力栈与 intrinsic 布局收敛

## 1. 目标

下一阶段不按 AOSP package 铺全 API，而是补齐老游戏启动、渲染、音频和存档路径共用的
基础能力，并把 intrinsic 源码收敛到稳定的 API family。

核心交付顺序固定为：

```text
源码与 ownership 收敛
→ API 19 Context 类型体系
→ java.nio / DirectByteBuffer
→ Java GLES / AudioTrack / SystemClock 与调度
→ Android 高频小能力
→ 真实命中驱动的 P1
```

本章是开发约束，不表示对应能力已经实现。每项完成状态仍以 `capabilities.toml` 和
`docs/state/CURRENT.md` 为准。

## 2. 当前基线

以下结论已按 DVM-81 完成后的 2026-08-25 `main` 复核：

- DVM-80 已把 `src/runtime/dexvm/intrinsics/` 与
  `src/runtime/integration/dexvm_android/` 收敛为 12/18 个 `.cpp` family TU；22 个非
  Android Java 声明已迁回 core，layout gate 锁定 ownership 与唯一 catalog 注册点。
- DVM-81 已发布 `ContextWrapper`/`ContextThemeWrapper`，并恢复 Application、Service、
  Activity、IntentService 的 API 19 superclass；现有 Context 行为经同一 base 委托。
- core 尚无 `java.nio.Buffer` 家族；`Charset` 仍在 Android integration。
- JNI ABI 目录包含 `NewDirectByteBuffer`、`GetDirectBufferAddress`、
  `GetDirectBufferCapacity`，但生产绑定测试明确要求三者当前未绑定。
- Java GL 当前只有 EGL façade 和最小 `GL10.glGetString`；native EGL/GLES1/GLES2 已共享
  唯一 `GuestGlContext`，Java GLES 必须接入该状态，不能另建 GL 实现。
- DVM-84 已把 DexVM 与旧 Java/JNI `AudioTrack` 接到进程唯一 `OpenSlesPcmMixer`；
  STREAM/STATIC write、播放状态、音量、播放头及 GC 释放均共享真实 backend。
- `Handler.sendMessage/post` 当前同步重入 guest，`Looper.prepare/loop` 为 no-op；
  `DexVmAndroidContext::uptime_millis` 已由 lifecycle 发布，可作为统一 Clock 的接入点。
- `Context.getSystemService()` 当前只提供 phone、audio、wifi、sensor、connectivity、
  input_method 和 window，其他服务明确抛 `UnsupportedOperationException`。

因此，后续工作的重点是补共享 runtime 和真实语义，不是增加 neutral-return stub。

## 3. 范围边界

### 3.1 必须做到

- API 19 可见的 superclass、interface、方法签名、常量值和异常类型正确。
- 基础容器、Buffer、Parcel、SparseArray 等数据类必须保存真实状态。
- Java、JNI 和 native boundary 对同一资源使用同一 backend 和同一对象身份。
- 未覆盖的方法继续逐方法记账并明确失败；survey 结果只能生成候选工作单。
- 每个行为变更都有机器可判定测试；最终兼容性结论必须关闭 survey。

### 3.2 明确不做

- 不实现 Binder、service manager、system_server、Zygote 或完整 Android 系统。
- 不建立第二套 EGL/GLES 状态、Java-only PCM mixer、线程池线程模型或宿主网络旁路。
- 不以 API 19 class 覆盖率为目标，不优先深化 Telephony、SMS 或 WebView。
- 不在 `src/` 引入游戏名、厂商名、包名或 title-specific handler。

## 4. Ownership 与统一 backend

### 4.1 命名空间归属

```text
runtime/dexvm/intrinsics
  java.*
  javax.net.*
  javax.xml.*
  org.xml.sax.*
  dalvik.*

runtime/integration/dexvm_android
  android.*
  javax.microedition.khronos.egl.*
  javax.microedition.khronos.opengles.*
```

core Java intrinsic 不得 include 或保存 `DexVmAndroidContext`。需要 VFS、guest 内存、网络等
平台能力时，只注入窄接口，依赖方向保持从 integration 指向 core。

### 4.2 统一 runtime

| Runtime | 单一事实 | 消费者 | 约束 |
| --- | --- | --- | --- |
| `NioRuntime` | Buffer 元数据、backing/view、byte order | typed buffer、Java GLES、AudioTrack、JNI direct buffer | view 共享 backing；GC 可追踪；不得暴露悬挂宿主指针 |
| `AndroidScheduler` | deadline、sequence、owner thread/Looper、cancel 状态 | Handler、Looper、HandlerThread、Timer、CountDownTimer、AsyncTask | 使用统一 Clock；不得同步伪装异步投递 |
| `JavaGlBridge` | Java 参数编组，不拥有 GL 状态 | GL10、GLES10/11/20、GLUtils | 只调用现有 graphics dispatch/`GuestGlContext` |
| `PcmPlaybackRuntime` | PCM player、queue、play state、head position、volume | OpenSL ES、AudioTrack，后续 SoundPool/MediaPlayer 可复用 | mixer 只有一个 owner；Java write 必须进入真实队列 |
| `NetworkRuntime` | socket、DNS、TLS policy 与 guest-visible handle | `java.net`、`javax.net.ssl` | 默认 policy 禁止隐式泄露宿主网络 |

`NioRuntime` 的 backing 至少区分：

- heap array：保存 `VmObjectRef + array offset`；
- direct memory：保存强类型 guest address、capacity 和 ownership token；
- view：保存共享 storage id、byte offset、element type 和独立 cursor。

`ByteBuffer.allocateDirect` 只能经 integration 注入的 guest-memory allocator 分配；
`NewDirectByteBuffer` 必须校验传入 guest 地址区间，并让三个 JNI direct-buffer API 与
Java Buffer 返回同一地址和容量。

## 5. 目标源码布局

布局收敛只改变物理 TU，不改变 class 级 `Declare_*` 边界。`catalog.cpp` 只调用 family
appender 或 declaration，不得出现 handler。

### 5.1 Core intrinsic：约 12 个 `.cpp`

| 文件 | 内容 |
| --- | --- |
| `catalog.cpp` | 唯一注册点 |
| `java_lang.cpp` | Object、String、System、Math、Thread、Enum、wrapper、lang interface、throwable、ref |
| `java_classloading.cpp` | ClassLoader、BootClassLoader、PathClassLoader |
| `java_reflect.cpp` | Class 与 `java.lang.reflect` family |
| `java_io.cpp` | Serializable、streams、readers/writers、File、IO exception |
| `java_util.cpp` | collections、Date、Random、Locale、Timer、Calendar、Arrays/Collections algorithm |
| `java_regex.cpp` | Pattern、Matcher、PatternSyntaxException |
| `java_zip.cpp` | zip/gzip family |
| `java_nio.cpp` | Buffer family、ByteOrder、Charset |
| `java_net.cpp` | URL、HTTP、socket、datagram、`javax.net.ssl` |
| `java_xml.cpp` | `javax.xml.parsers`、`org.xml.sax` |
| `java_concurrent.cpp` | Executor/Future/concurrent/atomic |

### 5.2 Android integration：约 18 个 `.cpp`

| 文件 | 内容 |
| --- | --- |
| `catalog.cpp` | 唯一注册点 |
| `shared.cpp` | 仅跨 family helper |
| `support_ui.cpp` | binding、inflater、widget dispatch |
| `android_app.cpp` | Application、Activity、Dialog、Service、PendingIntent |
| `android_content.cpp` | Context/Wrapper、Intent、receiver、preferences、package manager |
| `android_resources.cpp` | Asset、Resources、Configuration、TypedValue |
| `android_graphics.cpp` | Bitmap、Canvas、Paint、geometry、drawable、PorterDuff |
| `android_hardware.cpp` | sensor family |
| `android_media.cpp` | AudioManager、SoundPool、MediaPlayer、VideoView、AudioTrack |
| `android_network.cpp` | connectivity、Uri、wifi |
| `android_gl.cpp` | GLSurfaceView、javax EGL/GL、android.opengl GLES/GLUtils/GLU |
| `android_os.cpp` | Build、Bundle、Handler/Looper、SystemClock、Parcel、power/process |
| `android_device.cpp` | telephony、SMS、Settings 等有界 device façade |
| `android_text.cpp` | Editable、TextPaint、TextWatcher、TextUtils |
| `android_util.cpp` | Log、Pair、DisplayMetrics、Base64、SparseArray family |
| `android_view.cpp` | View/ViewGroup、surface、window、input、ContextThemeWrapper |
| `android_widget.cpp` | widget family |
| `android_webkit.cpp` | WebView family |

`android_*_*.cpp` 按首个 API family 前缀归入上表；`support_activity.cpp` 归入
`android_app.cpp`，`support_video.cpp` 归入 `android_media.cpp`，三个 UI support 文件归入
`support_ui.cpp`。非 Android Java family 必须迁回 core。

这两个数字是最终目录目标。DVM-80 经用户明确授权作为一次全量机械归并执行，不受通常的
单 WU 10 文件与 family TU 800 行限制；行为变更仍然禁止混入。

## 6. 实施批次

### A · 物理归并与 ownership 纠正

要求：

- 每次只移动代码，不改 handler、签名、声明顺序或会话状态。
- 先迁 `java.*`/SSL/XML ownership，再逐 family 合并 Android 文件。
- 保留 catalog 的 descriptor 顺序，避免改变 class identity、静态初始化或测试快照。
- CMake 当前以 `CONFIGURE_DEPENDS` glob 收集两个目录；归并不得增加手工源文件清单。

验收：catalog descriptor/method/field 集合迁移前后完全相等；core、Android、GC、JNI 和
architecture focused tests 全绿。

### B · API 19 Context 类型体系

状态：DVM-81 已完成本批次；能力总项仍因其他 Android API 缺口保持 partial。

必须新增并按 AOSP 修正为：

```text
Context
└─ ContextWrapper
   ├─ Application
   ├─ Service
   └─ ContextThemeWrapper
      └─ Activity
```

- `ContextWrapper` 保存稳定 base Context，`attachBaseContext` 只能成功一次；Context 方法委托
  base，不复制 service/resource 状态。
- `ContextThemeWrapper` 先提供正确结构与有界 theme 状态；未实现 theme 行为明确失败。
- `Application`、`Activity` 和 `Service` 使用真实 superclass；不引入 Instrumentation、
  ActivityManager 或 service process。

验收：`instanceof/check-cast`、`getSuperclass/getInterfaces`、继承方法解析、`invoke-super`、
base Context identity 和现有 Activity lifecycle 回归通过。

### C · `java.nio` 与 JNI direct buffer

状态：DVM-82 已完成本批次；完整 charset/file channel 不在本批次，Java core 总项仍为
partial。

首轮类型：`Buffer`、`ByteBuffer`、`ByteOrder`、Short/Int/Float/Char/Long/DoubleBuffer，
以及 BufferOverflow/Underflow、InvalidMark、ReadOnlyBuffer exception。

首轮行为：cursor 操作、remaining、allocate/allocateDirect/wrap、relative/absolute/bulk
get/put、array/arrayOffset/hasArray、slice/duplicate/read-only view、byte order 和全部 typed
view。越界、只读、mark 失效、重叠 copy 的异常和更新顺序以本地 libcore API 19 为准。

验收：heap/direct/view 共享、大小端、GC、JNI 三个 direct-buffer API、非法 guest 地址和
Java GLES buffer 编组测试通过。

### D · 游戏数据面

状态：Java GLES、AudioTrack 子项已由 DVM-83/DVM-84 完成；时间/调度仍待后续 WU。

1. Java GLES：实现 `GLES10/GLES10Ext/GLES11/GLES11Ext/GLES20/GLUtils/GLU` 的 API 19
   可链接面；行为按现有 native GLES catalog 分族接入，未知或边界外入口明确失败。
2. AudioTrack：实现 `AudioFormat` 常用 PCM 常量，以及 ctor、`getMinBufferSize`、state、
   play/pause/stop/flush/release、byte/short write、volume 和 playback head；STREAM/STATIC
   共用一个 PCM queue backend。
3. 时间与调度：`SystemClock.uptimeMillis/elapsedRealtime/elapsedRealtimeNanos/sleep` 读取统一
   Clock；建立真实 Looper queue 后再发布 `HandlerThread`，并把 Handler/Timer 的同步或
   next-frame 近似迁到同一 scheduler。

验收：Java GL 与 native GL 交错调用观察同一 texture/program/buffer 状态；GLUtils 复用
Bitmap 像素；AudioTrack write 能在 mixer 输出并推进 playback head；固定 Clock 下消息顺序、
delay、cancel 和 shutdown 可确定复现。

### E · 高频 Android 小能力

- util/text：Base64、SparseArray/SparseBooleanArray/SparseIntArray、TypedValue、TextUtils；
- graphics：Color、RectF、Point/PointF、Path、PorterDuff.Mode、BitmapDrawable、ColorDrawable；
- value transport：Parcelable、最小 Parcel、Bundle Parcelable API；Parcel 只覆盖 primitive、
  String、byte array 和已登记 Parcelable，不支持 Binder transaction、fd 或跨进程；
- services：PowerManager/WakeLock、Vibrator、Process 的进程内有界 façade，并从
  `getSystemService` 返回稳定 singleton。

这些类不得使用 neutral handler 代替数据语义。未知 flag、类型或宿主动作必须明确失败。

### F · 真实命中驱动的 P1

按关闭 survey 的 reached gap 和可复跑 Scenario 排序：

1. Arrays/Collections algorithm、Comparator、Calendar/TimeZone；
2. Pattern/Matcher；
3. Executor/Future/atomic，复用 `VmThreadRuntime` 和统一 scheduler；
4. socket/datagram/TLS，经过 `NetworkRuntime` 与显式 policy；
5. ContentValues/Cursor/SQLite，数据只落 guest VFS；
6. 其他 framework 长尾。

静态 DEX gap 报告只用于候选排序，不能直接推动整包实现。

## 7. 架构门禁

归并完成时新增一个 layout checker，并接入 `architecture` CTest label：

- 两个 intrinsic 目录的 `.cpp` 必须属于 family 白名单；
- 每个 descriptor 恰好一个 `Declare_*`，catalog 不得定义 handler；
- 禁止新增单类 TU 和 `*_misc.cpp`、`*_common.cpp`、`*_all.cpp`；
- Android integration 禁止新增 `java.*`、`javax.net.*`、`javax.xml.*`、`org.xml.sax.*`；
- core intrinsic 禁止依赖 `DexVmAndroidContext`；
- API 19 shape manifest 与 catalog 的 superclass/interface/method/field/constant 可机器比对。

每个能力 WU 至少运行对应 doctest/CTest、`architecture` label 和受影响的 targeted regression；
阶段完成后按 Windows `windows-msvc` 预设执行完整 configure、build、CTest。能力有变化时同步
更新 `capabilities.toml`、相关 `MODULE.md` 和 `docs/state/CURRENT.md`。

## 8. AOSP API 19 参考锚点

实现前按 [07 · AOSP 参考策略](07-aosp-reference.md) 对照本地固定源码：

- 类型体系：`framework/base/core/java/android/{content/ContextWrapper.java,view/ContextThemeWrapper.java,app/Application.java,app/Activity.java,app/Service.java}`；
- NIO：`libcore/luni/src/main/java/java/nio/` 与 `dalvik/vm/Jni.cpp` direct-buffer 入口；
- Java GL：`framework/base/opengl/java/android/opengl/` 及 `framework/base/core/jni/android_opengl_*`；
- 音频：`framework/base/media/java/android/media/{AudioFormat.java,AudioTrack.java}`、
  `framework/base/core/jni/android_media_AudioTrack.cpp`；
- 时间/消息：`framework/base/core/java/android/os/{SystemClock.java,Handler.java,Looper.java,MessageQueue.java,HandlerThread.java}`；
- value/service：`framework/base/core/java/android/os/{Parcel.java,Parcelable.java,PowerManager.java,Vibrator.java,Process.java}`。

AOSP 只作语义、shape 和测试参考，不编译进 OGPlay。

## 9. 阶段完成定义

本阶段完成必须同时满足：

- intrinsic 目录达到 family-only 布局且架构门禁生效；
- Context hierarchy、NIO/direct buffer、Java GLES、AudioTrack、SystemClock/调度的 P0 面均有
  真实状态和机器测试；
- Java/native/Android façade 之间没有重复 GL、PCM、Clock、线程或 Buffer backend；
- 至少一个通用 Scenario 在关闭 survey 时覆盖 NIO→Java GLES 与 AudioTrack 路径；
- 所有 deferred/unsupported 面在能力账本中可查询并明确失败。
