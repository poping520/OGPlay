# 11 · Class、ClassLoader facade 与有界反射基础栈

本章定义 DexVM 在 Android 4.4.4 / API 19 / Dalvik 语义下的 `java.lang.Class`、
`java.lang.ClassLoader` 可观察语义与 `java.lang.reflect.*` 基础能力。目标不是实现
完整 Java Reflection 或真正的多加载器，而是建立可持续扩展的 **类型/成员查询、
单一 loader facade、访问控制、反射调用、Field 与 Array** 内核；底层继续复用
`DexClassLinker`、`JavaObjectModel`、`Interpreter`、GC 与 intrinsic。

AOSP 语义引用一律指向 OGPlay 仓库根目录下的本地 checkout，供后续 AI 直接读取：

```text
.local/aosp/dalvik
.local/aosp/framework/base
.local/aosp/libcore
.local/aosp/libcore/libdvm
.local/aosp/libcore/luni
```

正文引用应落到具体文件与函数/Java 方法，例如：

```text
.local/aosp/dalvik/vm/interp/Stack.cpp :: dvmInvokeMethod
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Method.java :: invoke
```

继续遵守 [07](07-aosp-reference.md)：**只取语义，不移植 AOSP 的对象布局、GC、JNI、
线程或 native 结构。**

---

## 1. 范围

[01 §3](01-scope.md)“完整反射不是目标”继续有效；本章扩展
[03 §5](03-platform-intrinsics.md) 的最小反射面。

### 1.1 交付

- `Class`：名称、类型、层级、assignability、`forName`、declared/public 成员查询；
- `ClassLoader`：唯一 application loader facade、`getClassLoader/loadClass/findLoadedClass` 等有界可观察语义；
- `Method` / `Constructor` / `Field`：真实 metadata、modifiers、常用 Java 行为；
- `AccessibleObject`：per-wrapper accessible flag + caller-based access check；
- `Method.invoke`、`Constructor.newInstance`、`Class.newInstance`；
- Field object/primitive get/set；
- 统一 boxing / unboxing / primitive widening；
- `InvocationTargetException` 保留原 target throwable identity；
- `java.lang.reflect.Array` 的基础读写与 newInstance；
- 必需的 linker metadata、descriptor codec、有限 Dalvik system metadata。

### 1.2 非目标

| 非目标 | 裁决 |
| --- | --- |
| 多 ClassLoader / namespace isolation | 不做；仍是单一 application namespace |
| `DexClassLoader` / `PathClassLoader` / `defineClass` 动态装载 | 不做 |
| 完整 annotation runtime | 不创建用户 annotation proxy |
| generic reflection | `TypeVariable` / `ParameterizedType` / generic signature 延后 |
| `Proxy` / MethodHandle / invokedynamic | 不做 |
| SecurityManager / ReflectPermission | 不实现；`setAccessible` 仅影响 VM access check |
| framework/core 字节码执行 | 不做；平台类仍为 intrinsic |
| reflection wrapper 跨 session 持久化 | 不做 |

允许解析 `InnerClass`、`EnclosingClass`、`EnclosingMethod`、`Throws` 等 Dalvik
system annotations 作为 VM 内部结构元数据；这不等价于 annotation runtime。

---

## 2. 现状与架构裁决

当前 `java_lang_Class.cpp` 只有 `getName()`、`getDeclaredMethods()`，且直接遍历 linker、
手写 `Method` raw slots；当前 `java_lang_reflect_Method.cpp` 仅保存 `VmMethodId + name`，
`invoke` 只支持 0 参数与少量 int-like 返回。

继续在 intrinsic 文件中堆 API 会产生五类问题：

1. `Class` 与 linker 各自实现成员查询；
2. intrinsic 缺完整 access flags，modifiers/access check 错；
3. wrapper raw slot 协议泄漏到多个 intrinsic；
4. reflection 重复实现 dispatch、clinit、boxing、exception；
5. 若共享 guest wrapper，`setAccessible(true)` 会污染后续查询。

因此：

> `Class` 与 `ClassLoader` 都只做 facade；类定义、resolution、link 与 initialization 的唯一
> 事实源仍是 linker。查询、wrapper 物化、access、类型转换和 reflective invoke 由统一
> Reflection Runtime 完成。

```text
Class / ClassLoader / java.lang.reflect.*
                 │
                 ▼
       Reflection Runtime
  ├─ ClassNameCodec / LoaderFacade
  ├─ ReflectionMetadata / Factory
  ├─ AccessController / ReflectionCodec
  └─ ReflectInvoke
                 │
        DexClassLinker + Interpreter
```

`ClassLoader` 禁止维护第二份 `name -> Class` 表，也不是 class-definition authority；它只把
当前单一 namespace 暴露给 guest。未来 Multi-Dex 仍应是“多个 DexUnit，同一个 loader”。

