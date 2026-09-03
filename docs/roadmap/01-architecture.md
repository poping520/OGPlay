# 01 · 总体架构与运作原理

本篇回答需求 3.1。

---

## 1. 一句话原理

> **游戏的 ARM 机器码由 CPU 翻译器执行；它以为自己在 Android 上，实际上它每一次
> 系统调用、每一次 JNI 调用、每一次 OpenGL 调用，都被我们用宿主的原生实现接住了。**
>
> 我们不模拟 Android，我们**扮演** Android。

这就是 HLE（高层模拟）与 LLE（低层模拟）的区别：BlueStacks 那类产品是在虚拟机里跑
一整个 Android 系统；我们只翻译 CPU 指令，其余全部换成 Windows/Linux/macOS 的原生等价物。
代价是兼容性要一个一个 API 攒出来，收益是启动快、开销小、可深度调试、可跨平台。

---

## 2. 分层架构

```
┌────────────────────────────────────────────────────────────────┐
│  前端                                                           │
│ GUI(ImGui) · CLI · Agent Control Server(JSON-RPC) · MCP Adapter│
├────────────────────────────────────────────────────────────────┤
│  会话编排 Session                                                │
│  题库/Title Profile · 存档 · 配置 · 输入映射 · 兼容性上报          │
├────────────────────────────────────────────────────────────────┤
│  Android 运行时（HLE）                                           │
│  ┌──────────────┬──────────────┬──────────────┬──────────────┐ │
│  │ 包/加载器     │ 运行时桥      │ 框架 HLE      │ 内核 HLE      │ │
│  │ APK/OBB/ELF  │ JNI · DEX(可选)│ Activity      │ syscall       │ │
│  │ linker/reloc │              │ AssetManager  │ futex/mmap    │ │
│  │              │              │ Audio*/Sensor │ fd/VFS        │ │
│  └──────────────┴──────────────┴──────────────┴──────────────┘ │
│  真实 Bionic（libc/libm/libdl，按 API 版本）—— 跑在 guest 侧      │
│  HLE 边界库（libEGL/libGLESv*/libOpenSLES/libandroid）—— 陷入宿主  │
├────────────────────────────────────────────────────────────────┤
│  Guest 执行层                                                    │
│  CPU JIT(A32/T32/A64) · 内存与 MMU · 线程与调度 · 统一时钟        │
├────────────────────────────────────────────────────────────────┤
│  宿主抽象层 HAL                                                  │
│  窗口 · 图形(ANGLE) · 音频 · 输入 · 文件 · 视频解码 · 线程 · 时间   │
├────────────────────────────────────────────────────────────────┤
│  Windows        │        Linux        │        macOS            │
└────────────────────────────────────────────────────────────────┘
```

**依赖方向严格单向向下。** 上层可以调用下层，下层不得反向依赖。
唯一的例外是回调，且必须通过显式接口而非直接引用。

---

## 3. 从双击到出画面：完整流程

```
 1. 识别
    APK/XAPK → 解析 AndroidManifest → package/versionCode/minSdk/ABI
    → .so 指纹 → 匹配 Title Profile（题库）→ 决定 API 版本与 quirks

 2. 装配数据
    APK assets + OBB + 外置目录 → 组装成该游戏的虚拟 sdcard（只读挂载，不复制）

 3. 建立 guest 地址空间
    预留 4 GiB 宿主区域作为 32 位 guest 的线性地址空间
    → 栈 / 堆 / TLS / HLE 桩区

 4. 加载与链接
    加载对应版本的 Bionic（libc/libm/libdl）
    → 加载游戏 .so 及其 DT_NEEDED 依赖
    → 应用重定位；遇到 HLE 边界库的符号则绑定到宿主桩
    → 建立 exidx / init_array / TLS

 5. 启动运行时
    跑 init_array（C++ 全局构造）→ JNI_OnLoad
    → 建立 JNIEnv/JavaVM 与 Java 框架绑定表

 6. 生命周期
    onCreate → surfaceCreated → onResume → 首次 EGL 上下文创建

 7. 主循环（每帧）
    输入映射 → 注入触摸/传感器事件
    → 调用 guest 渲染回调（NativeActivity 或 GLSurfaceView.Renderer）
    → guest 发出 GLES 调用 → 翻译到 ANGLE → 宿主 GPU
    → present（统一时钟控制节奏）

 8. 并行
    guest 的音频线程 / 加载线程 = 宿主真实线程，各自持有独立 JIT 上下文
```

