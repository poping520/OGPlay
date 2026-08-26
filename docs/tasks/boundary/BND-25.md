# BND-25 · libdl process service boundary

## 目标

把 Android `libdl.so` 中依赖设备 linker 的公开入口连接到 OGPlay process-owned ELF
namespace 与 sealed Virtual SO symbols，使 guest native 能按 KitKat 进程语义查询已加载
模块和边界符号，而不执行没有独立语义的 guest stub。

## AOSP 与 reached-path 依据

- API 19 `libdl.so` 的 `dlopen/dlsym/dlclose/dlerror` 是 linker service 入口，不是可脱离
  `/system/bin/linker` 独立工作的普通库函数。
- reached native 路径以 `dlopen(path, RTLD_NOW)` 打开 GLES 或引擎包装库，再通过
  `dlsym`、`RTLD_DEFAULT` 与相关 boundary handle 查询 GL 入口。旧实现执行 guest
  libdl stub，sealed HLE symbols 对该路径不可见，导致 GL 函数表未建立并在后续调用中
  出现空指针故障。
- 修复属于 Bionic/native boundary 与 process namespace 的通用能力，不属于 DexVM
  Java/JNI 能力，生产代码不得出现 title、厂商或包名专用条件。

## 设计与范围

- 四个 libdl export 作为显式 `GuestSymbolOverrideDescriptor`，分别 direct-bind 到
  `LibdlOverrideModule`。
- boundary 负责受检 guest C string 搬运、null/-1 ABI 返回，以及按 guest thread 隔离、
  消费式清除的 `dlerror` 缓冲。
- integration 通过窄 `BionicDynamicLinkHooks` 拥有 handle/refcount；已加载 guest ELF
  使用 root scope，Virtual SO 使用 sealed provider。
- `dlopen(nullptr)` 返回 `RTLD_DEFAULT` 伪句柄，`dlclose` 对该句柄 no-op；普通 handle
  重开和关闭维持引用语义，但 process-lifetime module 不因引用归零而卸载。
- `dlsym(RTLD_DEFAULT)` 先在完整 sealed HLE namespace 查询，再回退 guest ELF
  namespace。boundary handle 在本模块查询未命中时同样回退完整 sealed namespace，表达
  KitKat 引擎包装库重导出 GL 入口的进程语义。
- 主机 GL 包装名 `libhgl.so` 规范化为共享 `libGLESv2.so` boundary handle；其他路径只
  规范化 basename，不访问宿主文件系统。
- 未知库、符号、handle 或 flag 明确失败并进入 `dlerror`，不得伪造成功。

## 明确不做

- 不发现、映射或装载任意新的 APK `.so`。
- 不调用宿主 `LoadLibrary`、`dlopen` 或宿主 linker namespace。
- 不实现 `eglGetProcAddress` 的完整函数表回退；当前 sealed symbol 路径没有可观测缺口。
- 不处理 `EGLContextImpl/GLImpl` 契约、Handler 生命周期或后续 GLES 固定管线能力；
  cube-map 等后续独立能力由对应 boundary 工作单闭合。

## 验收

- [x] guest `libdl.so` 的四个公开 stub 均重定位到独立 HLE thunk。
- [x] `dlopen`/`dlsym` 可返回 sealed GLES boundary symbol，handle 重开/关闭保持引用语义。
- [x] `dlopen(nullptr)`、`RTLD_DEFAULT`、boundary handle miss 和 guest ELF fallback 形成
  一致的全进程查询链。
- [x] `libhgl.so` 可打开共享 GLES2 boundary handle 并查询 GLES2 symbols。
- [x] 失败返回 null/-1，同一 guest thread 的 `dlerror()` 返回一次后清除。
- [x] Bionic route/relocation、libdl bridge/error、libc override 与 architecture 门禁通过。
- [x] 临时 JNI/native/intrinsic/monitor 调试代码全部移除。
- [x] `MODULE.md`、`capabilities.toml` 与 `CURRENT.md` 同步。
- [x] 按要求未执行完整测试。

## 验证结果

- Windows Debug/Release 目标构建通过。
- libdl、Bionic route、libc override 与 architecture 定向集合通过；覆盖
  `RTLD_DEFAULT` 全局查询、主机 GL alias、boundary handle 跨 sealed surface 回退和
  消费式错误状态。
- reached run 越过 GL 表为空导致的空指针故障，`LoaderThread.glInit(I)V` 与后续 GL
  调用进入 boundary，未再出现原故障 PC/地址。
- 后续运行暴露 `GL_TEXTURE_CUBE_MAP` 能力缺口；它与 libdl namespace 无因果关系，已由
  独立 GLES1 cube-map 工作单处理。该运行只证明 BND-25 阻断闭合，不代表完整兼容性验收。

状态：完成。