---

## 3. API19 AOSP 语义锚点

实施 AI 应直接读取下列本地源码：

| 能力 | 本地路径 | 重点 |
| --- | --- | --- |
| `Class` Java 半边 | `.local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java` | 名称、forName、member query、copy/aggregate |
| `ClassLoader` Java 半边 | `.local/aosp/libcore/libdvm/src/main/java/java/lang/ClassLoader.java` | parent/loadClass/findLoadedClass/system loader facade |
| VM loader glue | `.local/aosp/libcore/libdvm/src/main/java/java/lang/VMClassLoader.java` | bootstrap lookup/resource/native glue；只取 API19 行为 |
| `Class` native 半边 | `.local/aosp/dalvik/vm/native/java_lang_Class.cpp` | component/modifier/assignability/newInstance/declared members |
| wrapper 物化 | `.local/aosp/dalvik/vm/reflect/Reflect.cpp` / `Reflect.h` | Method/Field/Constructor metadata → object |
| reflective invoke | `.local/aosp/dalvik/vm/interp/Stack.cpp :: dvmInvokeMethod` | args/access/widen/dispatch/boxing/异常包装 |
| `Method` | `.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Method.java`；`.local/aosp/dalvik/vm/native/java_lang_reflect_Method.cpp` | shape + invoke/native 边界 |
| `Constructor` | `.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Constructor.java`；`.local/aosp/dalvik/vm/native/java_lang_reflect_Constructor.cpp` | allocation/direct invoke/异常 |
| `Field` | `.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Field.java`；`.local/aosp/dalvik/vm/native/java_lang_reflect_Field.cpp` | static init/access/conversion/final/volatile |
| `AccessibleObject` | `.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/AccessibleObject.java`；`.local/aosp/dalvik/vm/native/java_lang_reflect_AccessibleObject.cpp` | per-wrapper flag/access bypass |
| `Array` | `.local/aosp/libcore/luni/src/main/java/java/lang/reflect/Array.java`；`.local/aosp/dalvik/vm/native/java_lang_reflect_Array.cpp` | object/primitive get/set/newInstance |
| `Member` / `Modifier` | `.local/aosp/libcore/luni/src/main/java/java/lang/reflect/Member.java`；`.local/aosp/libcore/luni/src/main/java/java/lang/reflect/Modifier.java` | Java-visible flags/masks |
| `InvocationTargetException` | `.local/aosp/libcore/luni/src/main/java/java/lang/reflect/InvocationTargetException.java` | target/cause identity |
| access check | `.local/aosp/dalvik/vm/oo/AccessCheck.cpp` | public/private/protected/package |
| assignability | `.local/aosp/dalvik/vm/oo/TypeCheck.cpp` | receiver/arg/field reference type check |
| class/link/init/loader | `.local/aosp/dalvik/vm/oo/Class.cpp`；`.local/aosp/dalvik/vm/oo/Resolve.cpp` | defining loader、lookup、clinit、member resolution |
| system metadata | `.local/aosp/libcore/dalvik/src/main/java/dalvik/annotation/InnerClass.java`、`EnclosingClass.java`、`EnclosingMethod.java`、`Throws.java`、按需 `MemberClasses.java` | simple/canonical/enclosing/exception types |

`.local/aosp/framework/base` 也可直接读取；需要确认 framework 对反射 API 的真实调用方式时，
引用其中具体源码，不改用 host JDK 或在线最新版。

测试注释统一写：

```text
AOSP API19: .local/aosp/dalvik/vm/interp/Stack.cpp :: dvmInvokeMethod
```

---

## 4. Descriptor / ClassNameCodec

反射会在多个组件重复使用 descriptor 拆分和 Class name 转换，必须先抽成正式 API：

```cpp
struct MethodTypeDescriptor {
    std::vector<std::string> parameters;
    std::string return_type;
};

class ClassNameCodec {
public:
    static MethodTypeDescriptor ParseMethod(std::string_view);
    static bool IsPrimitive(std::string_view);
    static bool IsReference(std::string_view);
    static bool IsArray(std::string_view);
    static std::string ClassGetName(std::string_view descriptor);
    static std::string BinaryNameToDescriptor(std::string_view name);
};
```

关键转换：

```text
Ljava/lang/String;      -> java.lang.String
I                       -> int
V                       -> void
[I                      -> [I
[Ljava/lang/String;     -> [Ljava.lang.String;

java.lang.String        -> Ljava/lang/String;
foo.Bar$Inner           -> Lfoo/Bar$Inner;
[Ljava.lang.String;     -> [Ljava/lang/String;
```

primitive keyword（如 `int`）不能通过 `Class.forName` 找到 primitive Class；primitive Class
由 wrapper `TYPE` 字段等入口获得。现有 primitive class synthesize 路径必须补 `V`。

