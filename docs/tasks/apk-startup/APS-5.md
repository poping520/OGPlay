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
