# 08 · 解释执行的 EGL/GL façade（通用形态）

本篇把 [DVM-31](../../tasks/dexvm/DVM-31.md) 展开为可分批执行的设计：自带
`GLSurfaceView` 的 title 用解释执行的 javax EGL 面驱动宿主已拥有的 ANGLE
surface，而不是第二套 EGL。动手前先读 `docs/state/CURRENT.md`、
`src/runtime/integration/dexvm_android/MODULE.md`、`src/runtime/boundary/MODULE.md`
与 `docs/playbook/NEW-TITLE.md`。

## 0. 通用性红线（最高优先级）

本方案实现的是 **AOSP `GLSurfaceView` 这一代 title 的通用形态**，Asphalt 6 只是
第一个消费者与 gate 载体。硬性要求：

- `src/` 不得出现任何游戏名、厂商名、包名或 title 分支（AGENTS.md 既有红线）。
- façade 行为只允许来自三个事实源：
  1. AOSP javax EGL/GLES API 形状与常量（参考源码 `.local/opengl`，见 §2）；
  2. 宿主 managed surface 的真实属性（RGBA8+D24S8，`gles.angle`）；
  3. façade 自身的 guest 可见状态机（本文 §4）。
- 不为 A6 的具体调用序列写捷径：每个入口按 EGL 规范子集实现完整语义，
  A6 未走到的分支按"记账并明确失败"处理，而不是按 A6 走到的路径裁剪语义。
- 新增的 GL currency 迁移（§5）是 boundary/gles 的通用能力，任何自带
  GLSurfaceView 的 title（Dungeon Hunter 后续 title 同款形态）直接受益。

## 1. 背景与阻塞现场

- Asphalt 6 打包了一份改名的 AOSP `GLSurfaceView`（dex 内应用类），其
  `GLThread`（DVM-28 后是真实宿主线程）自驱 EGL。三轮 exact 逐字一致停在
  `class is not available: Ljavax/microedition/khronos/egl/EGLContext;`
  （证据 `.local/evidence/a6-gate-r1..r3/`）。
- 已核实的 guest 调用序列（反编译源 `.local/jadx/asphalt6/sources/`，
  `GLSurfaceView.java` EglHelper + `GameGLSurfaceView.java` ConfigChooser）：

```text
启动:   EGLContext.getEGL() → eglGetDisplay(EGL_DEFAULT_DISPLAY)
        → eglInitialize(display, int[2]) → eglChooseConfig(两段式: 先计数后取)
        → eglGetConfigAttrib(逐属性, 失败回退默认值) → eglCreateContext(
          attribs {12440, 2, 12344}) → eglCreateWindowSurface(holder)
        → eglMakeCurrent(d, s, s, ctx) → EGLContext.getGL()
每帧:   renderer 回调(解释执行) → native 绘制(GLES trap) → eglSwapBuffers
暂停:   eglMakeCurrent(d, NO_SURFACE, NO_SURFACE, NO_CONTEXT)
        → eglDestroySurface
退出:   eglDestroyContext → eglTerminate
错误路: eglGetError（swap 失败后区分 12299/12302）
```

- 该 title **从不调用** intrinsic `android.opengl.GLSurfaceView.setRenderer`
  （`GameGLSurfaceView` 继承的是 dex 里自己的 GLSurfaceView），所以
  `DexVmAndroidContext::renderer` 保持无效，`dex_activity_lifecycle.cpp`
  `StepFrame()` 的 `onDrawFrame`+`present_surface` 路径天然不激活。
  **两条 present 驱动的互斥由此自动成立**：intrinsic-renderer 形态由 lifecycle
  present，自带 GLSurfaceView 形态由 guest `eglSwapBuffers` present，一个
  session 只会激活其一。

## 2. 参考资料（AOSP 4.4.4_r1，`.local/opengl`，只作语义参考，不 vendor）