---

## 4. 关键设计决策

### 4.1 图形：用 ANGLE，不要自己写 GLES→桌面 GL

**这是本项目最重要的一个技术决策。**

DEMO 阶段的做法是手写把 GLES 调用映射到 macOS 的 OpenGL 4.1 core profile。
它带来的问题在实践中已经全部暴露：

- client-side vertex array 在 core profile 非法，要自己做 staging（已踩）
- 扩展字符串、能力上报要自己编，编错了游戏就静默降级（**本次菜单缺失的根因**）
- NPOT、纹理完整性、精度限定符等 GLES 语义与桌面 GL 有大量细微差异
- macOS 的 OpenGL 已废弃且停在 4.1，Windows/Linux 又各是一套

改用 **ANGLE**（Google，Chrome/WebGL 在用）：

| | 覆盖 |
| --- | --- |
| 前端 | GLES 1.1（基于 ES3.0 前端实现）· GLES 2.0 · 3.0 · 3.1（3.2 进行中） |
| 后端 | Vulkan · Metal · D3D11 · D3D9 · 桌面 GL |
| 平台 | Windows / Linux / macOS 全覆盖 |

收益：

1. **GLES 语义由 ANGLE 保证**，我们不再需要理解两套 API 的差异；
2. **一次实现，三平台通吃**；
3. **CI 可用 SwiftShader/llvmpipe 软件后端跑黄金帧测试**，无 GPU 也能回归；
4. GLES 1.x 固定管线（很多 2010 年前后的老游戏在用）直接得到支持，
   不需要自己写固定管线→着色器的转换。

我们自己只需要保留一层很薄的 `libGLESv*.so` 桩：把 guest 的调用参数从 guest 内存
搬到宿主，然后转调 ANGLE。这一层**必须由 IDL 表生成**，不能手写——DEMO 阶段
`gl_bridge.cpp` 已经手写到 2828 行，还远未覆盖完整 GLES2。

### 4.2 内存：直接映射，不要逐次访问回调

DEMO 用的是"每次 guest 内存访问都回调到 C++"的 soft-MMU。正确但慢。

正式版：32 位 guest 在 64 位宿主上，**预留一块 4 GiB 的连续宿主虚拟地址空间**，
guest 地址 `A` 直接对应宿主地址 `base + A`。JIT 生成的访存指令可以直接寻址，
零开销。权限由宿主 `mprotect`/`VirtualProtect` 施加，越界访问变成宿主的
SIGSEGV/SEH，再转换成 guest 的 fault。

保留 soft-MMU 作为**调试模式**（可下断点、可记录访问、可做 watchpoint）。

### 4.3 CPU：Dynarmic + 解释器双后端

- 继续用 Dynarmic（DEMO 已在用，社区验证充分，x86_64/arm64 后端齐全）
- 必须支持 **A32 + Thumb/Thumb-2**（老游戏大量 Thumb 代码）
- **AArch64 要纳入规划**：2015 年之后的"老游戏"有 `arm64-v8a`，且部分只有 64 位
- **每个 guest 线程一个独立 JIT 实例**
- 配一个**解释器后端**：慢，但可单步、可对拍。JIT 出问题时用它二分定位

### 4.4 线程：真线程，不是协作式调度

见 [04 · 4.3 节](04-android-runtime.md)。这是 DEMO 与正式版的关键分水岭之一。

### 4.5 时间：统一时钟

所有时间源（`clock_gettime`、`nanosleep`、帧节拍、音频时钟）走同一个 `Clock` 对象，
支持：实时 / 固定步长（确定性）/ 快进 / 暂停。

这是**确定性回放**和**自动化测试**的前提，也是"快进"这个用户功能的实现基础。
必须在设计期就做，后补要改遍全部时间调用点。

