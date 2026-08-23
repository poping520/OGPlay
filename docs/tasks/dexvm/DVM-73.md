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

## 语义边界

- 发布 `GLSurfaceView$EGLContextFactory` 与 `$EGLConfigChooser` interface descriptor，
  使 application classes 的真实 `implements` hierarchy 可链接；
  `setEGLContextFactory` / `setEGLConfigChooser` 保存传入 guest identity，纳入 GC
  roots 并在 session teardown 清理，null 同样覆盖旧值；不伪造 callback 返回、不主动
  调用 factory/chooser，不改变 managed EGL/ANGLE context、SVC 或 Virtual SO
  transport，不增加未触达 WindowSurfaceFactory/GLWrapper；
- 每个 guest `IntentFilter` identity 独立保存 case-sensitive scheme 文本，保持
  insertion order，重复 scheme 不追加；null 沿 guest String 解引用抛
  `NullPointerException`；
- authority 按 insertion order 追加、与 AOSP 一致不去重；保存原始 host，识别首字符
  `*` wildcard 并保存去除星号后的 match host；null port 保存为 `-1`，非 null port 按
  Java 十进制 int 解析，非法或溢出抛 `NumberFormatException`，null host 抛
  `NullPointerException`；
- 不借此扩展广播派发或未触达的 filter query/match/Uri 匹配 API；`registerReceiver`
  仍只保留现有 per-Context receiver ownership 与无 sticky broadcast 事实。

## 验收

- [x] `GLSurfaceView$EGLContextFactory` 与 `$EGLConfigChooser` interface descriptor
      可链接，application classes 的真实 `implements` hierarchy 通过 linker；
- [x] `setEGLContextFactory` / `setEGLConfigChooser` 保存/覆盖精确 guest identity，
      null 同样覆盖旧值，纳入 GC roots 并在 session teardown 清理；
- [x] 每个 guest `IntentFilter` identity 独立保存 case-sensitive、去重、保序的
      scheme 文本与按 `AuthorityEntry` 契约的 host/wildcard/parsed-port authority；
- [x] null 参数沿 guest 解引用抛 `NullPointerException`，非法/溢出 port 抛
      `NumberFormatException`；
- [x] 不伪造 callback 调用、广播派发或 Uri 匹配；
- [x] focused GLSurfaceView/EGL/receiver tests 通过；
- [x] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [x] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

验收：focused 4/4、2/2、3/3。exact run 关闭 survey，frame limit 3、wall limit 45 s，
依次越过两个 EGL interface、`addDataScheme` 与 `addDataAuthority` 并越过 Java
StartupNativeImpl；新首 fault 为 application field `mConfigurationOrientation`
访问到 non-VM instance。结束后无 `ogplay` 进程。

状态：完成。
