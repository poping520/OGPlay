# DVM-63 · Single ClassLoader facade

## 目标（一句话）

以 linker 唯一 class directory 建立稳定的 API-19 boot/application ClassLoader facade，
闭合受检 lookup、delegation 与 initiating-loader 状态，同时明确排除动态 classpath 和
多 namespace。

## 依赖

- DVM-62（ClassNameCodec、defining-loader role 与 reflection linker metadata）
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 libcore `Class.java` / `ClassLoader.java` / `VMClassLoader.java` 与 Dalvik
  `Class.cpp` 语义基线

## 交付

- 新增 `ClassLoaderFacade`，每个 VM 懒创建且强根持有唯一稳定
  `PathClassLoader`/`BootClassLoader`；parent 链固定为 application → boot → null。
- linker 为 class 保存 bootstrap/application initiating-loader bit；DEX 注册仅表示
  “已知”，只有实际链接/加载才标记 initiated。defining loader 与 initiating loader
  保持独立。
- `findLoadedClass` 仅查询对应 loader 已发起的 class，不链接、初始化、合成 array，
  也不把已注册 DEX class 误报为 loaded。
- `loadClass(String[, boolean])` 统一经 `ClassNameCodec` 解析 binary name，application
  可委托平台类给 boot，boot 不越权加载 application class；array loader 跟随 component，
  primitive keyword 不可加载，API-19 `resolve` 参数明确忽略且不触发 `<clinit>`。
- `Class.getClassLoader()` 对 primitive 返回 null，对 boot/application class（含 array）
  返回稳定 facade；自定义 `ClassLoader` 只共享 application namespace，不获得动态定义权限。
- 动态 classpath、多 namespace、resource lookup 与自定义 `findClass` 定义不在本 WU；
  缺失 surface 继续明确失败，不伪造成功。

## 验证与裁决

- `tests/dexvm/class_loader_tests.cpp` 覆盖稳定对象与 parent 链、GC 强根、
  `Class.getClassLoader`、known/initiated 分离、无副作用查询、platform delegation、array、
  resolve 不初始化、boot 边界、异常类型和自定义 loader 的有界 namespace。
- 定向回归覆盖 name codec、reflection linker metadata、intrinsic builder/catalog、懒链接
  缺失层级与 class initialization；不跑全量测试和 title gate。
- 新能力记为 `dexvm.class_loader_facade = complete`；reflection wrappers、完整 Class core
  与 reflective invoke/Field/Array 仍由 DVM-64..69 交付。

状态：完成。
