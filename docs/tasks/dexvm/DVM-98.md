# DVM-98 · 平台 intrinsic enum 声明生成器

## 目标

在一个 WU 内以 `IntrinsicEnumBuilder` 统一平台 enum 的常量字段、`$VALUES`、
`<clinit>`、`values()` 与 `valueOf(String)`，并迁移现有 intrinsic enum。

## 依赖

- DVM-51：`IntrinsicClassBuilder` 与预绑定字段；
- DVM-61：稳定 guest identity；
- pinned API 19 `Enum.java` 与对应平台 enum 声明。

## 交付与边界

- builder 在链接前生成 `Enum` superclass、enum access flag、常量静态字段、合成
  `$VALUES`、精确类型的 `values/valueOf` 及类初始化器。
- `values()` 每次浅克隆 `$VALUES`；常量由静态字段强根持有，名称和 ordinal 统一经
  `Enum(String,int)` 初始化。
- object factory、逐常量初始化和完成回调用于 AsyncTask singleton、nativeInt 与 UI
  side state；扩展不得另写 enum 公共机制。
- 迁移 `Thread.State`、`Bitmap.Config`、`Path.Direction`、`PorterDuff.Mode`、
  `Region.Op`、`NetworkInfo.State`、`AsyncTask.Status`、`ImageView.ScaleType`。
- 保持各 Android enum 已有的有界常量集合；本 WU 不扩充未交付常量，不实现 `EnumSet`、
  `EnumMap` 或通用枚举常量缓存。

## 验收

- [x] builder 生成的类形状、常量 identity、name/ordinal、数组克隆与异常受检；
- [x] 8 个现有平台 enum 全部使用 builder，普通 Object 假 enum 修正为 Enum；
- [x] Bitmap native 映射、AsyncTask singleton、ImageView side state 保持回归；
- [x] Windows Release 受影响目标构建和架构检查通过。

验证：DVM-98 3/3、540 断言；Bitmap/AsyncTask/ImageView 3/3、110 断言；相邻
catalog/graphics 3/3、3856 断言；`architecture.dexvm_intrinsic_layout` 1/1。

状态：已完成。