| 内容 | 路径 |
| --- | --- |
| EGL/EGL10/EGL11 接口与全部常量值 | `.local/opengl/java/javax/microedition/khronos/egl/*.java` |
| 平台实现语义（getEGL、Impl 类、错误处理） | `.local/opengl/java/com/google/android/gles_jni/EGLImpl.java` 等 |
| GL10 接口形状与 glGetString | `.local/opengl/native/tools/glgen/stubs/jsr239/`、`GLImpl.java` |
| EGL native 语义仲裁 | `.local/opengl/native/libs/EGL/eglApi.cpp` |

关键 AOSP 事实（已核实）：

- `EGL10.EGL_DEFAULT_DISPLAY` 是 `Object` 类型且**值为 null**——`eglGetDisplay`
  的默认入参就是 null，不需要为它造 singleton。
- `EGL_NO_DISPLAY/EGL_NO_CONTEXT/EGL_NO_SURFACE` 是静态对象单例，guest 用
  **引用相等**比较（EglHelper 里到处是 `== EGL10.EGL_NO_SURFACE`），必须按
  `Bitmap$Config` 先例在 `<clinit>` 建 singleton。
- `EGL10 extends EGL`（marker）、`GL10 extends GL`（marker）；EglHelper 的
  方法签名直接引用 `GL`，类链接时就会解析，两个 marker 接口都必须声明。

## 3. 现状盘点（已核实，含关键文件）

| 事实 | 位置 |
| --- | --- |
| `EGLConfig` 已有空壳具体类声明 | `src/runtime/integration/dexvm_android/javax_microedition_khronos_egl_EGLConfig.cpp` |
| `GL10` 已声明为空接口（无 super、无方法） | `.../javax_microedition_khronos_opengles_GL10.cpp` |
| `EGLContext/EGL10/EGL/EGLDisplay/EGLSurface/GL` 全部缺失 | 触发本次阻塞 |
| intrinsic 声明 API（Static/Virtual/Field/ConstantInt/Clinit/Unimplemented） | `include/ogplay/runtime/dexvm/intrinsic_builder.h` |
| 静态对象单例先例（Field + Clinit + SetIntrinsicStaticRef） | `.../android_graphics_Bitmap_Config.cpp` |
| `$Impl` 命名先例（接口 + 宿主实现类） | `.../android_view_SurfaceHolder_Impl.cpp`（`Landroid/view/SurfaceHolder$Impl;`） |
| session 状态唯一入口 | `include/ogplay/runtime/integration/dexvm_android.h` `DexVmAndroidContext` |
| managed surface open/present/close | `AndroidGuestCallSession::{Open,Present,Close}ManagedSurface` → `src/runtime/boundary/android_boundary_hle.cpp:179-211` |
| ANGLE context 目前创建线程一次性 make-current，无迁移 | `src/gles/egl_lifecycle.cpp:302`（创建即 current）、`:350`（析构时解绑） |
| intrinsic handler 全部在 `VmExecutionLock` 内、调用方宿主线程上执行 | `src/runtime/dexvm/MODULE.md` |
| 子线程 native 调用在调用方宿主线程上执行（复用 root guest 栈） | 同上"尚未实现"节 |
| 持有 native 帧的线程禁止停泊（`blocking_in_native`） | `src/runtime/dexvm/vm_threads.cpp:52-63` |
| guest 数组读写 API | `JavaObjectModel::{Get,Set}PrimitiveElement/SetObjectElement/ArrayLength`（`include/ogplay/runtime/dexvm/object_model.h`） |
| singleton/字符串 helper | `src/runtime/integration/dexvm_android/shared.h`（`Singleton`、`MakeString`） |
| SurfaceHolder 注册表（native_window 校验依据） | `DexVmAndroidContext::surface_holders` |

## 4. 设计总览

三层，自下而上：

1. **GL currency 迁移（gles + boundary，通用地基）**：ANGLE context 的
   "当前线程"从隐式（创建线程）变为显式受管，允许在受控条件下迁移到
   发起 GL 工作的宿主线程。
