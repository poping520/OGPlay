# DVM-40 · java.lang Throwable hierarchy 聚合与 API 19 class shape 补齐

## 目标（一句话）

将 Android 4.4.4 `java.lang` 下完整 Throwable hierarchy 的 DexVM intrinsic
统一聚合到 `src/runtime/dexvm/intrinsics/java_lang_throwables.cpp`，并补齐当前
OGPlay 缺失的 Throwable 派生类 class shape；已有 handler 行为保持不变。

## 依赖

- DVM-35
- DVM-37

## Source of truth

- 本地源码：`.local/asop/libcore/luni/src/main/java/java/lang/`
- Android tag：`android-4.4.4_r2.0.1`

本 WU 的 inventory、direct superclass 与新增 class shape 只取自上述源码，
不以当前 catalog 或预设类数量为事实来源。范围只含包 `java.lang` 的顶层类，
不递归纳入 `java.lang.annotation`、`java.lang.ref`、`java.lang.reflect` 等子包。

## 基线盘点

对本地目录的顶层 Java 源文件提取 public class 与 direct superclass，再沿父类
闭包筛选直接或间接继承 `Throwable` 的类，实际得到 **50** 个类。catalog 与
同名实现 TU 交叉核对得到：OGPlay 已有 26 个 `Declare_*`，且 26 个均已有
绑定 handler；本 WU 新增 24 个。`ClassNotFoundException` 当前已声明，但其
superclass 错写为 `Exception`，本 WU 按源码修正为
`ReflectiveOperationException`。

