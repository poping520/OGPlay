# DVM-75 · String.format 与 PackageManager 启动面

## 目标（一句话）

实现 exact reached 的 `String.format` 顺序 `%d`/`%s`/`%%` conversion，并在 Context
继承面发布进程级稳定 `PackageManager` identity，关闭 exact run 当前故障链。

## 依赖

- DVM-74 的关闭-survey 第一故障
- API19 libcore `String.format`/`Formatter` general/decimal conversion 与统一
  Object[]/wrapper slot representation
- AOSP `android-4.4.4_r2.0.1` `Context` / `ContextWrapper` / `PackageManager`
  类型契约与 `DexVmAndroidContext` 会话状态

## 语义边界

- `%d` 接受 API19 Formatter 的 integral wrapper family（Byte/Short/Integer/Long），
  按 signed decimal 输出；`%%` 无参数；null format 抛 `NullPointerException`；null
  args 仅在格式不消费参数时等价空参数表；参数不足、错误 wrapper 明确失败；
- `%s` 对 null 输出 `null`，对 guest String 输出完整 UTF-16 value，与 `%d`/`%%` 共用
  顺序 argument cursor，额外参数忽略；不伪造普通 Object 的 `toString`，普通 object
  `%s` 明确 `UnsupportedOperationException`，等待真实 reached behavior 后再设计可保留
  throwable identity 的 virtual-toString pipeline；
- 未实现 conversion/flags/index/width/precision 明确抛
  `UnsupportedOperationException`，不静默返回原 format，不调用宿主 printf/locale，
  不改变 Object[]、wrapper、String identity 或 logging 行为；
- `getPackageManager()` 声明为 `android.content.Context` virtual FinalMethod，Activity
  沿现有 Context 继承链解析，不增加 Activity 特例；每个 `DexVmAndroidContext` 只物化
  一个稳定 `Landroid/content/pm/PackageManager;` guest identity，所有 Context
  receiver 共享；
- 只发布 API19 抽象 `PackageManager` 类型形状，不预实现 `getPackageInfo`、feature
  query、intent resolution 或系统包数据库；下一真实 reached member 由关闭-survey
  exact run 固定后另行设计；不复用 legacy headless JNI HLE 的 host
  identity/reference，不改变 JNI 或 native PackageManager 行为。

## 验收

- [x] 四个 boxed Integer 与 `%%` 生成 exact LruCache 文本，Byte/Short/Long signed
      decimal 有覆盖；
- [x] null format、参数不足、错误类型与 unsupported conversion 明确失败；
- [x] `NIMBLE VERSION %s (Build %s)` 按两个 guest String 精确生成，`%s/%d/%%`
      混合时共享正确 argument cursor，null 输出 `null`，普通 object 与缺参明确失败；
- [x] Activity 继承解析 `getPackageManager()`，重复调用与不同 Context receiver 返回
      同一非 null identity；
- [x] Android intrinsic catalog 包含 API19 PackageManager class shape；
- [x] focused String/Object[]/wrapper/Context tests 通过（4/4、5/5）；
- [ ] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [x] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

验收：focused 4/4、5/5。exact run 关闭 survey，frame limit 3、wall limit 45 s，
依次越过 decimal format、`BaseCore.getInstance` 的 `%s` conversion 与
`Activity.getPackageManager()` identity 发布；下一 fault 待关闭-survey exact run
固定。结束后无 `ogplay` 进程。

状态：进行中（PackageManager 下一 reached member 待 exact run 固定）。