---

## 5. Linker metadata v2：硬前置

反射不能从 vtable 反推 declared members；`Class.getClassLoader()` 也不能靠 descriptor 前缀
临时猜测。先引入轻量 loader identity：

```cpp
struct VmClassLoaderId final { uint32_t value; };
constexpr VmClassLoaderId kBootstrapLoader{0};
constexpr VmClassLoaderId kApplicationLoader{1};
```

这是 **defining/initiating loader 的语义角色**，不是开放任意 loader graph。application facade 必须
有 guest object；pinned API19 `ClassLoader` parent 行为需要稳定的 `BootClassLoader` 对象。
`Class.getClassLoader()` 的 Java 半边会把 native 返回的 bootstrap null 替换为该
`BootClassLoader` singleton，因此 bootstrap-defined 非 primitive class 公开返回 boot
facade；只有 primitive class 返回 null。不得套用桌面 JVM 常见的 bootstrap-null 描述。
linker metadata 至少需要：

```cpp
struct LinkedClass {
    VmClassLoaderId defining_loader;
    std::vector<DexClassId> direct_interfaces;
    std::vector<DexClassId> interfaces; // flattened/link-time
    std::vector<VmMethodId> own_direct_methods;
    std::vector<VmMethodId> own_virtual_methods;
    std::vector<VmFieldId> own_static_fields;
    std::vector<VmFieldId> own_instance_fields;
};
```

规则：

- platform intrinsic / primitive 归 bootstrap；application DEX 归唯一 application loader；
- array 的 defining-loader 语义跟随 component type，并按 API19 `Class.getClassLoader` 结果验收；
- app DEX 保留 `class_data_item` declared order；
- intrinsic 保留 generated declaration order；
- vtable 只用于 dispatch，不代替 declared methods；
- constructors = own direct 中 `<init>`；
- declared methods = own direct 非 `<init>/<clinit>` + own virtual，过滤 Miranda；
- arrays 不产生可反射 `length` Field。

ClassLoader facade 若实现 `findLoadedClass`，还需区分 **known/registered** 与 **initiated by loader**：
`RegisterDex` 让 linker 知道 class，不等于 Java 层该 loader 已经 initiate 它。当前只有 bootstrap/app
两个角色，可用小型 per-class load-state/initiating flags 表达；禁止因此建立第二份类目录。

`LinkedMethod` 应显式保留 invocation category：

```cpp
enum class DeclaredInvokeKind : uint8_t {
    direct,
    static_call,
    virtual_call,
    interface_call,
};
```

`Method.invoke` 无 `super` 模式：private 精确 direct；static 精确 static；普通 instance
按 receiver 做 virtual/interface dispatch。

### 5.1 Intrinsic flags

Reflection Runtime 开始前，`IntrinsicClassDecl/MethodDecl/FieldDecl` 必须加入完整 raw access
flags 并一路传入 linker。否则 `getModifiers`、public member query、`setAccessible(false)`、
final/volatile/private/static 都不可信。

API19 shape 工具链应升级：

- class/method/field 保存完整 modifiers/access flags；
- reflection-sensitive 类允许保留 private VM fields；
- behavior overlay 不改变 generated shape/flags；
- source roots 显式包含：

```text
.local/aosp/libcore/libdvm/src/main/java
.local/aosp/libcore/luni/src/main/java
```

同 descriptor 在多个 root 出现时 fail closed，不按遍历顺序覆盖。system annotations 单独从
`.local/aosp/libcore/dalvik/src/main/java` 读取。

`getModifiers()` 必须按 class/method/field/constructor 各自 mask 输出，不能把 Dalvik 内部 flags
直接暴露给 guest。

---

## 6. Reflection metadata 与 wrapper

Reflection Runtime 内部保存不可变 metadata，例如：

```cpp
struct ReflectMethodMeta {
    VmMethodId method;
    DexClassId declaring_class;
    uint32_t access_flags;
    DeclaredInvokeKind invoke_kind;
    std::vector<DexClassId> parameter_types;
    DexClassId return_type;
    std::vector<DexClassId> exception_types;
};
```

Field/Constructor 同理。metadata 可缓存；guest wrapper 不共享。

每次 `getDeclared*` / `get*` 查询都重新物化 `Method/Field/Constructor` guest object：

```text
wrapper ref identity 不同
semantic equals 可为 true
setAccessible 只影响当前 wrapper
identityHashCode 各自独立
```

`Class.cpp` 不允许手写 `slots[0] = VmMethodId`。ReflectionFactory 按 API19 wrapper shape 统一写入
fields。核心 shape 至少包括：

```text
Member
AccessibleObject
Modifier
Method
Constructor
Field
Array
InvocationTargetException
```