### 4.6 状态可序列化（为即时存档预留）

CPU 寄存器、guest 内存、HLE 对象表、GL 资源镜像，从一开始就设计成可快照的。
即时存档对老游戏体验提升极大，但如果不在设计期考虑，后期几乎无法补上。

---

## 5. 从 DEMO 到正式版：差距盘点

DEMO 现状（实测）：**17,146 行 / 48 个文件**。

| 模块 | 行数 | 说明 |
| --- | --- | --- |
| `src/hle` | 4,237 | libc/JNI/dispatcher |
| `src/app` | 3,925 | 入口 + 各游戏专用帧循环 |
| `src/graphics` | 3,000 | GL 桥 |
| `src/audio` | 1,319 | OpenSL + Java 音频 + CoreAudio |
| `src/cpu` | 948 | Dynarmic 封装 |
| `src/elf` | 571 | ELF 解析与重定位 |
| `src/guest` | 363 | 内存与分配器 |
| `src/apk` | 304 | zip/APK |
| `tests` | 987 | 31 个单元测试 |

现有 HLE 面（实测）：

| 子系统 | 已绑定 | 有实质实现 | 桩/空操作 |
| --- | --- | --- | --- |
| Bionic / libc | ~289 个符号 | ~247 | ~42 |
| `JNIEnv` | 231 槽（无 null） | **50（22%）** | **181 静默返回 0** |
| `JavaVM` | 231 槽 | 2 | 229 |
| GL / EGL | 153 个入口 | 视模式而定 | GLES1 的 39 个默认是空操作 |
| OpenSL ES | 43 个 vtable 槽 | ~22 | ~21 |
| **内核 syscall** | **0** | **0** | 无 syscall 层 |
| CPU 架构 | A32 + Thumb | — | **无 arm64** |

**结构性问题（这决定了重构策略）**：

1. **三个巨型文件占了 56% 的代码**：
   `run_main.cpp` 3727 行、`dispatcher.cpp` 3037 行、`gl_bridge.cpp` 2828 行。
   对 AI 开发尤其致命——单个文件就吃掉大半上下文窗口。
   → 拆分成按 API 域组织的小文件，是 M0 的强制任务。
2. **`run_main.cpp` 里有 4 条独立的游戏专用帧循环**（Asphalt 6 / Dungeon Hunter /
   Asphalt 5 / Tales From Deep Space），合计约 1700 行，是硬编码的主要聚集地
   （见 [05](05-title-profiles.md)）。**注意投入产出：写了四条路径，
   目前只有 Asphalt 6 能进主界面，Tales From Deep Space 至今未跑通。**
3. **大量"静默返回 0"的桩**：181 个 JNI 槽、39 个 GLES1 函数、`pthread_join`、
   `pthread_cond_signal/broadcast`、`munmap`/`mprotect`。这是最危险的一类债务——
   它们不报错，只是让错误在很远的地方以无关的形式爆出来。
4. **测试薄**：987 行测试、31 个用例对 17k 行代码，且没有画面级回归。
   `a32_cpu`、`gl_bridge`、`fake_jni`、`run_main` 完全没有单元测试。
5. **平台绑定**：CGL 无头上下文、CoreAudio/AudioUnit、AVFoundation、
   `mach-o/dyld.h` —— 跨平台需要先抽 HAL。
6. **80 个环境变量**（其中 15 个是每游戏兼容开关）：配置与调试手段的熵已经很高。
7. **无 arm64**：APK 的 ABI 选择直接跳过 `arm64-v8a`，64 位独占的游戏加载不了。
8. **无日志系统**：275 处裸 `std::cerr`/`printf`，没有级别、分类、帧号、限流。
   排查靠"加日志 → 重跑 90 秒 → 再看"的循环（见 [09](09-logging.md)）。

**结论：正式版是重写内核 + 移植资产，不是在 DEMO 上继续长。**
DEMO 的真正价值不是代码，而是**已经验证过的知识**——哪些 API 必须实现、
哪些坑长什么样。这些知识已提炼成下一节的设计约束。

---

## 6. 已知失效模式（设计约束）