2. **javax EGL/GL 类形状与 façade 状态机（dexvm_android intrinsic）**：
   按 AOSP 形状声明类与常量；18 个 EGL10 方法实现为宿主 managed surface
   的 façade——不创建第二套 EGL，`runtime.host_managed_gl_surface` 的
   "guest EGL 不得替换或终止 host-owned surface"契约不变。
3. **present 与帧节拍（session lifecycle）**：`eglSwapBuffers` 触发
   `PresentManagedSurface` 并以帧屏障与宿主帧循环对齐（每宿主帧一次 swap），
   同时解决确定性与执行锁饥饿。

### 4.1 错误模型（两类，严格区分）

- **EGL 规范内可表达的失败**：按规范返回 false/EGL_NO_*，并把错误码存入
  façade 的 last-error（`eglGetError` 读取后复位为 EGL_SUCCESS）。guest
  （AOSP GLSurfaceView）会读这些错误并走自己的重试/回退路径，必须走规范
  通道而不是宿主异常。例：未知 config 属性 → false + `EGL_BAD_ATTRIBUTE`。
- **模型破坏（我们的单 surface/单 currency 模型被违反）**：记账
  （ledger `RecordUnimplemented` 或专用条目）并明确失败（宿主异常），
  绝不伪造成功。例：第二个并存 window surface、跨线程抢夺 GL currency、
  在无 managed surface 时 createWindowSurface。
- 未列入 §4.3 范围的 EGL10 接口方法：接口上声明形状（invoke-interface
  解析需要），Impl 类用 `Unimplemented()` 显式进入记账+失败路径。

### 4.2 新增类清单（一类一文件，进 `catalog.h/cpp`）

| 描述符 | 形态 | 内容 |
| --- | --- | --- |
| `Ljavax/microedition/khronos/egl/EGL;` | 接口 | 空 marker |
| `Ljavax/microedition/khronos/egl/EGL10;` | 接口，Implements EGL | 全部 int 常量（值抄 `.local/opengl/java/javax/microedition/khronos/egl/EGL10.java`）；静态字段 `EGL_NO_DISPLAY/EGL_NO_CONTEXT/EGL_NO_SURFACE`（Clinit 建 singleton）与 `EGL_DEFAULT_DISPLAY`（保持 null，不需初始化）；AOSP 全部 21 个方法的形状声明 |
| `Ljavax/microedition/khronos/egl/EGL10$Impl;` | 类，Implements EGL10 | §4.3 范围内方法的 handler；范围外 `Unimplemented()` |
| `Ljavax/microedition/khronos/egl/EGLContext;` | 类 | `Static getEGL()` 返回 EGL10$Impl singleton；`Virtual getGL()` 返回 GL10$Impl singleton |
| `Ljavax/microedition/khronos/egl/EGLDisplay;` | 类 | 空壳（façade 实例化；dex 从不实例化/继承它们） |
| `Ljavax/microedition/khronos/egl/EGLSurface;` | 类 | 空壳 |
| `Ljavax/microedition/khronos/opengles/GL;` | 接口 | 空 marker |
| `Ljavax/microedition/khronos/opengles/GL10;` | 既有文件改造 | Implements GL；声明 `glGetString(I)Ljava/lang/String;` 形状 |
| `Ljavax/microedition/khronos/opengles/GL10$Impl;` | 类，Implements GL10（间接含 GL） | `glGetString` handler |

命名裁决：宿主实现类用仓库既有 `$Impl` 先例（`SurfaceHolder$Impl`），
不用 AOSP 的 `com.google.android.gles_jni.*`——dex 从不按名字引用实现类
（只经 `getEGL()/getGL()` 返回值拿到），引用相等是唯一身份约束。

### 4.3 EGL façade 状态机与方法语义

