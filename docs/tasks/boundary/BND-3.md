# BND-3 · JNI Fast Transport 与 libc Override 分离

## 目标

让 JNI `SVC #3` 复用 CPU fast transport，同时将真实 guest libc 的宿主拦截从
Virtual SO catalog 拆成独立 `GuestSymbolOverride` 元数据。

## 交付与验收

- [x] JNI live-register fast entry 与 slow path 共用唯一 dispatch 语义。
- [x] JNI slot/family/decoder、RegisterNatives、JNI_OnLoad 语义不变。
- [x] libc override 有独立 descriptor，仍绑定真实 guest libc export。
- [x] focused JNI、CPU 与 Bionic override tests。
- [x] fast callback 按 guest thread/PC 登记原始 `exception_ptr`，JIT 退出后由 JNI/boundary
  consumer 恢复；C++ exception 不跨 Dynarmic callback。
- [x] unbound JNI、invalid interface receiver 与 guest memory fault 的 fast/slow
  failure identity/diagnostic 等价测试。
- [x] 五个 libc override 在 seal 时按真实 parameter count 分别绑定独立 `{fn,module*}`；
  fast/slow 共用 export-specific binding，不以共享 PC/function id 推导 symbol。
- [x] 不同 guest thread 并发执行 memcpy/memset/strlen 的回归测试，无函数串线和共享
  mutable dispatch state。

JNI handler storage 继续保持既有语义；它不属于 Virtual SO hot table，未改动
RegisterNatives、JNI_OnLoad、对象/引用与异常模型。