以下每一条都是 DEMO 阶段**实际踩过**的坑。它们不是"注意事项"，而是新架构必须从结构上
消除的失效模式——每条后面标注了正式版由谁负责解决。

### 6.1 图形

| 失效模式 | 现象 | 正式版对策 |
| --- | --- | --- |
| 扩展串未以空格结尾 | 老引擎的分词器只在读到分隔符时提交 token，**最后一个扩展被静默丢弃**；真机驱动习惯带结尾空格，故真机不复现 | ANGLE 提供真实 GLES 语义（§4.1） |
| 能力上报不完整 | 少报一个扩展，引擎**安静地降级**到另一条代码路径，不报错 | 同上；能力上报不再手写 |
| GLES2 允许 client-side 数组，桌面 core profile 不允许 | `glDrawElements` 报 `0x502` | 同上 |
| 纹理状态调用被注册成 no-op | 默认 mipmap 过滤 + 未生成 mipmap = incomplete texture，表现为黑屏/白块，**且不报 GL error** | 禁止 no-op 桩（§6.4） |
| `glFlush()` 被当作 `Present()` | 一帧内多次 swap，画面在两张完整图之间闪烁 | GL 边界语义由 IDL 表定义，不手写 |
| RTT 失败后内容画到屏幕 FBO 再被覆盖 | UI 元素完全不显示，但 draw call 数正常 | `gpu.render_targets()` 可查（[03](03-agent-interface.md)）；题库断言 `draw_fbos_min` |

### 6.2 内存与地址

| 失效模式 | 现象 | 正式版对策 |
| --- | --- | --- |
| 低地址兼容窗口静默返回 0 | guest 读到全 0，走进"缺省分支"，无任何报错 | 兼容窗口必须计数上报；限定到具体游戏实例，不全局开 |
| 空页兼容吞掉空指针虚调用 | 功能整块消失，无报错 | quirk 必须配套 `hle.null_calls()` 计数（[05 · 4.3](05-title-profiles.md)） |
| 老 ARM 二进制含旧 Android 加载布局的绝对指针 | 指向 `0x01000000–0x02000000` 等未映射区 | 当作"缺失的可选参数"处理，**不要扩大映射来掩盖** |

### 6.3 执行与线程

| 失效模式 | 现象 | 正式版对策 |
| --- | --- | --- |
| `PT_ARM_EXIDX` 未暴露给 unwinder | C++ 异常直接 terminate | 加载器完整支持 exidx（[04 · 2.2](04-android-runtime.md)） |
| 渲染线程与 worker 复用 TLS 槽位 | 状态互相污染，症状随机 | 每 guest 线程独立 stack/TLS（[04 · 4.3](04-android-runtime.md)） |

### 6.4 最重要的一条：HLE 函数不要"假装成功"

**返回假的成功值会让 guest 走进更深的错误路径，现场离根因越来越远。宁可明确失败。**

这条是前面几乎所有难题的共同根因，也是当前技术债最集中的地方
（181 个 JNI 空桩、39 个 GLES1 空操作、`pthread_join` 空实现）。新架构的硬性要求：

- 未实现的东西**必须记账**（[02 · 7](02-ai-workflow.md) 能力账本）
- 未实现的东西**必须可查**（[03 · 3.6](03-agent-interface.md) `hle.unimplemented()`）
- 未实现的东西**不得返回成功**

---

## 7. 补充设计要点

### 7.1 Guest 地址布局

固定布局便于调试时一眼判断地址属于哪个区域。建议：

| Guest 地址范围 | 用途 |
| --- | --- |
| `0x00000000-0x0000ffff` | Null guard / 未映射 |
| `0x10000000-0x17ffffff` | 主 ELF 映像 |
| `0x18000000-0x1fffffff` | Guest 系统库（Bionic 等） |
| `0x20000000-0x5fffffff` | Heap / 匿名 mmap |
| `0x70000000-0x70ffffff` | HLE thunk 区 |
| `0x71000000-0x71ffffff` | JNI / OpenSL vtable 与常量对象 |
| `0xa0000000-0xafffffff` | 文件映射与资源 staging |
| `0xb0000000-0xbfffffff` | Guest 线程栈 |
| `0xffff0000-0xffffffff` | kuser 辅助向量区 |

