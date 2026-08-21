# DVM-62 · Reflection linker metadata

## 目标（一句话）

建立反射后续批次唯一依赖的 descriptor/name codec 与 linker metadata v2，使类、
接口、成员声明顺序、完整 access flags、调用类别和 defining-loader role 都由 linker
发布，不再由 `Class`/reflect intrinsic 从 vtable、descriptor 前缀或 raw slot 猜测。

## 依赖

- DVM-7（DexClassLinker 注册、层级、布局与 dispatch）
- DVM-51（IntrinsicClassBuilder 与 API-19 shape 工具底座）
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 Dalvik `Class.cpp` / `Resolve.cpp` 与 libcore `Class.java` 语义基线

## 交付

- 新增正式 `ClassNameCodec`：受检解析 method descriptor，分类 primitive/reference/
  array，并实现 descriptor → `Class.getName()` 与 binary name → descriptor 转换；
  primitive keyword 不冒充 `Class.forName` 可查 binary name，primitive class 补齐 `V`。
- `LinkedClass` 保存 bootstrap/application defining-loader role、direct interfaces、
  link-time flattened interfaces，以及 DEX/intrinsic declaration order 下的 own direct/
  virtual methods 与 own static/instance fields。
- `LinkedMethod` 保存 raw access flags 与 `direct/static/virtual/interface` 声明调用类别；
  vtable 继续只负责 dispatch，不再承担 declared-member metadata。
- `IntrinsicClassDecl/MethodDecl/FieldDecl` 与 builder 全链路保存 raw access flags；新增
  private/direct method 声明入口，static/final/constructor category flags 由 builder
  受检补齐。
- array defining loader 跟随 component type；platform intrinsic、primitive 与 survey
  platform class 属 bootstrap，application DEX 属唯一 application role。

## 验证与裁决

- `tests/dexvm/class_name_codec_tests.cpp`：覆盖合法/非法 descriptor、255 维/255 参数槽
  边界、API-19 `Class.getName` 拼写、binary name 转换、primitive keyword 拒绝。
- 同文件以 intrinsic hierarchy 与 `interp.dex` 验证 direct/flattened interface 分离、
  raw flags、四类 invocation category、DEX `class_data_item` 顺序、bootstrap/application
  loader role 及 object/primitive/multidimensional array loader 继承。
- 相关回归仅跑 intrinsic builder、缺失层级、core catalog、virtual/super dispatch、
  Cloneable 与 java.lang interface inventory；不跑全量测试和 title gate。
- 新能力记为 `dexvm.reflection_linker_metadata = complete`；完整 reflection surface 仍未
  交付，后续由 DVM-63..69 继续闭合。

## Chapter 11 收尾复验

- `Class.forName` 两个重载继续只经 `ClassNameCodec` 做 binary name/array descriptor
  转换；primitive keyword 保持不可查，未引入第二套名称或 class directory。

状态：完成（含 Chapter 11 closure 复验）。
