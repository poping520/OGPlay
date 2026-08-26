# BND-25 · libdl process service boundary

## 目标

把 Android `libdl.so` 中依赖设备 linker 的公开入口连接到 OGPlay process-owned ELF
namespace 与 sealed Virtual SO symbols，使 guest native 能按 Android 语义动态查询已加载
模块和边界符号，而不执行无独立语义的 guest stub。

## AOSP 与 reached-path 依据

- API 19 `libdl.so` 的 `dlopen/dlsym/dlclose/dlerror` 是 linker service 入口，不是可脱离
  `/system/bin/linker` 独立工作的普通库函数。
- IDA 确认 reached native 路径以 `dlopen(path, RTLD_NOW)` 打开 GLES 库，再用 `dlsym`
  查询 `glVertexPointer`；旧实现执行 guest libdl stub，导致 GL feature 初始化失败。
- 修复属于 Bionic/native boundary 与 process namespace 的通用能力，不属于 DexVM Java/JNI
  字段能力，也不允许在生产代码中出现 title 专属条件。

## 范围

- 将四个 libdl export 作为显式 `GuestSymbolOverrideDescriptor`，分别 direct-bind 到
  `LibdlOverrideModule`；
- boundary 负责受检 guest C string 搬运、null/-1 ABI 返回和逐 guest thread、消费式
  `dlerror` 缓冲；
- integration 通过窄 `BionicDynamicLinkHooks` 拥有 handle/refcount；已加载 guest ELF 使用
  root scope，Virtual SO 使用 sealed provider，`RTLD_DEFAULT` 查询全局 namespace；
- `dlopen` 路径仅规范化 basename，不访问宿主文件系统；`dlclose` 释放引用但不卸载
  process-lifetime module；未知库、符号、handle 或 flag 保留为 `dlerror`；
- 不实现任意新 APK `.so` 的源发现/映射，不引入宿主 `LoadLibrary/dlopen`，不处理后续
  Handler/GL 初始化缺口。

## 验收

- [x] guest `libdl.so` 的四个公开 stub 均重定位到独立 HLE thunk；
- [x] `dlopen`/`dlsym` 可返回 sealed GLES boundary symbol，handle 重开/关闭保持引用语义；
- [x] 失败返回 null/-1，同 guest thread 的 `dlerror()` 返回一次后清除；
- [x] Bionic route/relocation、libdl bridge/error 与既有 libc override 定向回归通过；
- [x] `MODULE.md`、`capabilities.toml` 与 `CURRENT.md` 同步；
- [x] 临时 JNI/native/intrinsic/monitor 调试代码全部移除；
- [x] 按要求未执行完整测试。
- [x] `dlopen(nullptr)` 返回 `RTLD_DEFAULT` 伪句柄，`dlclose` 对其 no-op；
- [x] `dlsym(RTLD_DEFAULT)` 先命中任意 sealed HLE 模块再回退 ELF namespace，未知名仍
  进入 `dlerror`；
- [x] 主机 GL 包装名 `libhgl.so` 打开共享 `libGLESv2.so` 边界 handle，GLES2 符号可查。

## 2026-08-26 追加修复：KitKat 全进程语义

- 症状：pvz `LoaderThread.runNative` 在 `f≈2382` 以 `pc=0x6045be18` 的 NULL `AddRef`
  guest fault 终止。静态证据：S3E 只按名导入 `eglGetProcAddress`，`glCreateShader`
  等仅为运行时查询字符串；原 `RTLD_DEFAULT` 分支只查 ELF namespace，sealed HLE GL
  模块不可见，GL 函数表全零（frontend `draws=0 clears=0`），首个空对象解引用即崩。
- 修复（对齐 KitKat：`RTLD_DEFAULT=0xffffffff` 走 solist 全表遍历，`dlopen(NULL)`
  不建立普通库句柄）：
  - `BionicHleSymbolProvider::LookupAny(name)`：`dlsym(RTLD_DEFAULT)` 先搜任意
    sealed HLE 模块（catalog 顺序首个命中）再回退 ELF namespace；
  - `dlopen(nullptr)` 直接返回 `RTLD_DEFAULT` 伪句柄，不再对 root module 新建引用
    句柄；`dlclose` 对伪句柄 no-op；
  - 主机 GL 包装名 `libhgl.so` 归一化为 `libGLESv2.so` 边界 handle：引擎自带
    wrapper 库名在 APK 内无 ELF，设备上同样由进程内 GL 可见性满足，别名保持其
    落在 sealed GL surface。
- 明确不做、另行记账：boundary 句柄 miss 的全库回退（pvz-tmp 问题 2，超出 KitKat
  单库语义，是否必要待下一阻断复测）、`eglGetProcAddress` 全表回退（问题 3，当前
  符号表下无可观测影响）、`EGLContextImpl/GLImpl` 契约（问题 4，与本次崩溃无因果）。

## 验证记录

- Windows Release `ogplay` 与 Debug `ogplay_tests` 增量构建通过。
- libdl、Bionic route 与 libc override 定向 5/5、60 assertions 通过；architecture 门禁
  6/6 通过，本轮合计 11/11。
- 同一 survey reached run 收到此前缺失的 `LoaderThread.glInit(I)V`，并持续到约 2 万
  frontend frame 未再出现 PC `0x60462edc`、地址 `0x1f84` fault；`presented=1` 保持稳定。
  该运行只证明 BND-25 阻断已推进，不是完整兼容性验收。
- 2026-08-26 追加修复轮：Windows Debug/Release 全目标增量构建通过；新增
  `Bionic HLE default lookup crosses sealed module boundaries`（8 assertions）与
  `libdl process service exposes RTLD_DEFAULT and the host GL alias`
  （25 assertions，经 `process->Invoke` 直调 libdl thunk）定向通过；`Bionic*`
  14/14、存量 libdl 2/2 无回归；按要求未执行全量测试。
- pvz Release 实跑越过原 `f≈2382 pc=0x6045be18` NULL AddRef 崩溃点，GL 调用已进入
  boundary；新停止点 `f=2478` 为 `GLES1 texture target must be GL_TEXTURE_2D`
  （`gles1_dispatch.cpp:340` 把非法 target 当致命异常，真机仅产生 guest 可见
  `GL_INVALID_ENUM`），属范围外独立问题，未处理。

状态：完成。
