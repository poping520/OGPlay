# BND-3 · JNI Fast Transport 与 libc Override 分离

## 目标

让 JNI `SVC #3` 复用 CPU fast transport，同时将真实 guest libc 的宿主拦截从
Virtual SO catalog 拆成独立 `GuestSymbolOverride` 元数据。

## 交付与验收

- [ ] JNI live-register fast entry 与 slow path 语义等价。
- [ ] JNI slot/family/decoder、RegisterNatives、JNI_OnLoad 语义不变。
- [ ] libc override 有独立 descriptor，仍绑定真实 guest libc export。
- [ ] focused JNI、CPU 与 Bionic override tests。
