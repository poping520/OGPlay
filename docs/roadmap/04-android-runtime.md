# 04 · Android 运行时层（Bionic / Syscall / JNI / DEX）

本篇回答需求 3.3、3.4、3.5。

---

## 1. 范围边界：什么叫"够用的 Android"

这是整个项目最重要的一条线。**越界一步就变成 Android 模拟器，永远做不完。**

| 做 | 不做 |
| --- | --- |
| Bionic libc/libm/libdl（三个 API 版本） | ART/Dalvik 完整虚拟机（见第 7 节的分级方案） |
| 游戏用到的 Linux syscall（约 120 个） | binder / IPC / system_server / Zygote |
| 完整 JNI（JNIEnv + JavaVM 全表） | Android 框架的完整 Java 实现 |
| 游戏会碰的框架类（Activity/AssetManager/AudioTrack/…）的 HLE | PackageManager / ActivityManager / 四大组件生命周期全套 |
| EGL / GLES 1.1 / GLES 2.0 / GLES 3.x | Vulkan on Android、RenderScript |
| OpenSL ES、AudioTrack、MediaPlayer | AudioFlinger、MediaCodec 全格式 |
| NativeActivity / ALooper / AInputQueue / AAssetManager | Wi-Fi/蓝牙/电话/通知/权限系统 |
| SurfaceFlinger 的**接口形状**（够 EGL 用） | 真正的 SurfaceFlinger 合成器 |

判断准则：**"这个游戏进程内会直接调用它吗？"** 会 → 做；只有系统服务会调 → 不做。

---

## 2. 加载器

### 2.1 包解析

需要支持的输入形态（老游戏分发方式很杂）：

- 单 APK
- APK + OBB（`main.<ver>.<pkg>.obb` / `patch.*`）
- Split APK / XAPK / APKM / APKS
- 已解包的目录

解析产出：`AndroidManifest.xml`（二进制 XML）→ package、versionCode、minSdk/targetSdk、
NativeActivity 声明、`android:debuggable`、`libraryName` 等；`lib/<abi>/` 列表；`assets/`；`res/`。

### 2.2 ELF 与动态链接

- 支持 `armeabi-v7a`（首要）与 `arm64-v8a`（次要，见 [01](01-architecture.md) 的 CPU 章节）
- 完整重定位类型覆盖，而非按需补
- `DT_NEEDED` 递归解析，构造真正的**guest 侧链接命名空间**：guest 库之间正常互链，
  只有触到"HLE 边界库"时才落到宿主实现
- TLS（`__aeabi_read_tp` / TPIDRURO）、`PT_ARM_EXIDX`、`init_array`/`fini_array`、
  `DT_INIT`/`DT_FINI`、符号版本（`DT_VERSYM`）
- `dlopen`/`dlsym`/`dladdr` 走同一套链接器，游戏动态加载插件时才不会崩

---

## 3. Bionic 策略：加载真实系统库

### 3.1 两条路线

| | A：宿主重实现 libc | B：加载真实 AOSP Bionic |
| --- | --- | --- |
| HLE 面 | ~1500–2000 个 libc 函数 | ~120–150 个 syscall |
| ABI 保真度 | 每个函数都可能有细微偏差 | 天然正确 |
| 多版本支持（4.4/5.1/7.1） | 每个版本都要人工核对差异 | **换一组 .so 即可** |
| 性能 | 快（宿主原生） | 慢（走 CPU 翻译） |
| 可调试性 | 好 | 栈更深，但有符号 |

DEMO 阶段走的是 A，并且已经暴露出问题：libc 行为的细节偏差会让 guest 走进错误分支，
而且完全静默。

### 3.2 决策：混合，以 B 为主

**默认加载真实 Bionic，对少数函数做选择性拦截。**

```
                    guest 调用 malloc / memcpy / pthread_create / open
                                    │
                        ┌───────────┴───────────┐
                        │  拦截表（少而精）      │
                        └───────────┬───────────┘
                  命中 │                       │ 未命中
                        ▼                       ▼
              宿主原生实现              真实 Bionic（CPU 翻译执行）
           memcpy/memset/strlen…                 │
                                                  ▼
                                      syscall 层（clone/futex/TLS/
                                      mmap/open → VFS）→ 宿主真线程
```

