# 05 · Native library 动态加载

## 1. 目标 API

process 级服务概念接口：

```cpp
class NativeLibraryLoader {
public:
    Result<LoadedLibraryHandle> LoadLibrary(
        std::string_view logical_name,
        JavaClassLoaderId class_loader);

    Result<LoadedLibraryHandle> LoadPath(
        std::string_view guest_path,
        JavaClassLoaderId class_loader);
};
```

实际类型名可调整，但必须同时支持 logical-name 与 path 两条入口，并共享同一 loaded
registry、ELF namespace 与 JNI initialization 逻辑。

## 2. `System.loadLibrary(name)`

处理流程：

```text
Java System.loadLibrary("foo")
  ↓
Runtime/loadLibrary intrinsic
  ↓
resolve caller application ClassLoader token
  ↓
map logical name → libfoo.so
  ↓
resolve libfoo.so in selected APK ABI inventory / allowed system search roots
  ↓
NativeLibraryLoader explicit load
```

约束：

- 不扫描 DEX 字符串提前装库；
- 不按 Profile `so_sha256` 选 root；
- 名称解析失败必须回到 Java 可观察失败路径；
- 应保留 caller ClassLoader identity，即便当前只有单一 app ClassLoader。

## 3. `System.load(path)`

`path` 是 guest-visible path。loader 先通过 VFS/已注册 APK native mapping 解析，
再取得 library bytes/image。

为未真正“安装”到 Android filesystem 的 APK，OGPlay 可以为 APK native entry 建立
稳定 guest synthetic path；这是 OGPlay 的兼容层实现选择，不应伪装成 AOSP 安装器
行为。

禁止把任意 host path 直接暴露给 guest。

## 4. APK native inventory

APK 读取阶段建立：

```text
abi -> soname/path -> ApkEntry/ImageSource
```

例如：

```text
armeabi/libfoo.so
armeabi/libbar.so
armeabi-v7a/libfoo.so
```

process ABI 一旦确定，loader 只看该 ABI 的应用库集合。

## 5. ELF dependency 与 explicit load 分离

加载 `libfoo.so` 时，linker 可以因 `DT_NEEDED` 自动加载 `libbar.so`。但两者语义不同：

- `libfoo.so`：Java explicit load 的目标；
- `libbar.so`：ELF linker dependency。

**不得默认对每个 `DT_NEEDED` library 调用 `JNI_OnLoad`。**
`JNI_OnLoad` 的精确调用条件必须在实现 WU 前对照本地 KitKat Dalvik 源码确认，并用
测试固定。当前设计预期：VM 的 explicit native load 对请求库执行 JNI load
初始化；依赖由 linker 负责 ELF 初始化。

## 6. constructors 与 `JNI_OnLoad`

顺序应由 linker/VM 语义明确分层：

```text
map requested module + missing DT_NEEDED closure
  ↓
relocate / publish symbols
  ↓
run ELF initialization plan (dependency order)
  ↓
for explicit requested library: locate JNI_OnLoad
  ↓
call JNI_OnLoad(JavaVM*, ...)
  ↓
validate returned JNI version
  ↓
mark explicit load successful
```

现有 `AndroidGuestCallSession::InitializeJniLibrary()` 的“root-only”责任应迁入这里；
frontend 不再调用。

## 7. loaded registry 与幂等

至少维护状态：

```text
canonical library identity
class loader token
state = Loading | Loaded | Failed
module handle
JNI_OnLoad result/version
```

同一 ClassLoader 对同一库重复 `System.load*`：

- 已 Loaded：成功返回，不重复 constructors/JNI_OnLoad；
- 正在 Loading：按 §07 的同线程重入规则处理，禁止自锁；
- Failed：返回稳定失败，不静默重试出不同状态。

不同 ClassLoader 的行为当前不会成为常规路径，但数据模型不得抹掉该维度；KitKat
Dalvik 对跨 ClassLoader 重复 load 的语义在 APS-4 中对照本地源码固定。

## 8. 动态 namespace 能力

现有启动型 ELF loader 若只能“一次性给出 root + module set”，必须扩成 process
lifetime namespace：

- 已装入 guest system libraries 保持；
- boundary/HLE provider 保持；
- 后续 application explicit load 可追加 module；
- 新模块符号解析能看到既有 global namespace；
- 新模块的 `DT_NEEDED` 可来自 APK、guest system libs 或 boundary provider；
- module ownership 可在 process stop 时按正确顺序 finalize。

不允许为了省改动，在 `Prepare()` 阶段重新“预加载 APK 里所有 `.so`”。那会绕过
本设计核心语义。

## 9. 错误映射

至少区分：

- library not found；
- ABI incompatible；
- malformed ELF；
- unresolved symbol / dependency；
- constructor guest fault；
- missing/invalid `JNI_OnLoad` result；
- cross-loader conflict；
- recursive load cycle。

Java 层应看到与 `System.load*` 语义相符的失败（通常落到 link error 家族）；底层同时
输出结构化 runtime loader error，保留 requested name/path、ABI、resolved APK entry、
dependency chain。
