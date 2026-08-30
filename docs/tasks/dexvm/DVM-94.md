# DVM-94 · 稳定 Linker、Intrinsic 继承与强类型调用模型

## 目标

让 intrinsic declaration 只保存 own members，继承、override 和调用类别完全由 linker
产生并校验；运行期追加数组/Survey metadata 不得使活动引用失效。

## 交付

- class/method/field/extras 改用追加地址稳定存储；Frame 的局部 method 引用因此有明确稳定期。
- 新增 `InvokeKind`、`ResolvedCallSite` 和按 `(method index, kind)` 隔离的缓存。
- 新增注册期 `MethodShape`；参数 word offset、incoming words 与返回类别不再在热路径解析。
- builder 新增 `OverrideMethod`、`FinalOverrideMethod`、`UnimplementedOverride`；链接器拒绝
  虚假 override、final override、收窄可见性和 `VirtualMethod` 隐式覆盖。
- 清除 Stack/LinkedHashSet 的复制声明；ContextWrapper 委托及其余真实覆盖改为显式 override。
- 缺陷修复：以 Android 4.4.4 framework/core DEX 元数据校准现有 protected callback；
  Activity 生命周期、View.onSizeChanged、AsyncTask callbacks 与 HandlerThread.onLooperPrepared
  不再错误继承 builder 的 public 默认值。未增加 Android API。
- architecture gate 锁定 Application 不复制 Context 方法。规则见 ADR-0028。

## 验收

- [x] 三层 intrinsic vtable 继承、同 slot override、MethodShape 与 metadata 追加稳定测试。
- [x] 隐式、虚假和 final override 均在链接期失败。
- [x] core/Android catalog 链接与 Context 继承定向回归通过。
- [x] protected 父方法允许 protected/public 覆盖，拒绝 package/private；真实 DEX protected
  Activity override 与 Android 4.4 callback metadata 回归通过。
- [x] switch/threaded invoke、reflection 与 Survey 定向回归通过。

状态：已完成。
