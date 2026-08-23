# DVM-73 · GLSurfaceView/IntentFilter 策略元数据

## 目标（一句话）

发布目标 DEX 实际实现的 GLSurfaceView EGL policy 接口层级并保存 setter 注入的
guest policy identity，按 API19 语义保存 IntentFilter data-scheme/authority 元数据，
关闭 exact APK 当前链接故障。

## 依赖

- DVM-72 的关闭-survey 第一故障
- AOSP `android-4.4.4_r2.0.1` `GLSurfaceView.java` / `IntentFilter.java` /
  `IntentFilter.AuthorityEntry`
- DVM-31 managed EGL façade 与现有 renderer lifecycle、现有 Context dynamic
  receiver registration ownership

## 验收

- [ ] `GLSurfaceView$EGLContextFactory` 与 `$EGLConfigChooser` interface descriptor
      可链接，application classes 的真实 `implements` hierarchy 通过 linker；
- [ ] `setEGLContextFactory` / `setEGLConfigChooser` 保存/覆盖精确 guest identity，
      null 同样覆盖旧值，纳入 GC roots 并在 session teardown 清理；
- [ ] 每个 guest `IntentFilter` identity 独立保存 case-sensitive、去重、保序的
      scheme 文本与按 `AuthorityEntry` 契约的 host/wildcard/parsed-port authority；
- [ ] null 参数沿 guest 解引用抛 `NullPointerException`，非法/溢出 port 抛
      `NumberFormatException`；
- [ ] 不伪造 callback 调用、广播派发或 Uri 匹配；
- [ ] focused GLSurfaceView/EGL/receiver tests 通过；
- [ ] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [ ] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

状态：设计完成，待实现。
