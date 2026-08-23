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

## 验收

- [ ] `%d` 接受 integral wrapper family（Byte/Short/Integer/Long）按 signed decimal
      输出，`%%` 无参数；null format 抛 `NullPointerException`，参数不足、错误类型
      与 unsupported conversion 明确失败，不调用宿主 printf/locale；
- [ ] `%s` 对 null 输出 `null`、对 guest String 输出完整 UTF-16 value，与 `%d`/`%%`
      共用顺序 argument cursor；普通 Object `%s` 明确
      `UnsupportedOperationException`；
- [ ] `Context.getPackageManager()` 沿继承解析，每个 `DexVmAndroidContext` 物化一个
      稳定非 null `PackageManager` identity，重复调用与不同 receiver 共享；
- [ ] 只发布 API19 抽象 `PackageManager` 类型形状，不预实现 `getPackageInfo`、
      feature query、intent resolution 或系统包数据库；
- [ ] focused String/Object[]/wrapper/Context tests 通过；
- [ ] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [ ] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

状态：设计完成，待实现。