最终布局由 Loader 固定后写入模块契约。

### 7.2 AAPCS32 ABI 桥接

宿主与 guest 之间每一次调用都要过这一层，必须完整：

- `r0-r3` + 栈参数、64 位参数对齐、`r0/r1` 64 位返回值、结构体指针
- **softfp** 浮点外部调用约定
- guest 函数指针回调（宿主调回 guest）

**guest 指针必须强类型包装**，禁止直接当宿主指针解引用：

```cpp
template <typename T> struct GuestPtr { std::uint32_t address; };
```

禁止：把 guest 指针直接转成宿主指针；在宿主对象里长期保存未验证的 guest span；
在回调期间跨线程使用未固定的 guest buffer。

### 7.3 Varargs 必须自己解析

不能把 guest 的 `va_list` 直接交给宿主 libc——布局不兼容。
`printf` / `fprintf` / `sprintf` / `snprintf` / `vsprintf` / `vsnprintf` /
`sscanf` / `vsscanf` 全部需要自己解析 guest 格式串，**未知格式符必须触发诊断而非静默跳过**。

### 7.4 setjmp/longjmp

不要复制宿主的 `jmp_buf`。在 guest `jmp_buf` 里存一个不透明 ID，宿主侧保存
ARM 核心寄存器、SP/LR/PC、CPSR、必要的 VFP 寄存器和 guest 线程号。
**跨线程 `longjmp` 必须拒绝。**

---

## 8. 目录结构建议

```
src/
  core/          基础设施：日志、配置、错误、序列化
  hal/           宿主抽象：window/gfx/audio/input/fs/video/thread/clock
    windows/ linux/ macos/
  cpu/           JIT 后端、解释器、寄存器与异常模型
  memory/        地址空间、MMU、分配器、快照
  loader/        apk/ obb/ elf/ linker/ manifest
  runtime/
    bionic/      真实 Bionic 装载与选择性拦截
    syscall/     内核 HLE，按组分文件
    jni/         JNIEnv 全表、引用表、签名解析
    dex/         DEX 解析（L1）/ 解释器（L2，可选）
    framework/   Java 框架类 HLE，按包分文件
  gles/          GLES 边界库（IDL 生成）+ ANGLE 接入
  audio/         OpenSL ES / AudioTrack / MediaPlayer
  input/         设备抽象 + 映射引擎
  session/       会话编排、Title Profile、存档
  agent/         Agent Control Server
  frontend/
    cli/ gui/
data/
  android/{19,22,23}/    预置 guest 系统库
  profiles/              题库（每游戏一个 toml）
  goldens/               黄金帧基线
tests/
  unit/ contract/ golden/ titles/
```

**每个目录一份 `MODULE.md`**（见 [02](02-ai-workflow.md)）。

---

## 9. 技术选型汇总

| 领域 | 选型 | 备注 |
| --- | --- | --- |
| 语言 | C++20 | 沿用；接口层考虑 C ABI 便于将来绑定 |
| CPU | Dynarmic | 沿用，扩展到 A64 |
| 图形 | **ANGLE** | 替换手写 GLES→GL |
| 窗口/输入/手柄 | **SDL3** | 替换 GLFW；手柄数据库、触控、多平台一次到位 |
| 音频 | miniaudio 或 SDL3 audio | 替换 CoreAudio 直连 |
| 视频 | FFmpeg | 片头动画 |
| 压缩 | zlib（沿用）+ zstd | OBB / 存档 |
| GUI | SDL3 + Dear ImGui | 与游戏窗口共用 SDL3+ANGLE 渲染栈；屏幕叠加编辑器同源 |
| 构建 | CMake + vcpkg/Conan | 三平台一致的依赖获取 |
| 测试 | Catch2 或 doctest | 替换自制框架，获得更好的 CI 集成 |
| 日志 | 自建结构化 Logger（可用 spdlog 做后端） | 需要帧号轴、地址符号化、环形缓冲，现成库不直接满足（[09](09-logging.md)） |
| 序列化 | 自定义二进制 + TOML（配置） | 存档要版本化 |