状态存 `DexVmAndroidContext`（新增 `struct EglFacadeState`，跨 handler 共享；
helper 放新文件 `support_egl.cpp`，遵守 MODULE 的 shared-handler 工厂约定）：

```text
display singleton（惰性建）; initialized 标志; config singleton;
live contexts（VmObjectRef 集合 + 各自记录的 client version）;
live window surface（0 或 1 个，绑定 managed surface 的事实）;
currency{display,surface,context}（façade 层）; last_error（int）
```

方法语义（全部按 EGL 1.4 规范子集，AOSP `eglApi.cpp` 仲裁分歧）：

| 方法 | 行为 |
| --- | --- |
| `eglGetDisplay(Object)` | 入参 null（== EGL_DEFAULT_DISPLAY）→ display singleton；非 null → 返回 `EGL_NO_DISPLAY` 并记账（合法 EGL 行为 + 可查） |
| `eglInitialize(d, int[])` | 校验 d；版本数组可为 null，否则写 `{1, 4}`（façade 自身的契约版本，同 `api_level{19}` 性质的会话事实）；置 initialized |
| `eglTerminate(d)` | 清 façade 状态（context/surface 必须已全部退役，否则模型破坏→明确失败）；**不触碰宿主 surface** |
| `eglChooseConfig(d, attribs, configs, size, num)` | 解析 int[] 键值对至 `EGL_NONE`；对照 §4.4 事实表按规范匹配（size 类"至少"、mask 类子集、`EGL_DONT_CARE(-1)` 跳过）；匹配 → num=1，configs 非 null 且 size≥1 时写入 config singleton；不匹配 → num=0；**未知键 → false + `EGL_BAD_ATTRIBUTE` + 记账** |
| `eglGetConfigAttrib(d, c, attr, int[])` | 事实表内 → 写值返回 true；表外 → false + `EGL_BAD_ATTRIBUTE` + 记账（已核实 AOSP 派生 chooser 对 false 回退默认值，安全） |
| `eglCreateContext(d, c, share, attribs)` | share 必须为 EGL_NO_CONTEXT 单例或 null，否则记账失败；attribs 只认 `{EGL_CONTEXT_CLIENT_VERSION(12440), v, EGL_NONE}` 或 null，v∈{1,2} 记录为 guest 事实（宿主 ANGLE context 不变，GLES1 经既有转译）；返回新建 EGLContext 实例并登记 |
| `eglCreateWindowSurface(d, c, native_window, attribs)` | native_window 必须是 `context.surface_holders` 中的注册 holder（把 façade 拴到唯一 managed surface 上）；managed surface 未开或已有存活 window surface → 模型破坏；返回新建 EGLSurface 实例 |
| `eglDestroySurface(d, s)` | 退役 façade surface；**宿主 surface 不动**（契约） |
| `eglDestroyContext(d, ctx)` | 退役 façade context；仍为 current → 模型破坏 |
| `eglMakeCurrent(d, draw, read, ctx)` | 绑定形（draw==read 为存活 surface 且 ctx 存活）→ 记录 façade currency + **宿主 currency 迁移到调用线程**（§5）；解绑形（NO_SURFACE×2 + NO_CONTEXT）→ 清 currency + 调用线程释放宿主 currency；draw≠read 或混合形 → 记账失败 |
| `eglSwapBuffers(d, s)` | façade currency 必须在调用线程上且 s 为 current surface，否则 false + `EGL_BAD_SURFACE`；正常路径：`session->PresentManagedSurface()`（readback 发生在 GL-current 的调用线程，线程安全）→ 帧屏障停泊（§6）→ true |
| `eglGetError()` | 返回并复位 last_error（默认 `EGL_SUCCESS`） |
| `eglGetCurrentDisplay/eglGetCurrentSurface(readdraw)` | 回答 façade currency（未绑定回 EGL_NO_*） |
| `EGLContext.getEGL()` | EGL10$Impl singleton（`Singleton` helper） |
| `EGLContext.getGL()` | GL10$Impl singleton |
| `GL10.glGetString(name)` | name ∈ {GL_VENDOR/GL_RENDERER/GL_VERSION/GL_EXTENSIONS} → 经 session 新增窄访问器取当前 AngleFrame 真实字符串（与 native GLES1 `glGetString` 同源，`android_boundary_hle.cpp:141-145`）→ `MakeString`；其他 name 记账失败 |