`Type` / `GenericDeclaration` / `AnnotatedElement` 可先只保证 hierarchy/shape；generic/annotation
行为继续 deferred。

reflection `slot` 是 opaque token/ordinal，不等于 `VmMethodId`、DEX method index 或 field index：

```text
wrapper -> declaring Class + reflection slot -> linker metadata -> linked member
```

这样未来 DexUnit/multi-Dex 不需修改 guest wrapper 协议。

---

## 7. ClassLoader facade 与加载语义

ClassLoader 本轮只解决 **Class/reflection 必需的 Java 可观察行为**。运行时仍只有 bootstrap 与
唯一 application loader 两个语义角色；不因此开放多 application namespace、动态 DEX 或自定义
class-definition authority。

语义首先读取：

```text
.local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/ClassLoader.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/VMClassLoader.java
.local/aosp/dalvik/vm/oo/Class.cpp
```

### 7.1 对外能力

| API | 本轮语义 |
| --- | --- |
| `Class.getClassLoader()` | API19 public Java 半边：primitive 返回 null；bootstrap-defined 非 primitive 返回 `BootClassLoader` singleton；application 返回 system facade；array 跟随 component |
| `ClassLoader.getSystemClassLoader()` | 返回稳定的唯一 application loader facade |
| `getParent()` | 仅在实现该 surface 时按 pinned `ClassLoader.java` 的 System/Boot 关系；需要时物化 bootstrap facade，禁止猜 null |
| `loadClass(String)` / `loadClass(String, boolean)` | 复用 API19 delegation 顺序的**语义结果**，最终仍委托唯一 linker namespace；不读新 dex/jar |
| `findLoadedClass(String)` | 无副作用查询 initiating/load-state；mere `RegisterDex` 不自动等于 initiated |
| `Class.forName(String)` | 按 pinned `Class.java` 使用 calling class loader；DexVM 把 caller class 映射到 bootstrap/application 角色 |
| `Class.forName(name, initialize, loader)` | `null` loader 的行为严格按本地 API19 `Class.java`；只接受 DexVM 已知 loader facade/语义角色，不成为动态加载入口 |

资源加载 (`getResource*`) 不属于本反射基础依赖；真实命中时另按 VFS/classpath 能力评估。

### 7.2 名称、lookup、link 与 init

```text
caller/loader -> ClassNameCodec -> loader role -> linker lookup/resolve -> optional link/init
```

- primitive keyword（`int` 等）不能经 `forName/loadClass` 得到 primitive Class；
- array binary name 按 API19 codec 处理，可走现有 array synthesize；
- `forName(..., false, ...)` 不执行 `<clinit>`；`true` 只触发既有 class-init state machine；
- CNFE、LinkageError、ExceptionInInitializerError 的 wrapping/unwrapping 顺序照本地 `Class.java`，禁止“一律 CNFE”；
- `findLoadedClass` 不 synthesize、不 link、不 init；
- application `loadClass` 对 platform class 的 delegation 结果可以记录“app 为 initiating loader、bootstrap 为 defining loader”；
- guest-created/subclassed `ClassLoader` 不获得独立 namespace 或 `defineClass` 权限；override `findClass` 若试图动态定义类，明确 unsupported。

### 7.3 Multi-Dex 兼容形态

```text
Application VmClassLoaderId
  ├─ DexUnit(classes.dex)
  ├─ DexUnit(classes2.dex)
  └─ DexUnit(classesN.dex)
```

Multi-DexUnit 与 Multi-ClassLoader 是两个问题。未来增加 DexUnit 只扩展 provenance/search order，不改变
application loader identity；reflection wrapper 同样不得把 DEX-local index 当全局 member identity。

---

## 8. Class structural core

优先实现不依赖 reflective invoke 的查询：

| API | 语义 |
| --- | --- |
| `getName` | §4 codec |
| `getSimpleName` | array 递归 `[]`；member/local/anonymous 依赖 system metadata |
| `getSuperclass` | Object/interface/primitive/void -> null；array -> Object |
| `getInterfaces` | 只返回 direct interfaces；array -> Cloneable + Serializable |
| `getComponentType` | array -> 一维 component；否则 null |
| `getModifiers` | Java-visible flags；InnerClass metadata 可覆盖 class_def flags |
| `isArray/isInterface/isPrimitive/isEnum/isSynthetic` | metadata/flags |
| `isInstance` | null -> false；否则 linker assignability |
| `isAssignableFrom` | null -> NPE；否则 linker assignability |
| `cast/asSubclass` | assignability + 正确异常 |
| `toString` | primitive 名；否则 `class ` / `interface ` + getName |

---

## 9. Class member query

ReflectionMetadata 统一实现 declared/public lookup；intrinsic 不自行遍历 linker。

### 9.1 Declared

