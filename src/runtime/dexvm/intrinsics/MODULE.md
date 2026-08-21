# 模块：runtime/dexvm/intrinsics

intrinsic 的逻辑单位仍然是 Java class：每个 class 恰好一个 `Declare_*()`，
handler 与对应 `Declare_*` 同址；`catalog.cpp` 只负责显式聚合，
`shared.h` 只保存跨类复用的内部 helper。

文件组织默认仍是一类一个同名源文件。例外是 Android 4.4.4 `java.lang`
Throwable hierarchy、primitive wrapper family 与接口 family，分别统一位于
`java_lang_throwables.cpp`、`java_lang_primitive_wrappers.cpp` 和
`java_lang_interfaces.cpp`。Java class 仍是
一等逻辑单位，每类保留独立 `Declare_*()`，但这些函数为 family TU-private；
文件只向 `catalog.h` 暴露对应的 `AppendJavaLangThrowables()`、
`AppendJavaLangPrimitiveWrappers()` 或 `AppendJavaLangInterfaces()`，
`catalog.cpp` 不感知 family 内具体 class。
后续 API-family 沿用相同规则。family TU 为控制翻译单元数量允许超过通常 800 行。
禁止新增 `misc`/`common`/`all` 等无语义聚合文件、字符串 core handler id、
全局静态自注册，以及 android.* 声明和行为顺手修改。

`java_lang_interfaces.cpp` 覆盖 pinned libcore `java.lang` 顶层 8 个
interface；方法表按 Luni 源码建模。已有 `CharSequence.length` handler 保持
不变，其余接口方法（含 `Readable.read(CharBuffer)`）为显式
`UnimplementedVirtual`，不伪造成功。不纳入 `Thread.UncaughtExceptionHandler`
与 `java.lang.annotation`。

`java_lang_Thread.cpp` 是 pinned libcore `Thread.java`/`VMThread.java` 的 core
façade：声明 Java-visible fields 并负责参数校验/Java exception，生命周期、
execution context、parking、interrupt、sleep/join 与 identity mapping 全部委托
`VmThreadRuntime`/`VmMonitorTable`。`start()` 必须 virtual-dispatch `this.run()`；
基类 `run()` 才 virtual-dispatch target Runnable。纳秒在统一毫秒 Clock 上向上取整；
priority 与 daemon 仅是明确有界的 guest fact。七个字段通过 builder 的预绑定 handle
访问，handler 参数与字段值统一走 `IntrinsicCall`，不得恢复逐调用 descriptor 查找和
裸 instance slot 编解码。

`java.lang.System` 的 `getProperty`/`setProperty` 与 primitive wrapper property
API 共用每 VM 属性表；默认只发布
API 19 guest 可确定的 `/`、`:`、`\n` 三个 separator 属性，不读取宿主系统属性。
未知 key 返回 null，null/空 key 与 null value 按 Java 异常语义失败。

`java.lang.ClassLoader`、`java.lang.BootClassLoader` 与
`dalvik.system.PathClassLoader` 分别保持一类一文件；对象身份、parent 与 lookup/
initiate 状态委托 `ClassLoaderFacade`。动态 classpath 构造器显式未实现，自定义
ClassLoader 仅映射到唯一 application namespace，不获得第二套 class directory。

reflection R3 shape 按一类一文件声明 `AnnotatedElement`、`GenericDeclaration`、`Type`、
`Member`、`AccessibleObject`、`Modifier`、`Method`、`Constructor` 与 `Field`。
`Class` 的结构、类型关系和 declared/public Method/Constructor/Field 查询只能调用
linker/`ReflectionRuntime`，禁止读写 raw member id；public 聚合顺序固定为
class → superclass → direct interface 递归。nested/enclosing、annotation/generic、完整 invoke、
Constructor 实例化与 Field/Array 行为仍须明确 deferred。