### 4.4 config 事实表（唯一 config，全部来自宿主 surface 真实属性）

RGBA8+D24S8（`gles.angle` 已验收），一处定义、choose 与 getAttrib 共用：

```text
RED/GREEN/BLUE/ALPHA_SIZE=8  BUFFER_SIZE=32  DEPTH_SIZE=24  STENCIL_SIZE=8
SAMPLES=0  SAMPLE_BUFFERS=0  CONFIG_ID=1  LEVEL=0  CONFIG_CAVEAT=EGL_NONE
SURFACE_TYPE=EGL_WINDOW_BIT|EGL_PBUFFER_BIT
RENDERABLE_TYPE=EGL_OPENGL_ES_BIT|EGL_OPENGL_ES2_BIT   # 边界真实提供两代 API
NATIVE_RENDERABLE=0  NATIVE_VISUAL_ID=0  NATIVE_VISUAL_TYPE=EGL_NONE
TRANSPARENT_TYPE=EGL_NONE  COLOR_BUFFER_TYPE=EGL_RGB_BUFFER
LUMINANCE_SIZE=0  ALPHA_MASK_SIZE=0
```

表外属性一律 false + 记账（含 NV 12514/12515——A6 只在 Tegra 设备事实下
才请求，我们的 Build 事实不会命中该分支，已核实）。

## 5. GL currency 迁移（gles + boundary，本方案的通用地基）

**问题**：ANGLE EGL context 线程亲和。现状在 `OpenManagedSurface`（主线程，
lifecycle `Start()` 触发）创建即 current 于主线程。而自带 GLSurfaceView 的
title 在 `GLThread`（另一宿主线程）上发起全部 GL 工作——native GLES trap 在
调用方宿主线程执行（dexvm MODULE），`PresentManagedSurface` 的 readback 也
必须在 context-current 线程上。EGL 规范禁止直接在 B 线程绑定 A 线程 current
的 context（`EGL_BAD_ACCESS`），释放必须由持有线程自己做。

**设计**（全部通用，无 title 感知）：

1. gles `EglLifecycle`/`AngleFrame` 新增显式 currency API：
   `ReleaseCurrent()`（调用线程解绑）与 `BindCurrentOnCallingThread()`。
2. boundary 维护 `std::optional<std::thread::id> gl_owner_`：
   - `OpenManagedSurface`：创建 + `InitializeGuestGlDefaults()`（此时仍
     current 于打开线程）→ **随即 `ReleaseCurrent()`，owner 置空**。
   - 每个触碰 ANGLE 的入口（`RequireFrame` 与 present）检查：owner 为空 →
     绑定到调用线程并登记；owner == 调用线程 → 直通；owner 为**其他**线程 →
     记账（新条目 `gles.context_thread_affinity`）并明确失败，绝不静默抢夺。
   - guest `eglMakeCurrent` 解绑形 → 若 owner == 调用线程则 `ReleaseCurrent()`
     并清 owner（AOSP GLThread 暂停前正是这么做的，通用形态天然支持
     currency 在线程间的合法接力）。
   - `CloseManagedSurface`：无条件清 owner（持有线程可能已死；AngleFrame
     析构自会在销毁线程解绑自己的 currency，残留的死线程 currency 由
     display terminate 清理）。
3. 串行化由 `VmExecutionLock` 与"native 帧持锁"既有事实保证，owner 检查是
   诚实失败的防线，不是新锁。

