# DVM-61 · Java 对象身份与内部 handle 解耦

## 目标（一句话）

让 guest 可观察的 identity hash 独立于可复用的 `VmObjectRef`、对象记录槽和
intrinsic catalog 编号，并由 `Object.hashCode`、默认 `Object.toString` 与
`System.identityHashCode` 共用对象模型中的唯一身份服务。

## 依赖

- DVM-44（GC-B 句柄复用）
- DVM-49（统一 Java 对象模型）
- 本地 API-19 `libcore` / Dalvik `dvmIdentityHashCode` 语义基线

## 交付

- 每个非 Class 对象在登记时获得单调、不回收的 32-bit identity hash；对象存活期
  稳定，GC 清扫后即使记录槽和 `VmObjectRef` 被复用，新对象也获得新 hash。
- Class 对象的 identity hash 由稳定 descriptor 以固定 FNV-1a 派生，不依赖
  `DexClassId`、catalog 注册顺序或 Class object 的内部 handle。hash 碰撞遵循 Java
  契约允许，0 保留给 null。
- `JavaObjectModel::IdentityHashCode` 成为唯一服务：`Object.hashCode()` 使用它；
  默认 `Object.toString()` 输出 `dotted.class@lowercaseHexHash`；新增 API-19
  `System.identityHashCode(Object)`，null 返回 0，并绕过子类 `hashCode()` override。
- 删除 Android intrinsic catalog “hashCode 暴露 handle，前缀不得插入”的架构
  约束；catalog 的历史尾部布局不再属于 guest identity 契约。

## 验证与裁决

- `tests/dexvm/interpreter_tests.cpp`：交换两个 intrinsic class 的 catalog 顺序并
  制造不同 Class handle，验证同 descriptor 的 Class hash、普通对象 hash 均稳定；
  覆盖 Object hash/toString、System null 与 override bypass。
- `tests/dexvm/gc_tests.cpp`：清扫后复用同一 handle，断言新对象 identity hash 不同。
- 本 WU 不跑特定 title；能力属于 DexVM 基础身份栈，`dexvm.object_model` 保持
  `complete`，`dexvm.intrinsics_java_core` 因既有 deferred API 保持 `partial`。

状态：完成。
