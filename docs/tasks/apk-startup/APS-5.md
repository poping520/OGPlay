# APS-5 · DexVM System.load* 与 Java/native 重入

## 目标（一句话）

把 DexVM 的 `System.loadLibrary` 与 `System.load` handler 接到 process
`NativeLibraryLoader`，并用真实 nested Java→native→Java→native fixture 证明重入安全。

## 依赖

- APS-4
- `docs/design/apk-startup/05-native-loading.md`
- `docs/design/apk-startup/07-reentrancy-and-failure.md`

## 设计锚点

- 05 §1–3、§7、§9
- 07 §1–3、§5、§8
- 08 §5–6

## 语义出处

编码前核对：

- `.local/asop/libcore/luni/src/main/java/java/lang/System.java`
- `.local/asop/libcore/luni/src/main/java/java/lang/Runtime.java`
- `.local/asop/dalvik/vm/Native.cpp`

记录实际方法/函数、caller ClassLoader 与错误映射结论。

## 变更

- 替换当前 loadLibrary no-op/preloaded 假设；
- 增/补 `System.load(path)` intrinsic；
- 传递 application ClassLoader token；
- Java load failure 映射为正确异常族；
- 审计 DexVM execution lock、JNI invocation engine、loader mutex；
- 建立 A JNI_OnLoad → Java callback → load B 的 nested fixture。

## 验收（机器可判定）

- loadLibrary 正例与 missing library 负例；
- load(path) 正例与 guest path 负例；
- nested A→Java→B fixture 不 deadlock/timeout；
- A/B JNI_OnLoad 各一次；
- pending Java exception 符合断言；
- `System.loadLibrary` 不再只记录日志后成功返回；
- full CTest 无回归。

## 语义核对结论

- `libcore/luni/src/main/java/java/lang/System.java` 的 `load(String)` 与
  `loadLibrary(String)` 都把 `VMStack.getCallingClassLoader()` 传给 `Runtime`。
- `Runtime.load/loadLibrary/doLoad` 对 null 参数抛 `NullPointerException`；
  `nativeLoad` 返回非 null 错误字符串时抛 `UnsatisfiedLinkError`。有 ClassLoader 时由
  `findLibrary` 解析路径；OGPlay 的单 application loader 用 selected-ABI inventory 完成
  同等解析，不引入 host 搜索路径。
- Dalvik `Native.cpp::dvmLoadNativeCode` 以完整 path + ClassLoader 关联 library，同 loader
  重复加载成功、跨 loader 失败；`native/java_lang_Runtime.cpp::nativeLoad` 把该函数的失败
  detail 返回 Java 层。APS-4/5 的 registry 与异常映射保持该边界。

## 结果（机器可判定，已达成）

- `java.lang.System` core catalog 新增 `load(String)`；`load/loadLibrary` handler 通过
  `DexVmAndroidContext` 接到同一个 process `NativeLibraryLoader`，携带稳定非零
  application ClassLoader token。null 映射 NPE，其余 typed loader 失败映射 ULE；旧
  preloaded/log-only 成功路径已删除。
- `System.load(path)` 保留 synthetic APK native mapping，并在其外对规范化 absolute
  guest path 通过共享 VFS 读取 library image；VFS image 进入同一 loaded registry、
  dynamic namespace、constructor 与 explicit `JNI_OnLoad` 管线，host-style path 继续
  明确拒绝。
- `run-apk` 为匹配 APK 建立 selected-ABI inventory/loader，不再在生命周期前无条件调用
  legacy root `InitializeJniLibrary`；JNI_OnLoad 由实际 `System.load*` 请求拥有。
- 锁审计确认 loader registry mutex 在 constructor/JNI_OnLoad 前释放，JNI invocation
  engine 直接回调 `DexVmGuestBridge::InvokeInterpreted`，`VmExecutionLock` 支持同线程
  owner/depth 递归。夹具进一步发现并修复 native guest-call root CPU 现场覆盖：nested
  调用现使用独立 CPU 与外层 SP 下方的栈空间。
- `aps5.dexasm` + ARM ELF A/B 夹具实际执行
  Java `start` → load A → A `JNI_OnLoad` → JNI `CallStaticVoidMethod` → 解释执行 Java
  `callback` → load B；A/B `JNI_OnLoad` 各一次，logical/synthetic-path/VFS-path 正例与
  missing logical/bad guest path 的 pending Java `UnsatisfiedLinkError` 均受检。
- Windows `cmake --preset windows-msvc`、全量 build 成功；
  `ctest --preset windows-msvc` 为 793/793。