| Java class descriptor | direct superclass | 已有 Declare_* | 已有 handler | 本 WU 新增 |
| --- | --- | --- | --- | --- |
| `Ljava/lang/AbstractMethodError;` | `Ljava/lang/IncompatibleClassChangeError;` | 否 | 否 | 是 |
| `Ljava/lang/ArithmeticException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/ArrayIndexOutOfBoundsException;` | `Ljava/lang/IndexOutOfBoundsException;` | 是 | 是 | 否 |
| `Ljava/lang/ArrayStoreException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/AssertionError;` | `Ljava/lang/Error;` | 否 | 否 | 是 |
| `Ljava/lang/ClassCastException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/ClassCircularityError;` | `Ljava/lang/LinkageError;` | 否 | 否 | 是 |
| `Ljava/lang/ClassFormatError;` | `Ljava/lang/LinkageError;` | 否 | 否 | 是 |
| `Ljava/lang/ClassNotFoundException;` | `Ljava/lang/ReflectiveOperationException;` | 是 | 是 | 否 |
| `Ljava/lang/CloneNotSupportedException;` | `Ljava/lang/Exception;` | 否 | 否 | 是 |
| `Ljava/lang/EnumConstantNotPresentException;` | `Ljava/lang/RuntimeException;` | 否 | 否 | 是 |
| `Ljava/lang/Error;` | `Ljava/lang/Throwable;` | 是 | 是 | 否 |
| `Ljava/lang/Exception;` | `Ljava/lang/Throwable;` | 是 | 是 | 否 |
| `Ljava/lang/ExceptionInInitializerError;` | `Ljava/lang/LinkageError;` | 否 | 否 | 是 |
| `Ljava/lang/IllegalAccessError;` | `Ljava/lang/IncompatibleClassChangeError;` | 否 | 否 | 是 |
| `Ljava/lang/IllegalAccessException;` | `Ljava/lang/ReflectiveOperationException;` | 否 | 否 | 是 |
| `Ljava/lang/IllegalArgumentException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/IllegalMonitorStateException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/IllegalStateException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/IllegalThreadStateException;` | `Ljava/lang/IllegalArgumentException;` | 是 | 是 | 否 |
| `Ljava/lang/IncompatibleClassChangeError;` | `Ljava/lang/LinkageError;` | 否 | 否 | 是 |
| `Ljava/lang/IndexOutOfBoundsException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/InstantiationError;` | `Ljava/lang/IncompatibleClassChangeError;` | 否 | 否 | 是 |
| `Ljava/lang/InstantiationException;` | `Ljava/lang/ReflectiveOperationException;` | 否 | 否 | 是 |
| `Ljava/lang/InternalError;` | `Ljava/lang/VirtualMachineError;` | 否 | 否 | 是 |
| `Ljava/lang/InterruptedException;` | `Ljava/lang/Exception;` | 是 | 是 | 否 |
| `Ljava/lang/LinkageError;` | `Ljava/lang/Error;` | 是 | 是 | 否 |
| `Ljava/lang/NegativeArraySizeException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/NoClassDefFoundError;` | `Ljava/lang/LinkageError;` | 是 | 是 | 否 |
| `Ljava/lang/NoSuchFieldError;` | `Ljava/lang/IncompatibleClassChangeError;` | 否 | 否 | 是 |
| `Ljava/lang/NoSuchFieldException;` | `Ljava/lang/ReflectiveOperationException;` | 否 | 否 | 是 |
| `Ljava/lang/NoSuchMethodError;` | `Ljava/lang/IncompatibleClassChangeError;` | 否 | 否 | 是 |
| `Ljava/lang/NoSuchMethodException;` | `Ljava/lang/ReflectiveOperationException;` | 否 | 否 | 是 |
| `Ljava/lang/NullPointerException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/NumberFormatException;` | `Ljava/lang/IllegalArgumentException;` | 是 | 是 | 否 |
| `Ljava/lang/OutOfMemoryError;` | `Ljava/lang/VirtualMachineError;` | 是 | 是 | 否 |
| `Ljava/lang/ReflectiveOperationException;` | `Ljava/lang/Exception;` | 否 | 否 | 是 |
| `Ljava/lang/RuntimeException;` | `Ljava/lang/Exception;` | 是 | 是 | 否 |
| `Ljava/lang/SecurityException;` | `Ljava/lang/RuntimeException;` | 否 | 否 | 是 |
| `Ljava/lang/StackOverflowError;` | `Ljava/lang/VirtualMachineError;` | 是 | 是 | 否 |
| `Ljava/lang/StringIndexOutOfBoundsException;` | `Ljava/lang/IndexOutOfBoundsException;` | 是 | 是 | 否 |
| `Ljava/lang/ThreadDeath;` | `Ljava/lang/Error;` | 否 | 否 | 是 |
| `Ljava/lang/Throwable;` | `Ljava/lang/Object;` | 是 | 是 | 否 |
| `Ljava/lang/TypeNotPresentException;` | `Ljava/lang/RuntimeException;` | 否 | 否 | 是 |
| `Ljava/lang/UnknownError;` | `Ljava/lang/VirtualMachineError;` | 否 | 否 | 是 |
| `Ljava/lang/UnsatisfiedLinkError;` | `Ljava/lang/LinkageError;` | 是 | 是 | 否 |
| `Ljava/lang/UnsupportedClassVersionError;` | `Ljava/lang/ClassFormatError;` | 否 | 否 | 是 |
| `Ljava/lang/UnsupportedOperationException;` | `Ljava/lang/RuntimeException;` | 是 | 是 | 否 |
| `Ljava/lang/VerifyError;` | `Ljava/lang/LinkageError;` | 否 | 否 | 是 |
| `Ljava/lang/VirtualMachineError;` | `Ljava/lang/Error;` | 是 | 是 | 否 |

## 方案

- 所有 `java.lang` Throwable 派生类进入 `java_lang_throwables.cpp`。
- 每个 Java 类仍保留独立的 `Declare_java_lang_Xxx()`。
- `catalog.h` 仍逐类声明，`catalog.cpp` 仍逐类注册。
- 不增加 `DeclareJavaLangThrowables()` 等家族级接口。
- 删除属于该 hierarchy 的旧单类 `.cpp`。
- 已有声明和 handler 机械迁移；新增类的 descriptor、direct superclass、
  constructors、declared methods 与必要字段按本地 Luni 源码建模。

## WU 文件预算偏离

