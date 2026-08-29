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

- `.local/aosp/dalvik/vm/Native.cpp` 中 native load table、ClassLoader、JNI_OnLoad 路径；
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

## 结果（机器可判定，已达成）

- AOSP 对照：`.local/aosp/dalvik/vm/Native.cpp` 的 `SharedLib`、
  `checkOnLoadResult()`、`dvmLoadNativeCode()` 与 `dvmCreateSystemLibraryName()` 证明
  Dalvik 以路径作为全局 identity；同 ClassLoader 重复 load 幂等，同线程递归 loading
  返回成功，另一线程等待唯一结果，同一路径跨 ClassLoader 明确失败。`dlopen()` 完成依赖
  与 constructors 后，Dalvik 只对显式请求 handle 查找/调用 `JNI_OnLoad`；无 OnLoad 成功，
  JNI_ERR/坏版本永久标记失败。`.local/aosp/dalvik/vm/Jni.cpp` 的 `FindClass()` 证明
  JNI_OnLoad 期间用 `classLoaderOverride` 解析类。
- `ExtendElf32ModuleNamespace` 在既有 namespace 上解析、追加、映射并按动态 root scope
  重定位新 guest modules；Bionic extender 可按新依赖追加 HLE boundary。映射/重定位失败
  恢复地址空间且不发布 namespace，既有 module index 保持稳定。
- `AndroidGuestProcess` 以确定性空闲 load bias 追加 application closure，逐 module 执行
  依赖优先 constructors，成功初始化的 module 纳入 process stop 反序 finalization；
  constructor/JNI 失败后已发布映射保留为 process-lifetime 失败事实，不伪造卸载。
- `NativeLibraryLoader` 同时支持 logical name 与稳定 `/data/app-lib/<soname>` synthetic
  guest path，且只从 selected ABI inventory 解析；装载时验证 ET_DYN，但不把 APK entry
  basename 与 ELF `DT_SONAME` 不同视为错误：前者保持规范 inventory identity，后者记录为
  同一模块别名，`DT_NEEDED` 可用任一身份解析；
  registry 保存 Loading/Loaded/Failed、ClassLoader token、handle、JNI version/call count。
  registry mutex 不跨 constructors/JNI_OnLoad guest call，同线程递归不自锁。
- 直接 C++ fixture 覆盖 A→B dependency constructor 顺序、仅 A 的显式 JNI_OnLoad、B
  后续显式 load、无 OnLoad 成功、重复幂等、跨 ClassLoader、malformed/unresolved、
  SONAME/inventory 双身份、invalid JNI version 与稳定 Failed 重试；未接 DexVM
  `System.load*`。
- 后续 API19 纠偏：Dalvik `dvmLoadNativeCode()` 将显式路径交给 `dlopen()`，平台 linker
  不执行“文件 basename 必须等于 `DT_SONAME`”门禁。定向 fixture 锁定 mismatch 显式加载、
  `DT_NEEDED` 分别使用 ELF SONAME 与 inventory basename；Tales release exact run 越过原
  拒载。后续 WU-0231 在标准扩展目录补齐并真实绑定 `GL_OES_mapbuffer` 三项入口后，
  `libAmazonGamesJni.so` 已完成显式加载，新首错为独立 JNI
  `getPackageCodePath()Ljava/lang/String;` 声明缺口。
- Windows MSVC：`cmake --preset windows-msvc`、完整 Release build 通过；APS-4 定向
  2/2、完整 `ctest --preset windows-msvc` 792/792。
