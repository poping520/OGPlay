# DVM-74 · JNI 实例导入与 Configuration screenLayout

## 目标（一句话）

让 JNI `NewObject` 创建的 application class identity 回入 interpreted constructor 时
带上完整 instance slots，并发布 `Configuration.screenLayout` 派生 bits，关闭 exact
run 当前故障链。

## 依赖

- DVM-73 的关闭-survey 第一故障（application field `mConfigurationOrientation`
  访问到 non-VM instance）与 LLDB kind/call-stack 诊断
- DVM-49 统一 JNI/DexVM object identity 与 DVM-71 完整 class hierarchy
- `JavaObjectModel::FromIdentity`、`JniGuestObjectRegistry::Allocate`、DexVM linker
  layout
- AOSP `android-4.4.4_r2.0.1` `Configuration.reduceScreenLayout` 与 DVM-71 注入式
  DisplayMetrics surface/density 事实

## 验收

- [ ] JNI registry 分配的 application object round-trip 后为 `vm_instance`，
      imported identity 原样保留，constructor 可写并读取 instance field；
- [ ] intrinsic host-backed 对象保持现有 external classification，不改变 JNI
      `NewObject/NewObjectV/NewObjectA` 的分配、回滚、引用与 invocation ABI；
- [ ] `Resources.getConfiguration()` 稳定 singleton 同时发布 `keyboard=NOKEYS` 与
      `screenLayout`，按 linker field metadata 写入；
- [ ] 像素尺寸除以受检 `ui_density` 得 dp，按 API19 固定阈值发布 `SIZE_*`、`LONG_*`
      与 `COMPAT_NEEDED` bits；非有限/非正 density 或零 surface 明确失败；
- [ ] focused object model/Configuration tests 通过；
- [ ] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [ ] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

状态：设计完成，待实现。
