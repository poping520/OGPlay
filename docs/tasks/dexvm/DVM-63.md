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

## Chapter 11 收尾复验

- 补齐 `Class.forName(String)` 与三参数版本：单参数入口使用真实 interpreted caller
  loader 并初始化；三参数 null 按 API19 归 system/application facade，显式 boot 与
  custom facade 分别保持 bootstrap lookup 和 bounded application namespace。
- platform/app/object-array/primitive-array、primitive keyword、missing/linkage、
  initialize false/true 与 init throwable identity 在 switch/threaded 后端一致。

状态：完成（含 Chapter 11 closure 复验）。

## Boot/App 同名注册修复（2026-10-01）

注册 App 时按实际已登记的 bootstrap defining-loader 身份执行 parent-first；不按包名
扩大忽略范围，也不比较或混合两份定义。APK 同包独有类保留；同 DEX 非法重复仍由
ParseDex 在选类前拒绝。既有平台前缀封闭边界及不支持多 namespace 的约束不变。

双解释器回归覆盖同名同/异内容、父类/接口/字段/方法解析、App-only、clinit、
loader/反射/JNI 唯一身份及非法重复。原 APK 无 Profile/无 survey、隔离空沙盒越过
prepare，装载游戏 SO 后进入 onCreate；下一首错为 SurfaceView(Context, AttributeSet)。
Release 受影响目标构建及 10 用例、496 断言通过；证据在
`.local/tales-class-collision-fix/`。这是 reached-fault，未验收完整游戏或 Windows/Linux。
JNI 回归仅覆盖登记身份、可赋值关系及具体类调用；工具层直接以抽象接口 method ID
虚调用的既有路由缺口另记在证据目录，未改动或宣称其分派验收。
