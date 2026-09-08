# 子模块：runtime/bionic

## 职责

- `GuestSymbolOverrideDescriptor` 只描述真实 guest ELF export 的宿主覆盖；五个 libc
  memory override 与四个 libdl linker-service override 均与 Virtual SO catalog 分离，
  ELF ownership 始终属于真实 guest `libc.so`/`libdl.so`。
- override metadata 同时生成 descriptor 与 concrete handler binding；每个 export 使用
  自己的 A32 参数个数，调用期不得通过共享 PC 或 function id 选择实现。
- API 19 `libdl.so` 的公开入口本质上依赖进程 linker；`LibdlOverrideModule` 负责 guest
  字符串搬运和逐 guest thread 的 `dlerror` 消费式缓冲，只通过窄 hooks 请求上层 namespace
  打开、查符号和关闭 handle，不反向依赖 integration。
- `BionicHleSymbolProvider` 的 `Lookup` 保持 (library, symbol) 精确 scope；`LookupAny`
  只服务 `RTLD_DEFAULT` 全进程语义，按 catalog 顺序返回首个命中，不做版本或弱符号裁决。

选择 API 19/22/23 Bionic profile，规划并装载真实 guest 系统库闭包，自检 libc/libdl，建立
Bionic TLS，并只为确有生产 handler 的 libc 热点提供宿主边界。

## 依赖

依赖 `loader`、`memory` 与 CPU 状态契约；不得依赖 JNI、framework、syscall、execution 或
integration。线程和 syscall 只通过上层装配接入。

## 不变量

- 普通符号默认执行真实 guest Bionic，选择性拦截必须有显式声明和真实 handler；
  `libdl` 四入口必须进入 process linker service，不得执行设备 linker 才能解释的 guest stub。
- TLS slot、自指针、thread info 与 API profile 必须精确。
- profile 只接受 Android API 19、22、23。
- `BuildBionicModuleSet` 只从 ELF `DT_NEEDED` 递归选择 profile 声明的真实 guest 库；HLE
  边界不作为 ELF 输入，未知、缺失、重复来源及依赖库 SONAME 矛盾明确失败。
  APK 精确匹配选中的根条目保留其 archive basename，允许 `DT_SONAME` 作为同一模块
  的额外 linker 别名；别名与其他模块冲突仍由命名空间明确失败。
- module set 拥有全部字节，并以 64 KiB 间隔自动分配页对齐 load bias；任何映射不得进入
  固定 HLE thunk 地址区。

## 测试

对应 `tests/runtime/bionic_*_tests.cpp`。

DVM-122：API 19 guest 库包含 libcrypto、ICU/STLport 闭包和统一 libogplay_jni.so；API 22/23 保持五库。
统一桥作为普通 ARM ELF 执行，crypto 依赖复用 pinned libc，无宿主 EVP/AES
替代。libcrypto 固定为 AOSP android-4.4.4_r2.0.1 的 platform/external/openssl 源码构建，
ICU 桥只调用 ICU 51 C ABI，不跨越 NDK libc++/API 19 STLport C++ ABI；精确来源、SONAME、
DT_NEEDED 与哈希直接记录并由 payload validator 复核。