**对既有 title 的影响**：intrinsic-renderer 形态（Asphalt 5）首个 GL 调用
来自主线程 → currency 落回主线程，行为逐位不变（exact 回归验证）。

## 6. present 与帧节拍

- `eglSwapBuffers` 成功 readback/publish 后，把调用 guest 线程停泊到
  **下一次宿主帧边界**（`StepFrame` 尾部发信号），每宿主帧最多消费一次 swap。
  理由：(a) exact gate 需要确定的帧序列；(b) GLThread 否则长期持有
  `VmExecutionLock`，主线程的输入/线程泵会饥饿；(c) 语义对应设备 vsync。
- 实现：`DexVmAndroidContext` 新增帧屏障（帧计数 + condvar），
  `dex_activity_lifecycle.cpp` `StepFrame()` 尾部（`frame_++` 后）唤醒；
  停泊走既有 `ReleaseForBlocking/ReacquireAfterBlocking`。
  唤醒源必须包含：帧推进、`VmThreadRuntime::Shutdown`/`RequestStop`、
  `interrupt_guest_waits`（`Stop()` 既有清场顺序，`dex_activity_lifecycle.cpp:452-473`）。
- 合法性：swap 由解释执行的 EglHelper 调用，`native_depth==0`，
  不触发 `blocking_in_native`（已核实 AOSP GLThread 在 swap 时不持
  GLThreadManager monitor）。
- 决策点（实现时验证）：若实测出现屏障与 guest monitor 的交叉死锁，退化
  方案为"publish + 让出执行锁不停泊"，并把帧序确定性问题记入任务单再议——
  不许静默换方案。

## 7. 分批执行计划（每批一次会话可完成，依赖严格向前）

### 批次 A · GL currency 显式化（gles + boundary）

- 目标：currency 从"创建线程隐式持有"变为"显式受管、可释放、可在空档绑定"。
- 触及：`src/gles/egl_lifecycle.cpp` 及其头、`src/runtime/boundary/android_boundary_hle.cpp`
  及其头、`tests/gles/egl_lifecycle_tests.cpp`、
  `tests/runtime/android_boundary_hle_tests.cpp`、两个 MODULE.md、
  `capabilities.toml`（新条目 `gles.context_thread_affinity`）。
- 测试：open 后 owner 为空；首个 RequireFrame 绑定调用线程；跨线程访问
  明确失败且记账；释放后另一线程可接力；A5 exact 回归逐位持平。

### 批次 B · 类形状、常量与 singleton（dexvm_android）

- 目标：§4.2 全部类可链接，`EGLContext.getEGL()`/`getGL()` 返回可用 Impl
  singleton，EGL_NO_* 引用相等成立；§4.3 范围内方法先以 `Unimplemented()`
  占位（记账失败，不伪造）。
- 触及：8 个新 intrinsic 文件 + `GL10.cpp` 改造 + `catalog.h/cpp`。
- 测试：`tests/session/profile_entry_scope_tests.cpp` 追加 catalog 唯一性/
  方法集合锁定；新增 `tests/dexvm/egl_facade_tests.cpp`：dexasm 夹具做
  sget 常量、引用相等、getEGL/getGL、checkcast（GL10$Impl → GL10 → GL）。
  注意验证接口静态对象字段的 sget 解析（既有先例是类不是接口）。
- 完成后可用 `run-apk --survey-gaps` 对 A6 复核静态枚举无遗漏。

### 批次 C · façade 纯状态机（不触宿主 GL）

- 目标：display/init/choose/getAttrib/createContext/destroy/terminate/
  error/currency 查询按 §4.3 语义落地；makeCurrent/createWindowSurface/
  swap/glGetString 仍占位。
- 触及：`dexvm_android.h`（EglFacadeState）、`support_egl.cpp`（新）、
  EGL10$Impl/EGLContext 声明文件、`egl_facade_tests.cpp`。
