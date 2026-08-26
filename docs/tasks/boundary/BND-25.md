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

## 验证记录

- Windows Release `ogplay` 与 Debug `ogplay_tests` 增量构建通过。
- libdl、Bionic route 与 libc override 定向 5/5、60 assertions 通过；architecture 门禁
  6/6 通过，本轮合计 11/11。
- 同一 survey reached run 收到此前缺失的 `LoaderThread.glInit(I)V`，并持续到约 2 万
  frontend frame 未再出现 PC `0x60462edc`、地址 `0x1f84` fault；`presented=1` 保持稳定。
  该运行只证明 BND-25 阻断已推进，不是完整兼容性验收。

状态：完成。