本 WU 因大量旧 `.cpp` 删除会超过 AGENTS.md 通常的 10 文件预算，这是预期
偏离：本质是机械 TU 聚合，`Declare_*` 逻辑边界不变，大部分已有实现原样移动，
并删除大量一类一文件的旧 TU；全量测试兜底。处理方式沿用 DVM-35 对大量文件
机械迁移的先例。

## 边界（不做）

- 不改 Throwable/Exception/Error 已有运行语义。
- 不重构 `shared.h`。
- 不顺手抽象重复 constructor handler。
- 不做异常性能优化。
- 不改 `java.io`/`java.net`/`java.util` 异常。
- 不动 `android.*` intrinsic。
- 不做与 Throwable hierarchy 无关的 intrinsic 整理。

## 验收

1. 固定上述 Android 4.4.4 inventory，断言 `CoreIntrinsicCatalog()` 中 50 个
   descriptor 全部存在且各恰好一次。
2. 逐类断言 direct superclass 与上述 inventory 完全一致。
3. 抽样断言特殊类的 constructor/method/field shape，至少覆盖
   `AssertionError`、`ClassNotFoundException`、
   `EnumConstantNotPresentException`、`ExceptionInInitializerError` 与
   `TypeNotPresentException`，不能只覆盖简单 RuntimeException。
4. 既有 `NullPointerException`、`ArithmeticException`、
   `IllegalMonitorStateException`、`IllegalThreadStateException`、
   `StackOverflowError`、`OutOfMemoryError`、`UnsatisfiedLinkError` 解释器行为
   回归保持通过。
5. 结构测试断言 hierarchy 旧单类 `.cpp` 不残留，唯一实现 TU 为
   `java_lang_throwables.cpp`（`catalog.cpp` 除外）。
6. `cmake --preset dev`、`cmake --build --preset dev`、
   `ctest --preset dev` 全部通过，并通过 `git diff --check`。

## 结果

- 本地 `android-4.4.4_r2.0.1` Luni 源码盘点得到 50 个顶层
  `java.lang` Throwable classes；OGPlay 原有 26 个，本次新增 24 个。
- 26 个旧单类 `.cpp` 已删除，50 个独立 `Declare_java_lang_Xxx()` 统一位于
  `java_lang_throwables.cpp`；最终 934 行。catalog 继续逐类声明、逐类注册，
  未增加家族级接口。
- `ClassNotFoundException` 的 direct superclass 从旧 catalog 的 `Exception`
  修正为源码规定的 `ReflectiveOperationException`；特殊字段、constructor、
  getter shape 覆盖 `ClassNotFoundException`、
  `EnumConstantNotPresentException`、`ExceptionInInitializerError` 与
  `TypeNotPresentException`。
- 仍显式 Unimplemented 的特殊语义共 13 个 constructor：
  `AssertionError(String,Throwable)`、`AssertionError(Object)`、
  `AssertionError(boolean/char/int/long/float/double)`、
  `ReflectiveOperationException(Throwable)`、
  `ReflectiveOperationException(String,Throwable)`、
  `SecurityException(Throwable)`、`SecurityException(String,Throwable)`、
  `TypeNotPresentException(String,Throwable)`。原因是当前 Throwable 基础设施
  尚未发布可供 intrinsic 精确维护的通用 cause 与 Object-to-String 语义；这些
  方法走现有 miss/记账并明确失败，没有伪造成功。
- `tests/dexvm/interpreter_tests.cpp` 新增固定 inventory 的完整性/唯一性/direct
  superclass 测试、特殊 class shape 抽样与唯一 family TU 文件结构测试；既有
  NPE、Arithmetic、monitor/thread state、StackOverflow、OOM、UnsatisfiedLink
  行为随全量回归通过。
- 验证：`cmake --preset dev`（导入 VS 2026 原生环境并固定原生 Ninja）、
  `cmake --build --preset dev` 均成功；`ctest --preset dev` **769/769**。
  `windows-msvc` configure 与 `ogplay_tests` 构建亦成功。首次未导入原生环境的
  dev configure 命中 Cygwin Ninja `D:` 路径错误，切换项目约定的原生工具链后
  重跑通过，不计为代码失败。