- 只看 receiver class 自己声明的成员；
- methods 排除 `<init>/<clinit>`、Miranda；
- constructors 只取 `<init>`，不继承；
- fields = own static + instance；
- array/primitive/void 返回 empty member arrays；
- lookup miss -> `NoSuchMethodException` / `NoSuchFieldException`；
- `name == null` -> NPE；null parameter array 按 API19 契约作为 empty 处理。

### 9.2 Public

`getMethods/getMethod` 不能直接包装 vtable：需聚合 class/super/interfaces 的 public methods，
包括 static；override/covariant/bridge 按 semantic signature 稳定去重，更派生声明优先。

`getFields/getField` 聚合 current/super/interfaces 的 public fields；constructors 从不继承，只看
当前 class public constructors。

返回顺序虽通常不属于 Java 契约，OGPlay 仍需 deterministic：app 保留 DEX declaration order；
intrinsic 保留 generated order；递归聚合按 API19 traversal 稳定去重；禁止按全局 ID 数字排序。

---

## 10. AccessibleObject 与 caller access

`AccessibleObject` 至少实现：

```text
isAccessible()
setAccessible(boolean)
static setAccessible(AccessibleObject[], boolean)
```

flag 属于 wrapper，不属于 member metadata。

Reflection Runtime 必须取得真实 caller class；不能把 caller 固定成 `Method` intrinsic。
AccessController 对照：

```text
.local/aosp/dalvik/vm/oo/AccessCheck.cpp
```

覆盖 public/private/package/protected 与 protected receiver restriction。
`setAccessible(true)` 只跳过 Java language access check，不跳过 null/type/VM invariant。

runtime package identity 从现在就定义为 `(defining_loader, package)`；当前 application loader 唯一，
避免未来 Multi-ClassLoader 时重写 access contract。

---

## 11. ReflectionCodec

Method、Constructor、Field、Array 必须共用一套 Object/VmValue/primitive 转换。

允许 primitive widening：

```text
byte  -> short/int/long/float/double
short -> int/long/float/double
char  -> int/long/float/double
int   -> long/float/double
long  -> float/double
float -> double
```

禁止 narrowing，boolean 不与 numeric 互转。reference 使用 linker assignability；null 可传 reference，
不可传 primitive。

AOSP 锚点：

```text
.local/aosp/dalvik/vm/interp/Stack.cpp
.local/aosp/dalvik/vm/native/java_lang_reflect_Field.cpp
.local/aosp/dalvik/vm/native/java_lang_reflect_Array.cpp
```

返回：primitive -> 对应 wrapper；reference -> 原 VmObjectRef；void -> null；J/D 保持 wide bits。
不允许所有 int-like 都包装为 `Integer`。

---

## 12. Method.invoke

语义锚点：

```text
.local/aosp/dalvik/vm/interp/Stack.cpp :: dvmInvokeMethod
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Method.java :: invoke
```

调用路径：

```text
resolve metadata
 -> validate receiver / arg count
 -> caller access check（除非 accessible=true）
 -> unbox/widen
 -> target selection
      static    -> exact static
      direct    -> exact private/direct
      virtual   -> receiver vtable
      interface -> receiver interface dispatch
 -> static clinit
 -> Interpreter::Call
 -> target exception ? InvocationTargetException : box return
```

异常规则：non-static null receiver -> NPE；wrong receiver/args -> `IllegalArgumentException`；access ->
`IllegalAccessException`；`args == null` 等价 0 参数。

目标抛 E 时必须新建 `InvocationTargetException` wrapper，但其中 target/cause 指向 **原 E 的同一
VmObjectRef**；禁止按 descriptor/message 重新物化。复用 DVM-61 已建立的 existing-throwable 能力。

---

## 13. Constructor 与 Class.newInstance

### 13.1 `Constructor.newInstance`

- abstract/interface/array/primitive 等不可实例化 -> `InstantiationException`；
- access check / setAccessible；
- 分配 instance；
- args 经 ReflectionCodec；
- 精确 direct 调该 `<init>`；
- target E -> `InvocationTargetException(target=同一 E)`。

锚点：

```text
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Constructor.java
.local/aosp/dalvik/vm/native/java_lang_reflect_Constructor.cpp
```

### 13.2 `Class.newInstance`

关键差异：默认无参 ctor 抛出的 E **直接传播，不包装**。还需覆盖无 `()V`、access、clinit、
interface/abstract/array/primitive/void。

锚点：

```text
.local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java
.local/aosp/dalvik/vm/native/java_lang_Class.cpp
```

---

## 14. Field

规则：static field 忽略 receiver 但先初始化 declaring class；instance null -> NPE；wrong receiver ->
`IllegalArgumentException`；access -> `IllegalAccessException`；reference set 做 assignability；primitive
Object set 做 unbox/widen；final 写入行为按 API19，不自行发明 relaxed 规则。