拦截清单（初版）：

| 类别 | 拦截理由 |
| --- | --- |
| `memcpy/memset/memmove/strlen/strcmp` 等 | 纯性能。宿主 SIMD 比翻译后的 ARM 快一个量级 |
| `pthread_*` | 保持真实 Bionic 内部 ABI；在 clone/futex/TLS syscall 边界映射宿主真线程 |
| `malloc` 族 | 可选。真实 Bionic 的 dlmalloc/jemalloc 能跑，但拦截后更好观测 |
| `open/read/...` | 实际拦在 syscall 层而非 libc 层，统一走 VFS |
| `__android_log_*` | 直接进宿主结构化日志 |

**收益**：三个 Android 版本 = 三组预置 `.so`，不需要为每个版本重写 libc；
libc 的 locale、stdio 缓冲、`snprintf` 边界、`qsort` 稳定性这类细节自动正确。

### 3.3 内置三个版本

| 目标 | API Level | 代号 | 用途 |
| --- | --- | --- | --- |
| Android 4.4 | 19 | KitKat | 2011–2014 的绝大多数老游戏 |
| Android 5.1 | 22 | Lollipop | ART 时代早期、64 位起点 |
| Android 6.0 | 23 | Marshmallow | 覆盖 5.x 之后的 ABI 与运行库变化 |

每个版本内置：`libc.so` `libm.so` `libdl.so` `libstdc++.so` `libz.so`。
均来自 AOSP（Apache-2.0，可再分发；**发布前需完成许可证与来源标注**）。

### 3.5 系统库的来源与校验

每个版本的系统库必须建立**来源清单**：文件路径、体积、SHA-256、来源构建指纹。
理由有二：一是这些库参与 ABI 行为判定，版本混淆会产生极难排查的问题；
二是再分发需要可追溯的许可证来源。

DEMO 阶段已经验证过这套做法可行——从一台 root 的 Android 4.4.4 (API 19, `armeabi-v7a`)
设备上取下过 `linker` / `libc.so` / `libm.so` / `libdl.so` / `liblog.so` / `libstdc++.so`
并记录了哈希，可直接作为 API 19 这一档的基线。

清单格式：

```toml
[bionic.api19]
android_version = "4.4.4"
abi = "armeabi-v7a"
source = "AOSP prebuilt"      # 发布版本必须用 AOSP 产物，不用设备提取物

  [[bionic.api19.file]]
  path = "lib/libc.so"; size = 310660; sha256 = "f2f2b0…"
```

> 注意区分：**设备提取物只能作为开发期的 ABI 行为参照（oracle），不能进发行包。**
> 发行包里的库必须来自 AOSP 源码构建产物，许可证与来源才清晰。

### 3.4 HLE 边界库（这些必须是我们自己的）

以下库**不能**用 AOSP 版本，因为它们的真身依赖驱动/系统服务，必须由宿主实现：

```
libEGL.so  libGLESv1_CM.so  libGLESv2.so  libGLESv3.so
libOpenSLES.so   libandroid.so   libjnigraphics.so   liblog.so
libmediandk.so（可选）
```

实现方式：生成 guest 侧的**导出符号桩**，调用即陷入宿主。这套边界要用**声明式的
IDL 表**生成，而不是手写几千个 lambda——DEMO 阶段手写 GL 桥已经证明这条路会失控。

---

## 4. Syscall 层

### 4.0 现状：完全不存在

当前**没有任何 syscall 模拟**——没有 `__NR_*` 表、没有 futex、没有按号分派。
唯一被接受的 `svc` 是自定义的 HLE 陷阱魔数（`svc #0x5448c`），**任何其他 `svc` 立即致命错误**。
`syscall()` / `ioctl()` / `fcntl()` 作为 libc 符号存在，但一律返回 `-1`。

这有两层含义：