- 测试：完整 AOSP GLSurfaceView 启动序列的状态机走查（两段式 choose、
  属性回退、错误码置取、非法转移全部记账失败）；未知属性→
  `EGL_BAD_ATTRIBUTE` 路径。

### 批次 D · 宿主接线（makeCurrent 迁移、surface 绑定、present、glGetString）

- 目标：GLThread 真实驱动宿主 surface。
- 触及：EGL10$Impl/GL10$Impl 声明文件、`support_egl.cpp`、
  `android_guest_call_session.h/.cpp`（新增窄访问器：GL 字符串、
  currency 迁移/释放转发）、`egl_facade_tests.cpp`、
  `tests/runtime/android_boundary_hle_tests.cpp`。
- 测试：跨线程 makeCurrent 后 GL trap 在新线程成功；swap 触发 publish
  （帧序列号递增）；解绑后 currency 接力；模型破坏用例全部明确失败。

### 批次 E · 帧节拍 + gate 收口

- 目标：swap 停泊/唤醒接入 lifecycle；A6 首帧 gate；全量回归与记账收口。
- 触及：`dexvm_android.h`、`src/session/dex_activity_lifecycle.cpp`、
  `support_egl.cpp`、`data/scenarios/asphalt6.bootstrap.scenario.toml`、
  `capabilities.toml`（`dexvm.egl_facade` 定档、
  `profiles.asphalt6_glstore_v132` note）、`docs/state/CURRENT.md`、
  `dexvm_android/MODULE.md`。
- 验收（= DVM-31 验收）：
  1. `asphalt6.bootstrap` 取得确定首帧、无 guest fault、clean shutdown，
     三轮一致；
  2. Asphalt 5 exact 回归逐位持平（A5 走 intrinsic-renderer 路径，
     不得受任何影响）；
  3. 未实现 EGL/GL 入口记账可查、明确失败，无中性值伪装；
  4. teardown：Stop() 顺序下停泊中的 swap 被唤醒、GLThread 正常 join。

每批提交前：`cmake --preset dev && cmake --build --preset dev && ctest --preset dev`
（Windows 用 `windows-msvc` 预设）；批内触及模块同步更新 MODULE.md。

## 8. 风险与决策点（实现前先看）

1. **帧屏障 vs guest monitor 死锁**（§6 决策点）——批次 E 首先用 A6 实测，
   有证据再退化，不预防性绕路。
2. **接口静态对象字段的链接路径**——批次 B 第一个测试就验证 sget 经
   intrinsic 接口的解析；若 linker 不支持，先补 linker（属于通用能力，
   非 façade 特有）。
3. **exact gate 的跨线程确定性**——DVM-30 已在真实线程下取得三轮逐字一致，
   swap 屏障进一步量化 GLThread 进度；若首帧 hash 漂移，优先检查屏障前后
   的锁交接顺序，不许放宽 gate。
4. **`eglInitialize` 版本 {1,4}** 是 façade 契约事实（性质同 `api_level{19}`）；
   若后续 title 查询 `eglQueryString(EGL_VERSION)`，届时接真实 ANGLE 值，
   本期不实现（记账失败）。
5. **GLThread 异常死亡后的 currency 残留**——批次 A 的 CloseManagedSurface
   无条件清 owner 已覆盖；补一条 teardown 测试。

## 9. 明确不做（记账即可）

- `eglCreatePbufferSurface/eglCreatePixmapSurface/eglCopyBuffers/
  eglQueryContext/eglQueryString/eglQuerySurface/eglGetConfigs/
  eglReleaseThread/eglGetCurrentContext`：接口留形状，Impl `Unimplemented()`。
- EGL11、GLU、GL11 扩展面：不声明，命中即链接失败并有清晰报错。
- share context、多 window surface、多 display：模型破坏路径。
- GL10 除 `glGetString` 外的方法：dex 侧 GL 绘制不存在于这代 title
  （native 直接走 GLES trap）；命中即按缺声明明确失败，后续按命中批次补。
