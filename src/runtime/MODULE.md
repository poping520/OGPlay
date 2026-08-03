# 模块：runtime

## 职责

装载真实 Bionic，并实现 syscall、完整 JNI/JavaVM、可选 DEX 解释器和 Android 框架 HLE。

## 公共 API

- `A32SyscallDispatcher`：按 ARM EABI 号分派 syscall；声明与实现分离，未知或未实现调用
  统一记入能力账本并返回 `-ENOSYS`，覆盖率可按组查询。
- `BindAndroidTimeSyscalls`：将 `clock_gettime/gettimeofday` 绑定到统一 Clock 和受检 guest
  内存；错误 clock ID、溢出和坏指针分别返回 Linux errno。
- `BindAndroidMemorySyscalls`：实现匿名私有 `mmap2`、`munmap`、`mprotect` 与 `brk`；
  页对齐、地址范围和 W^X 失败均转换为明确 Linux errno。
- `BindAndroidThreadSyscalls`：把 `futex WAIT/WAKE` 装配到 M1 真线程 FutexTable，支持
  PRIVATE flag、精确 WAKE N、值不匹配和受检地址错误；`sched_yield` 让出宿主线程。
- `SelectBionicProfile` / `RouteBionicSymbol`：只接受 API 19/22/23，固定真实 guest Bionic
  库、宿主 HLE 边界库及少而明确的 str/mem/pthread 拦截表。
- 子域按 `bionic/syscall/jni/dex/framework` 分文件，禁止巨型 dispatcher。

## 不变量

- 未实现调用记入能力账本并明确失败；syscall 返回 ENOSYS。
- 普通 libc/libm/libdl 符号默认执行真实 guest Bionic；只有声明表命中才进入宿主拦截。
- JNIEnv 全表完成前，缺槽位必须 trap，不得静默返回零。
- 框架类绑定声明式；对象模型允许宿主对象与未来 VM 对象共存。

## 禁止

- 不实现 Binder/system_server/Zygote/完整 ART。
- 不包含包名、厂商名、绝对补丁地址或游戏专属 Java 回调。

## 测试

`tests/runtime/` 契约基准来自 AOSP/真机行为。