1. 任何绕过 libc 直接发系统调用的 guest 代码（真实 Bionic 内部到处都是）现在必然崩溃
   —— 这也正是当前不得不重实现整个 libc 的原因；
2. 反过来说，**syscall 层是一块完全的空地，没有历史包袱**，可以按正确的设计一次做对。

真实 Bionic 会发 `svc #0`，我们接住它。

### 4.1 需要覆盖的组

| 组 | 代表 | 备注 |
| --- | --- | --- |
| 内存 | `mmap2` `munmap` `mprotect` `madvise` `brk` `mremap` | 直接操作 guest MMU |
| 文件 | `openat` `read` `write` `pread64` `lseek` `close` `fstat64` `getdents64` `unlink` `mkdirat` `faccessat` `readlinkat` `fcntl64` `statfs` | 全部经 VFS |
| 线程 | `clone` `futex` `gettid` `set_tls` `exit` `exit_group` `tkill` `sched_yield` `sched_getaffinity` | **最关键，见 4.3** |
| 时间 | `clock_gettime` `nanosleep` `gettimeofday` `timer_*` | 需接入统一时钟（可暂停/加速） |
| 信号 | `rt_sigaction` `rt_sigprocmask` `sigaltstack` | 最小实现，够 C++ 异常和 crash handler 用 |
| 进程 | `getpid` `getuid` `getgid` `prctl` `sysinfo` `uname` | 返回一致的假身份 |
| 轮询 | `poll` `ppoll` `epoll_*` `eventfd` `pipe2` | ALooper 依赖 |
| 网络 | `socket` `connect` `sendto` `recvfrom` `getaddrinfo` 路径 | **默认离线**，可选放行 |
| ARM 私有 | `ARM_cacheflush` `ARM_set_tls` | 必需 |
| ioctl | `/dev/ashmem` | 少量老游戏用；binder 明确不做 |

### 4.2 未实现 syscall 的处理

**绝不静默返回成功。** 未实现 → 记账（`hle.unimplemented()` 可查）+ 返回 `ENOSYS` +
可选中断。DEMO 阶段"假装成功"导致 guest 走进更深的错误路径，现场离根因越来越远，
这是明确要避免的反模式。

### 4.3 线程模型：必须是真线程

DEMO 阶段是**协作式单线程调度器**，而且同步原语并不完整——实测：

```cpp
Register("pthread_join",           [](Registers& r) { r[0] = 0; });  // 不等待，直接返回
Register("pthread_cond_signal",    [](Registers& r) { r[0] = 0; });  // 不唤醒任何人
Register("pthread_cond_broadcast", [](Registers& r) { r[0] = 0; });  // 同上
```

`pthread_join` 立刻返回成功而不等待目标线程；条件变量的唤醒是空操作。
目前之所以还能跑，是因为协作式调度下时序碰巧对得上——**这不是能力，是运气**。

正式版必须改成 **1 个 guest 线程 = 1 个宿主线程**，每个线程一份独立 JIT 上下文，
`futex` 映射到宿主同步原语。

理由：

- 老游戏普遍有音频线程、资源加载线程、逻辑线程；协作式调度会在时序敏感处出错。
- 同类项目 Bogodroid 在 Unity 上的失败点正是 "Crashes when Mono creates first thread.
  Pthread bridge needs to be reworked"——线程模型是这类项目最常见的硬伤。
- 协作式调度让"卡死"和"死锁"无法区分，调试成本极高。

需要配套：guest 内存的多线程安全（原子/独占访问监视器已有）、per-thread TLS、
线程创建/退出的完整生命周期、以及**确定性模式**（调试时可退化为单线程串行，供复现用）。

---

## 5. JNI：完整实现

`JNIEnv` 有 **231 个**函数槽。老游戏用得很散，缺一个就是一次崩溃或一次静默失败。

### 5.0 现状（实测）：这是当前最大的静默失败源

| 指标 | 实测值 |
| --- | --- |
| `JNIEnv` 槽位总数 | 231 |
| **有真实逻辑的** | **50（22%）** |
| **被自动填成"返回 0"的** | **181（78%）** |
| `JavaVM` 有逻辑的槽位 | 2（`AttachCurrentThread`、`GetEnv`），其余 229 个同样返回 0 |

