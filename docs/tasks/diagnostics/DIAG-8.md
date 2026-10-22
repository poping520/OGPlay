# DIAG-8 · WU8-0 自有 JNI 一致性探针

状态：首轮基线已建立（2026-10-22）。本单只建设诊断工具并记录失败，不修复 JNI。
依赖：DIAG-7 的生产 run-dex、API19 ROM payload、NDK r25c、javac 1.7 字节码与 dx。

## 目标、判据和边界

建立与游戏无关、零 AOSP 测试库依赖的 JNI 判据。来源优先级：CTS 4.4 > Dalvik
`vm/Jni.cpp` > ART；引用 ART 形态前必须核实 Dalvik。参考源码位于
`.local/aosp-cts/`，下载来源、固定标签和哈希见其 `SOURCES.md`。

- CTS `android-4.4.4_r2.0.1`，commit `b8e2daba969c1d0d9eb3feec9559e12db378681b`：
  `tests/tests/jni/libjnitest/android_jni_cts_{Static,Instance}Nonce.c`、
  `macroized_tests.c` 及对应 Java 类提供测试点和期望值；自有代码按测试点改写，
  派生文件保留来源和 Apache-2.0 声明，不使用 JUnit、android.test、nativehelper 或 liblog。
- `GetVersion` 为 1.6；`DefineClass` 按 Dalvik `vm/Jni.cpp:1198` 返回 NULL 且不挂异常。
  未发现本批 CTS 判据与 Dalvik Jni.cpp 冲突；DefineClass 的 NULL 判据直接来自 Dalvik。
- ART MyClassNatives（commit `d5e4ac0`）只补充 synchronized、混合对齐、double pair、
  抛异常的 ABI 形态；持锁判据核对 Dalvik `vm/Jni.cpp:1146` 的 native 调用前加锁，
  其余使用明确的参数/返回值和 Java catch 判据，不把 ART 行为直接当 Dalvik 判据。
- 本批仅验证单应用 ClassLoader、ARMv7 softfp、API19 进程；不新增 Android 系统服务。
  覆盖率是源码调用覆盖，不表示全部参数、异常路径或行为已经验证。

## 接口与目录约定

- `run-dex --native-lib-dir <dir>` 将直接目录内的 `lib*.so` 作为 armeabi-v7a 应用库，
  经同一个 NativeLibraryLoader 和 application ClassLoader token 加载。
  不存在目录、非法 `.so` 文件名、坏 ELF 或重复 DT_SONAME 明确失败；其他文件忽略。
  默认仍是空应用库 inventory，原 UnsatisfiedLinkError 行为保持。
- `tests/conformance/jni/java/probe/`：独立 Main runner、static/instance boundary、
  Parent/Child/StaticTarget 回调 oracle。Main 支持 `--list`、`--group g`、`--case id`，
  输出 PASS/FAIL 和 DONE；列举不触发 probe 库加载。
- `tests/conformance/jni/native/`：C11，按 boundary/call/version/library 分主题。
  回调错误通过 jstring 返回 Java，pending callback exception 先清除再报告失败；
  native 抛异常用例仍由 Java 捕获。函数名均为字面量，没有 token-pasting。
- `tools/conformance/jni_tests.py`：用 NDK clang 编译、ld.lld 链接 ROM libc 与静态 builtins；
  `--no-undefined`，readelf 检查两库 ELF32/ARM/DYN/SONAME 和 DT_NEEDED。
  主库仅依赖 libc，1.4 库没有共享依赖；不依赖 AOSP 测试库。
- Java/dex/.so/日志/覆盖清单仅写 `--out`，默认 `.local/analysis/wu8-0/`，不入库。
  `cmake/Tests.cmake` 显式列举宿主测试源码，新增 ARM C 文件未进入宿主目标。

## 调度、分类与基线

每组一个进程，switch/threaded 双后端；组崩溃或超时后对该组所有 case 单独重跑。
`summary.json` 保存逐后端、逐 case 结果和分类汇总，逐 case JSON 保存 PASS/FAIL 原行、
原因和 ledger；原始进程日志与 ledger 旁置。进程级未实现输出优先识别明确缺口，
因此内部退出 70 的 unbound JNI 不会被误归为无原因 crash。