实现全部 primitive getter/setter：

```text
getBoolean/getByte/getChar/getShort/getInt/getLong/getFloat/getDouble
setBoolean/setByte/setChar/setShort/setInt/setLong/setFloat/setDouble
```

共用 ReflectionCodec widening matrix。

`volatile` flag 必须正确反射。当前 `VmExecutionLock` 的串行执行不等于完整 JMM volatile；本阶段
不另造 host 原子优化。

锚点：

```text
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Field.java
.local/aosp/dalvik/vm/native/java_lang_reflect_Field.cpp
```

---

## 15. java.lang.reflect.Array

补齐：

```text
getLength
get / set
getBoolean/getByte/getChar/getShort/getInt/getLong/getFloat/getDouble
setBoolean/setByte/setChar/setShort/setInt/setLong/setFloat/setDouble
newInstance(Class, int)
newInstance(Class, int[])
```

object array 保留真实 VmObjectRef；reference set 做 assignability；primitive get/set 共用 widening；
negative size -> `NegativeArraySizeException`；non-array -> `IllegalArgumentException`；index ->
`ArrayIndexOutOfBoundsException`；`Void.TYPE` element -> `IllegalArgumentException`。

multi-dimensional array 继续创建真实 typed array class，不用 Object[] 代替。

锚点：

```text
.local/aosp/libcore/luni/src/main/java/java/lang/reflect/Array.java
.local/aosp/dalvik/vm/native/java_lang_reflect_Array.cpp
```

---

## 16. Dalvik system metadata

只解析反射所需：

```text
Ldalvik/annotation/InnerClass;
Ldalvik/annotation/EnclosingClass;
Ldalvik/annotation/EnclosingMethod;
Ldalvik/annotation/Throws;
Ldalvik/annotation/MemberClasses;   // 按需
```

定义直接读：

```text
.local/aosp/libcore/dalvik/src/main/java/dalvik/annotation/InnerClass.java
.local/aosp/libcore/dalvik/src/main/java/dalvik/annotation/EnclosingClass.java
.local/aosp/libcore/dalvik/src/main/java/dalvik/annotation/EnclosingMethod.java
.local/aosp/libcore/dalvik/src/main/java/dalvik/annotation/Throws.java
```

loader 输出 host metadata，不物化 guest Annotation 对象。
`getSimpleName/getCanonicalName/getDeclaringClass/getEnclosing*` 不允许用 `$` split 猜测 inner/local/
anonymous；按 API19 Class.java + system metadata 裁决。

---

## 17. Wrapper Java 行为

### 17.1 Method

`getDeclaringClass/getName/getModifiers/getReturnType/getParameterTypes/getExceptionTypes`、
`equals/hashCode/toString/invoke`。

API19 `Method.hashCode()` 只返回 method name 的 `String.hashCode()`，不与 declaring
class name 异或；`toString()` 使用 `Modifier.toString` 的 JLS 顺序、return type 的
`Class.getName()`，parameter/throws 使用 `AccessibleObject.appendTypeName` 的数组格式。

### 17.2 Constructor

`getDeclaringClass/getModifiers/getParameterTypes/getExceptionTypes`、
`equals/hashCode/toString/newInstance`。

API19 `Constructor.hashCode()` 是 declaring class binary name 的 `String.hashCode()`。

### 17.3 Field

`getDeclaringClass/getName/getType/getModifiers`、`equals/hashCode/toString`、object/primitive get/set。

API19 `Field.hashCode()` 是 field name 与 declaring class binary name 两个
`String.hashCode()` 的异或；`toString()` 的 type/declaring class 使用
`appendTypeName`，包括 human-readable array suffix。

parameter/exception Class[] 返回 defensive copy。generic/annotation API 可 explicit-unimplemented，但 shape
必须来自 pinned API19，不能用 host JDK 同名类替代。

---

## 18. Java 异常矩阵

| 情况 | 结果 |
| --- | --- |
| `forName` 找不到 | `ClassNotFoundException` |
| method lookup miss | `NoSuchMethodException` |
| field lookup miss | `NoSuchFieldException` |
| instance Method/Field receiver null | NPE |
| receiver/type/arg/unbox/widen 不兼容 | `IllegalArgumentException` |
| access 不允许 | `IllegalAccessException` |
| Method target 抛 E | `InvocationTargetException(target=同一 E)` |
| Constructor target 抛 E | `InvocationTargetException(target=同一 E)` |
| Class.newInstance ctor 抛 E | 原 E，不包装 |
| abstract/interface/array/primitive 构造 | `InstantiationException` |
| static Field/Method 首次访问 | 初始化 declaring class |
| Array 参数非数组 | `IllegalArgumentException` |
| array index 越界 | `ArrayIndexOutOfBoundsException` |
| `newInstance(Void.TYPE, ...)` | `IllegalArgumentException` |