初始化时有一个兜底循环，把所有未显式实现的槽位统一填成
`jni::reserved_slot_N`，行为是 `registers[0] = 0`。

**这正是本项目反复吃亏的那个反模式**：游戏调用 `GetIntField` 拿到 0、调用
`ExceptionOccurred` 拿到"无异常"、调用 `NewObjectArray` 拿到空指针——全部不报错，
错误在几百帧之后以完全无关的形式爆出来。

当前缺失的重点类别：

- **字段访问**：`GetIntField` 等全部 `Get*Field` 实例读、以及**所有** `Set*Field` 写
- **异常**：`ExceptionOccurred`、`Throw`、`ThrowNew`
- **对象数组**：`NewObjectArray`、`Get/SetObjectArrayElement`
- **监视器**：`MonitorEnter` / `MonitorExit`
- **局部帧**：`PushLocalFrame` / `PopLocalFrame` / `EnsureLocalCapacity`
- **直接缓冲区**：`NewDirectByteBuffer` / `GetDirectBufferAddress`
- **反射**：`FromReflectedMethod` 等全族
- `Call*MethodA` 变体、多数 `CallNonvirtual*`

**结论：一次性把全表实现完，不要按需补。** 这是少数"一把梭比增量更省"的模块。
**并且在完成之前，未实现槽位必须改成"记账 + 显式报错"，绝不能静默返回 0。**

关键子系统：

1. **引用表**：Local / Global / WeakGlobal 三张表，带容量上限与溢出诊断
   （真机会在超限时报错，游戏可能依赖这个行为）
2. **异常状态机**：`Throw` / `ExceptionCheck` / `ExceptionClear` / `ExceptionDescribe`，
   以及"pending exception 时禁止大部分 JNI 调用"的规则
3. **签名解析器**：完整 JNI 描述符解析，支撑 `GetMethodID` 与三种调用变体（`...`/`V`/`A`）
4. **基本类型数组**：`Get/Release<Type>ArrayElements`、`GetPrimitiveArrayCritical`、
   `Get/SetArrayRegion`
5. **字符串**：`NewStringUTF` / `GetStringUTFChars` / `GetStringCritical`，
   **注意 Java 的 Modified UTF-8 与标准 UTF-8 的差异**（老游戏的中文/日文文本会踩）
6. **`RegisterNatives`**：游戏 Java 层注册 native 方法的主路径
7. **`AttachCurrentThread`**：native 线程回调 Java 必经之路

---

## 6. Java 框架层：声明式 HLE

DEMO 阶段的做法是为每个游戏手写假 Java 类（`Java_com_gameloft_..._nativeRender` 之类），
这是硬编码的主要来源。正式版改成**声明式绑定表**：

```toml
[[class]]
name = "android/media/AudioTrack"
impl = "audio.track"                    # 宿主实现 id

  [[class.method]]
  name = "<init>"; sig = "(IIIIII)V"; impl = "audio.track.ctor"
  [[class.method]]
  name = "write";  sig = "([BII)I";   impl = "audio.track.write"
```

- 框架类（`android.*` / `java.*`）的绑定表内置，**所有游戏共用**
- 游戏自有类（`com.gameloft.*`）的绑定进各自的 Title Profile（见 [05](05-title-profiles.md)）
- 找不到类/方法时：记账 + 明确报错，可通过 `hle.unimplemented()` 查询

框架侧优先级最高的几类：`Activity` / `AssetManager` / `AudioTrack` / `MediaPlayer` /
`SoundPool` / `SharedPreferences` / `Display` / `Vibrator` / `Sensor` /
`ConnectivityManager` / `Locale` / `PackageManager`（仅 versionName/versionCode）。

---

## 7. DEX 执行：分级方案（需求 3.5）

### 7.1 先说结论

**老游戏是否需要 DEX 执行，取决于游戏用的引擎，而不是取决于年代。**

