# APS-10 · native dlopen 按需装载共享进程库

## 目标

native dlopen 与 Java load 共用库来源、ELF namespace 和构造器状态，保持 JNI 初始化职责独立。

## 依赖与设计锚点

- APS-4/5/9、[ADR-0135](../../adr/runtime.md#adr-0135)。
- 保持 ADR-0076 的 soinfo 视图、符号查询与进程生命周期卸载边界。

## 语义出处

- `.local/aosp/bionic/linker/linker.cpp`：`find_library_internal/do_dlopen` 按需装载；
  `CallConstructors` 在递归前登记状态，依赖先于本库，DT_INIT 先于 INIT_ARRAY。
- `.local/aosp/bionic/linker/linker.cpp`：`open_library` 保留首次请求路径；已装载身份兼容 API19 basename。
- `.local/aosp/dalvik/vm/Native.cpp`：`dvmLoadNativeCode` 单独管理 ClassLoader 与 JNI_OnLoad。

## 变更与边界

- 提取共享、拥有型来源快照及依赖闭包，进程通过显式接口持有 native 装载入口。
- 库来源为所选 ABI 的 APK、受审系统库与 guest VFS；禁止宿主文件/库装载。
- 进程装载门禁可同线程重入；构造器递归保护、跨线程串行；元数据短锁不跨 guest 执行。
- native dlopen 不执行 JNI_OnLoad、不登记 ClassLoader；失败保留 dlerror，执行控制故障继续传播。
- 不扩展 dlsym 查找范围、RTLD_NEXT 或实际 dlclose 卸载。

## 验收

- 定向 ARM fixture：首次 native 装载、依赖构造顺序、重复打开与引用、构造器重入、跨线程等待。
- native 装载不执行 JNI_OnLoad；后续 Java load 复用映射并执行一次，ClassLoader 冲突仍拒绝。
- 路径/所选 ABI、缺依赖、坏 ELF、构造失败与原始控制错误均有机器判据。
- Windows Release 受影响目标构建；TML 原命令复跑，记录原错消失和下一首错。
- 能力账本及文档静态检查；不运行全量测试。

## 结果（机器可判定，已达成）

- Windows `cmake --build --preset windows-msvc --config Release --target ogplay ogplay_tests`
  及随后仅测试载荷的增量构建均退出 0；产物为 `build/windows-msvc/Release/ogplay.exe`
  与 `ogplay_tests.exe`。构建日志：`.local/aps10-build.log`、`aps10-tests-build.log`、
  `aps10-libdl-build.log`。
- `ogplay_tests.exe --test-case="APS-10*,native library loader*,DexVM System load APIs*" --no-colors`
  退出 0：10 cases / 130 assertions；覆盖真实 ARM 构造顺序、依赖反开根库、重复引用、
  native 不调用 JNI_OnLoad、随后 Java 初始化一次、ClassLoader 冲突、VFS 路径、系统源、
  来源 owner 生命周期、ABI 隔离、链接失败、构造退出/CPU fault 及跨线程等待。
- `ogplay_tests.exe --test-case="libdl*,BND-40*,Bionic libdl*,guest lifecycle follows*" --no-colors`
  退出 0：7 cases / 168 assertions；原边界 fake hook 改为 typed LinkError，并断言执行错误
  原样抛出、不产生 dlerror。日志：`.local/aps10-tests.log`、`.local/aps10-libdl-tests.log`。
- TML 原命令（无 survey）退出 1，第 0 帧已越过 libmono 装载；下一首错为
  `Unable to load library: /data/app-lib/libunity.so [strong ELF symbol is unresolved: ASensorManager_getInstance]`。
  日志 `.local/aps10-tml-run.log`；机器断言原 libmono 首错消失、新首错存在。
- 能力账本 check、`architecture.documentation_layout`、UTF-8/本地链接及 `git diff --check`
  通过。未跑全量测试、macOS/Linux 或正式 title gate；未实现下一项传感器能力。