分类：pass、fail:assert、fail:unimplemented、fail:crash、fail:timeout、fail:missing、
excluded:<原因>。用例排除字典当前为空；JNI 函数排除仅 DestroyJavaVM（宿主管理退出，
不向 guest 提供销毁 VM 的生命周期），不因失败新增排除。

`tools/conformance/jni_baseline.json` 为“后端 → case-id → 分类”。`--check-baseline`
拒绝任何 pass 回退，包括结果消失；`--update-baseline` 只在零回归时合并选中范围，
保留未运行后端和用例。不修改 dalvik 驱动或 dalvik_baseline。

```sh
python3 tools/conformance/jni_tests.py --self-test
python3 tools/conformance/jni_tests.py --coverage --check-baseline
# 首次或无回归的提升：--update-baseline
# 定向：--backend switch --group call；或 --case call.CallStaticIntMethodV
```

## 首轮结果

| 后端 | boundary_static | boundary_instance | call | library | version | 合计 |
| --- | --- | --- | --- | --- | --- | --- |
| switch | 34/34 | 34/34 | 108/108 | 6/6 | 1/2 | **183/184（99.5%）** |
| threaded | 34/34 | 34/34 | 108/108 | 6/6 | 1/2 | **183/184（99.5%）** |

call 覆盖三族 × 十种返回 × 普通/A/V 的 90 个函数，另 18 条参数压力用例。
nonvirtual 使用覆盖父方法的 Child 对象，要求调用 Parent 的返回值/副作用；void 每次
先归零副作用，避免前一条结果掩盖漏调用。library 验证 OnLoad 返回 1.4/1.6、计数一次、
重复 loadLibrary、缺库和导出短名/重载长名。

| 组 / 分类 | switch | threaded | 代表用例、第一处差异 | ledger |
| --- | --- | --- | --- | --- |
| version / fail:unimplemented | 1 | 1 | `version.DefineClass` 期望 NULL 且无异常；实际 `unbound JNI guest slot: DefineClass`，进程退出 70，隔离后同样复现 | 空；缺口由输出点名 |
| 全部 / fail:assert | 0 | 0 | 无 | — |
| 全部 / fail:crash | 0 | 0 | 无 | — |
| 全部 / fail:timeout | 0 | 0 | 无 | — |
| 全部 / fail:missing | 0 | 0 | 无 | — |
| 全部 / excluded | 0 | 0 | 无 | — |

覆盖率 **115/233（49.36%）**：NDK 两 ABI struct 共 229+5 个函数（reserved 不计），
排除 DestroyJavaVM 1 个，剩余未覆盖 118 个。包含排除项的原始分母为 234。
NDK jni.h 与本地 API19 libnativehelper/jni.h 的函数集合及顺序完全一致。
`coverage.json` 和 `coverage-{covered,uncovered,excluded}.txt` 保存机器清单。

## 验证证据

- 构建 ogplay-cli、ogplay_tests；run-dex 主题实际属于 ogplay_app_tests，因此额外构建
  该目标以执行更新后的参数/错误路径测试。仅运行 `ogplay_app_tests -sf='*run_dex_tests.cpp'`：
  **7/7，35 断言通过**（`wu8-0-tests.log`）。涵盖默认拒绝应用库、新参数、缺目录、
  非法文件名、重复 ELF SONAME。正向加载由本批两个 ARM 库的双后端运行证明。
- JNI self-test 覆盖七类结果、明确 unbound 缺口、重复结果、固定排除、pass 回退拒写、
  子集保留与覆盖扫描（注释/字符串不计，拒绝 `##`）；dalvik self-test 保持通过。
- 最终双后端全量 `--coverage --update-baseline` 零回归，基线已写入；运行报告和
  原始/隔离日志在 `.local/analysis/wu8-0/`，外层构建、测试和自测日志为
  `.local/analysis/wu8-0-*.log`。未运行 dalvik 全量或游戏场景；默认路径没有行为变化。
- `python3 tools/capabilities.py check`、`git diff --check` 通过。

## WU8-1 交接

以本批基线为起点，按 CTS `macroized_tests.c:1518` 的未覆盖函数清单与本工具
`coverage-uncovered.txt` 逐项核实 Dalvik 判据，再扩充引用、字段、数组、字符串、
异常、monitor 与 JavaVM 线程族；不提前排除未知项。DefineClass 保留未实现分类，
修复应另立运行时任务。源码覆盖统计不要求达到 100%，不得用假调用或排除提高通过率。