异常类型与检查顺序以第 3 节本地 AOSP 路径为准。已定义的 Java reflection failure 不得漏成
`DexVmError`；后者只留给 VM invariant/malformed image/internal unsupported path。

---

## 19. GC、identity、缓存、Multi-Dex

Class object 继续使用 `JavaObjectModel::ClassObject(DexClassId)` permanent root；唯一 application
ClassLoader facade 也作为 session permanent root。DVM-61 identity 规则不变。

Method/Field/Constructor wrapper 是普通 guest instance：reference 放正常 slots、GC 自动追踪、可回收；
metadata cache 不保存 guest `VmObjectRef`；禁止新增 raw `VmObjectRef.Value()` keyed reflection side table。

允许缓存 immutable metadata、parsed descriptor、public aggregate、decoded system metadata；不缓存 guest
wrapper、caller access decision、Field 实际值、invoke result。

reflection slot 不得定义为 DEX member index：

```text
wrapper -> declaring Class + opaque slot -> linker metadata -> linked member -> defining DexUnit（未来）
```

这样将来 Multi-Dex 只扩展 linked metadata/DexUnit provenance，不改 guest wrapper 或 ClassLoader identity。

---

## 20. 建议代码边界

基础设施：

```text
include/ogplay/runtime/dexvm/reflection.h
include/ogplay/runtime/dexvm/class_loader_facade.h
src/runtime/dexvm/reflection.cpp
src/runtime/dexvm/class_loader_facade.cpp
src/runtime/dexvm/reflection_metadata.cpp
src/runtime/dexvm/reflection_codec.cpp
src/runtime/dexvm/reflection_access.cpp
```

intrinsic：

```text
java_lang_Class.cpp
java_lang_ClassLoader.cpp
java_lang_reflect_AccessibleObject.cpp
java_lang_reflect_Member.cpp
java_lang_reflect_Modifier.cpp
java_lang_reflect_Method.cpp
java_lang_reflect_Constructor.cpp
java_lang_reflect_Field.cpp
java_lang_reflect_Array.cpp
java_lang_reflect_InvocationTargetException.cpp
```

同时修改 linker/intrinsic builder/API19 surface generator。目标是 `java_lang_Class.cpp` 变薄，而不是
扩张成新的 linker/reflection runtime。

---

## 21. 测试与验收

建议新增：

```text
tests/dexvm/reflection_tests.cpp
tests/dexvm/fixtures/reflection.dexasm
```

fixture 覆盖 Base/Derived/interfaces/visibility/fields/throwing/constructors/abstract/inner class，包含
public/protected/package/private、static/final/volatile、override/interface、primitive/reference/wide。

### 21.1 Class/query

- object/primitive/void/array name；multidimensional component；
- array interfaces = Cloneable + Serializable；direct interfaces 不混 inherited；
- superclass/interface/primitive null rules；assignability/instance；
- catalog reorder 不改变 Class identity/hash/name/member semantic order。

### 21.2 ClassLoader facade

- app/platform/primitive/array `getClassLoader` 与 pinned API19 一致；system loader identity 稳定；
- `forName(String)` 使用 calling-loader 角色；显式 null-loader 路径按本地 `Class.java`；
- `loadClass` delegation 结果正确；bootstrap-defined class 可被 application loader 记录为 initiating；
- `findLoadedClass` 区分 registered 与 initiated，且完全无副作用；
- `forName(false)` 不 clinit，true 恰好一次；primitive keyword -> CNFE；
- app/platform/object-array/primitive-array、missing/linkage/init failure 在 switch/threaded
  后端一致；init throwable 保留已有 guest identity；
- guest-created loader / 动态 loader 路径明确失败，不暗中定义 class；
- catalog reorder 不改变 defining loader；多 DexUnit 夹具仍共享一个 application loader。

### 21.3 Wrapper/access

- repeated query 返回不同 wrapper ref，但 semantic equals；
- `setAccessible` 只影响当前 wrapper；
- parameter/exception arrays defensive copy；
- app/intrinsic modifiers 正确；constructors/Miranda 不混 declared methods。

### 21.4 Method.invoke

switch/threaded 都覆盖 static、virtual override、interface、private direct、null/wrong receiver、null args、
0/1/N args、reference assignability、全部 widening、J/D、boxing、void->null、reference identity、target
throwable identity。

### 21.5 Constructor/Class.newInstance

覆盖 public/private + accessible、abstract/interface/array/primitive、clinit、arg conversion、Constructor target
包装、Class.newInstance target 不包装、无 `()V`。

### 21.6 Field/Array

Field：static/instance/ref/all primitive、clinit、receiver/access/final/widen/assignability/GC。
Array：object/primitive/multidimensional、get/set/widen/type/null/non-array/index/negative-size/void。