| 引擎 | Java 层厚度 | 是否需要 DEX |
| --- | --- | --- |
| Gameloft / EA / Glu 自研 C++ 引擎 | 极薄 | 否 |
| Unreal Engine 3 | 薄 | 否 |
| Cocos2d-x | 薄 | 否 |
| Unity 3.x/4.x（Mono/IL2CPP） | 薄（游戏逻辑在 Mono/native） | 否 |
| Corona / Solar2D（Lua） | 薄 | 否 |
| **libGDX** | **厚，游戏逻辑就在 Java** | **是** |
| **AndEngine** | **厚** | **是** |
| 纯 Java 休闲游戏 | 全部 | **是** |

也就是说：**3D/动作类老游戏大多不需要；2D 独立/休闲游戏有相当比例需要。**

### 7.2 不要现在就决定，要先测量

建议先做一个**静态分类工具**（成本低，一两天），对目标题库批量扫描输出：

- 有无 `lib/*/*.so`，ABI 是什么
- 引擎指纹（按 `.so` 名与特征符号：`libgdx.so`、`libcocos2dcpp.so`、`libunity.so`、
  `libUE3.so`、`libmono.so` …）
- DEX 中非框架类的数量与方法数
- Java 侧是否存在实质渲染/逻辑（启发式：`onDrawFrame` 的字节码规模、
  `View.onDraw` 是否被重写）
- native 方法声明数量（`native` 修饰符计数）

产出一张"题库 DEX 依赖度分布图"，**用数据决定要不要投入 DEX VM**。

### 7.3 如果要做：分三级

| 级别 | 能力 | 成本 | 覆盖 |
| --- | --- | --- | --- |
| **L0 · 无 DEX** | 完全靠 HLE Java 类 | 已有 | 薄 Java 层的 NDK 游戏 |
| **L1 · DEX 只读** | 解析 DEX，读常量/资源/类结构，不执行 | 小 | 提升识别与自动绑定质量 |
| **L2 · DEX 解释执行** | 解释游戏自有类；`android.*` / `java.*` 仍走宿主实现 | 大 | Java 厚层游戏 |

**L2 的关键架构**（这是重点）：

> 执行游戏的 DEX，但把 `android.*` 和 `java.*` 当作**内建类（intrinsic）**由宿主实现，
> 而不是去跑真实的 AOSP framework.jar。

这样 VM 只需要跑游戏自己那几百个类，规模可控；框架的复杂度仍然被 HLE 边界挡在外面。
反之，如果去加载真实 framework.jar，就等于在做 ART + Android 系统，项目会失控。

L2 需要的组件：DEX 解析器、字节码解释器（约 220 条指令）、对象模型与 GC、
异常表、`invoke-*` 分派、与 JNI 的双向互通、内建类库（`String`/`StringBuilder`/
集合/`Math`/`Thread`/IO 等最小集）。**没有 JIT，纯解释器即可**——Java 层不是这类游戏的热点。

### 7.4 建议节奏

- M0–M4：只做 **L1**（解析，不执行）。它对识别、自动绑定、Title Profile 生成本来就有用。
- M4 之后用 7.2 的数据复盘：如果需要 DEX 的题目占比 < 10%，L2 延后甚至不做；
  如果 > 25%，把 L2 提到一个独立里程碑。

**不要在架构上把 L2 堵死**：JNI 层与对象模型从一开始就要设计成"对象引用可以是宿主对象
也可以是 VM 对象"的双实现形态，这样 L2 是加法而不是重写。

---

## 8. 本篇产出的验收标准

- [x] 三个 API 版本的 Bionic 均能完成 `libc` 自检并跑通无界面 NDK `.so` 契约样本
- [x] syscall 覆盖率账本建立，未实现 syscall 100% 可被 `hle.unimplemented()` 观测
- [ ] JNIEnv 全表实现，签名解析器通过完整描述符测试集
- [ ] 多线程：Unity/Mono 类样例能创建线程并正常运行（对标 Bogodroid 的失败点）
- [ ] DEX L1 解析器可对题库输出引擎指纹与 Java 厚度报告
