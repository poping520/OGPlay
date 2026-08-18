# APS-4 · Dynamic NativeLibraryLoader 与 JNI_OnLoad

## 目标（一句话）

在已运行的 guest process 上实现可追加 ELF module 的 process 级
`NativeLibraryLoader`，并把 explicit native load 的 constructors/JNI_OnLoad 语义从
frontend/root-module 模型迁入 loader。

## 依赖

- APS-2、APS-3
- `docs/design/apk-startup/05-native-loading.md`
- `docs/design/apk-startup/07-reentrancy-and-failure.md`

## 设计锚点

- 05 §4–9
- 07 §2–4

## 语义出处

编码前必须记录：

- `.local/asop/dalvik/vm/Native.cpp` 中 native load table、ClassLoader、JNI_OnLoad 路径；
- 若调用链分散，补实际文件 + 函数。

重点确认“同 loader 重复 load / 不同 loader / dependency 自带 JNI_OnLoad”的真实行为。

## 变更

- ELF namespace 支持 append/load dependency closure；
- resolver 可从 selected APK ABI inventory 取应用库；
- loaded registry：Loading/Loaded/Failed + classloader token；
- explicit requested library 执行 JNI_OnLoad 并校验版本；
- frontend root-only `InitializeJniLibrary()` 责任下沉；
- 本 WU 提供直接 C++ 调用 loader 的测试，不接 DexVM `System.load*`。

## 验收（机器可判定）

- process 创建后动态 load A 成功；
- A→DT_NEEDED B 的映射/constructor 顺序正确；
- JNI_OnLoad 次数与本地 Dalvik 语义一致；
- 同库重复 load 幂等；
- malformed/unresolved/invalid JNI version 负例明确失败；
- AOSP 对照结论写回“结果”段；
- full CTest 无回归。