---

## 22. 实施顺序

```text
R0  Descriptor / ClassNameCodec
 │
R1  Linker reflectable metadata + intrinsic flags + defining-loader metadata
 │
R2  Bootstrap/application loader roles + system facade + forName/loadClass contract
 │
R3  ReflectionMetadata + ReflectionFactory + API19 wrapper shape
 │
R4  Class structural + declared/public member query
 │
R5  ReflectionCodec + caller access + Method.invoke + InvocationTargetException
 │
R6  Constructor.newInstance + Class.newInstance + Field
 │
R7  reflect.Array
 │
R8  Dalvik system metadata：InnerClass / Enclosing* / Throws / MemberClasses
```

`Throws` 可按需要提前到 R3；`InnerClass/Enclosing*` 可随 simple/canonical name 第二批开启。
R2 必须保持小：只建立 defining/initiating loader identity、system/bootstrap facade 与 lookup contract，
不顺带实现动态 classpath、独立 guest loader namespace。

每批完成定义：

1. 测试注释记录具体 `.local/aosp/...` 文件 + 函数/Java 方法；
2. 涉及 invoke 时 switch/threaded 一致；
3. Java exception type/时机有正反例；
4. capability/module 文档同步；
5. 未实现 surface 明确失败，不返回 neutral placeholder；
6. 不引入 title-specific 分支。

---

## 23. 禁止捷径

```text
× Class.cpp 手写 Method/Field raw slots
× Method.invoke 直接 vm.Call(declared VmMethodId) 而不 dynamic dispatch
× invoke 只支持 0 参数
× primitive 全部按 int
× target exception 转 RuntimeException 或重新物化
× getMethods 直接返回 vtable
× getInterfaces 返回 flattened interface set
× intrinsic modifiers 用 0/public 猜测
× setAccessible 修改全局 metadata
× getSimpleName 用 '$' split 猜 inner class
× forName 任意失败都转 CNFE
× ClassLoader intrinsic 自建 name->Class 缓存或第二套 resolution
× 把 RegisterDex 已知 class 全部冒充为 findLoadedClass 已 initiated
× 为 loadClass 顺带实现 DexClassLoader/defineClass/动态 classpath
× reflection slot 暴露 DEX method/field index
× wrapper 使用 raw VmObjectRef-keyed host side table
```

---

## 24. 最终成功标准

1. `Class.forName`、`getClassLoader`、有界 `ClassLoader.loadClass/findLoadedClass` 与类型/层级查询可真实使用；
2. bootstrap/application defining + initiating loader 语义稳定；多 DexUnit 不改变 application loader identity；
3. declared/public member query 与 `Method.invoke` 覆盖 static/direct/virtual/interface、primitive/reference/wide；
4. Constructor 与 `Class.newInstance` 的异常差异符合 API19 Dalvik；
5. Field 支持真实 slots、clinit、access、assignability、primitive widening；
6. `AccessibleObject` 为 per-wrapper 状态；`InvocationTargetException` 保留原 target throwable；
7. app/intrinsic access flags 可真实反射；
8. wrapper 可 GC，无 guest-ref host cache 泄漏；
9. catalog reorder、GC handle reuse、switch/threaded 不改变 Class/reflection/loader 语义；
10. generic/annotation、多 ClassLoader、动态加载等非目标明确失败，不用假数据伪装成功。

上述 closure 已由 DVM-62～69 及 Chapter 11 收尾验收闭合：两个 `Class.forName`
重载、API19 wrapper hash/string 行为和 system-annotation fail-closed 校验均在现有
`ClassLoaderFacade` / `ReflectionRuntime` / `ReflectionCodec` 边界内完成。DexVM 因此
拥有可继续扩展的 **Class + ClassLoader facade + bounded reflection foundation**，而不是
`java_lang_Class.cpp` 的零散补丁集合。

---

## 25. 本地 AOSP 读取纪律

实施 AI 直接读取本文 §3 的 `.local/aosp/...` 锚点，不用 host JDK 或在线最新版替代。
首个 Reflection/ClassLoader WU 前至少确认：

```text
.local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/ClassLoader.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/VMClassLoader.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Method.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Constructor.java
.local/aosp/libcore/libdvm/src/main/java/java/lang/reflect/Field.java
.local/aosp/dalvik/vm/native/java_lang_Class.cpp
.local/aosp/dalvik/vm/reflect/Reflect.cpp
.local/aosp/dalvik/vm/interp/Stack.cpp
.local/aosp/dalvik/vm/oo/AccessCheck.cpp
```

若本地 checkout 缺文件，应先修本地 AOSP 资料；禁止退回 host JDK、在线 master、ART 或凭记忆替代
API19 Dalvik 语义。
