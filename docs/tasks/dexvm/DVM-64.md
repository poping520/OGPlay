# DVM-64 · Reflection wrappers

## 目标（一句话）

建立 reflection runtime 的不可变成员 metadata 与唯一 wrapper factory，使 Method、
Constructor、Field guest wrapper 具有 API-19 shape、独立 identity/accessibility 和不透明
成员 token，不再暴露 linker/Dex id 或由 `Class` 手写 raw slots。

## 依赖

- DVM-62（declared-order linker metadata、raw access flags 与 invocation category）
- DVM-63（稳定 ClassLoader facade 与 initiating-loader contract）
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 Dalvik `Reflect.cpp` / `Reflect.h` 与 libcore `Method.java`、
  `Constructor.java`、`Field.java`、`AccessibleObject.java`

## 交付

- 新增 `ReflectionRuntime`：按 declaring class 懒构建不可变 Method/Constructor/Field
  metadata，保存解析后的类型、flags 和调用类别；失败构建不污染缓存。
- reflection slot 固定为“wrapper kind + declaring Class + declared-order ordinal”语义，
  不等于 `VmMethodId`、`VmFieldId` 或 DEX member index。
- 唯一 factory 物化 API-19 字段布局，统一写入 declaring class、name、parameter/
  exception/return/field type；`Class.getDeclaredMethods` 删除手写 slot 协议并委托 runtime。
- 补齐 `AnnotatedElement`、`GenericDeclaration`、`Type`、`Member`、`AccessibleObject`、
  `Modifier`、`Method`、`Constructor`、`Field` 基础 hierarchy/shape。
- repeated query 返回不同 wrapper identity，semantic equals/hash 稳定；parameter/exception
  arrays defensive copy；`AccessibleObject.flag` 为 per-wrapper guest field。
- metadata cache 不持有 guest ref；wrapper 与其引用字段走普通对象图，可由 GC 回收后重新物化。
- 清理 Android intrinsic 中三个非法重复 final override，使 DVM-62 的真实 final flags 在完整
  Android catalog 上仍可执行链接检查；行为继续继承同一基类 handler。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 覆盖 declared order、opaque slot、解析类型、fresh wrapper、
  semantic equals/hash、modifiers、defensive arrays、per-wrapper accessibility、Constructor/Field
  factory 与 GC 不泄漏。
- 定向回归覆盖 reflection linker metadata、core/Android intrinsic catalog、既有 Display
  reflection invoke、ClassLoader、intrinsic builder 与 GC；不跑全量测试和 title gate。
- 新能力记为 `dexvm.reflection_wrappers = complete`；Class 全结构查询、完整 invoke、
  Constructor/Field/Array 行为仍由 DVM-65..69 交付。

状态：完成。
