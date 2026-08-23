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

## 根因与语义边界

- guest native code 经 JNI `NewObject(MainActivity)` 分配 host-domain identity，随后
  通过 invocation engine 回入 interpreted `<init>`；registry 的 class identity 正确，
  但 `FromIdentity` 把普通导入对象统一标为 `external`，application constructor 的
  第一个 `iput` 失败；
- 为 `JavaObjectInterop` 增加最小 interpreted-instance layout resolver：仅当导入对象
  的 linked class 是 application/interpreted class 时，按 linker `instance_slots`
  建立 `vm_instance` storage 并保留原 JNI identity；intrinsic host-backed 对象保持
  现有 external/专用 store 语义；不改变 JNI `NewObject/NewObjectV/NewObjectA` 的
  分配、回滚、引用与 invocation ABI，不放宽字段 owner/slot/type 校验；
- `Resources.getConfiguration()` 的稳定 singleton 同时发布现有 `keyboard=NOKEYS` 与
  `screenLayout`，按 linker field metadata 写入，不按数组顺序隐式猜字段 slot；
- 像素尺寸除以受检 `ui_density` 得到 dp，long/short 轴送入 API19 固定阈值算法，发布
  `SIZE_*`、`LONG_*` 与 `COMPAT_NEEDED` bits；layout-direction 保持 undefined，
  不伪造未建立的 locale layout-direction pipeline；非有限/非正 density 或零
  surface 是 host assembly error，明确失败；不预发布 `smallestScreenWidthDp` 等
  相邻 Configuration surface，不改变窗口、旋转或 DisplayMetrics。

## 验收

- [x] JNI registry 分配的 application object round-trip 后为 `vm_instance`，
      imported identity 原样保留，constructor 可写并读取 instance field；
- [x] intrinsic host-backed 对象保持现有 external classification，不改变 JNI
      `NewObject/NewObjectV/NewObjectA` 的分配、回滚、引用与 invocation ABI；
- [x] `Resources.getConfiguration()` 稳定 singleton 同时发布 `keyboard=NOKEYS` 与
      `screenLayout`，按 linker field metadata 写入；
- [x] 像素尺寸除以受检 `ui_density` 得 dp，按 API19 固定阈值发布 `SIZE_*`、`LONG_*`
      与 `COMPAT_NEEDED` bits；非有限/非正 density 或零 surface 明确失败；
- [x] focused object model/Configuration tests 通过；
- [x] 关闭 survey 的 bounded exact run 固定下一 fault 或达到可见帧；
- [x] 无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

验收：focused 4/4、2/2。exact run 关闭 survey，frame limit 3、wall limit 45 s，
依次越过 application constructor field access 与 `Configuration.screenLayout:I`
fault；新首 fault 为 `String.format(String,Object[])`。结束后无 `ogplay` 进程。

状态：完成。
