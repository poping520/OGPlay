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

## WU8-1a · JNI 数据族（2026-10-22）

局部扩充诊断用例，**未修改 src/，未修复任何 JNI 失败**。本批全部判据来自本地
Dalvik `vm/Jni.cpp`，不采用 CTS TODO 清单之外的假定或 HotSpot 行为；非平凡断言
在 C/Java 两侧就近注明行号。通用路径用普通 case-id，Dalvik 额外行为以 `.dvm_要点`
结尾。原 184 条 case-id 和 PASS/FAIL/DONE 协议保持不变。

### 组织和新增范围

Main 从 1,015 行缩为 43 行，只保留调度/输出；`CaseGroup` 约束 ids/run，
`NativeSupport` 承接公共断言与原 native 入口。各组 oracle/注册移入
BoundaryStaticCases、BoundaryInstanceCases、CallCases、LibraryCases、VersionCases，
新增 ClassObjectCases、FieldsCases、StringsCases、ArraysCases。原 void 回调副作用
随 oracle 移到 CallCases，短名、长名和 native 入口的类名同步调整，原用例零回归。

新增 native 文件 `class_object.c`、`fields.c`、`strings.c`、`arrays.c`，全部是 C11。
未修改 `jni_tests.py`：复用现有构建、分类、组失败后逐条隔离、覆盖和基线机制。

| 新增组 | 规范级 | dvm_ | 合计 | 关键判据（Jni.cpp） |
| --- | --- | --- | --- | --- |
| class_object | 7 | 3 | 10 | 1264–1279（继承/assignability）、1508–1562（分配/构造）、1022–1026（InstantiationException）、1600–1610（instance/null） |
| field | 37 | 0 | 37 | 1674–1690（实例 ID/NoSuchFieldError）、1730–1745（静态 ID）、1752–1902（各类型读写） |
| string | 15 | 6 | 21 | 2101–2152（UTF-16）、2176–2222（MUTF-8/isCopy/null）、2564–2587（Region/异常）、2623–2645（Critical） |
| array | 61 | 35 | 96 | 2271–2294（对象元素）、2326–2429（基本数组/别名/Release/Region）、2598–2617（Critical） |
| **新增** | **120** | **44** | **164** | — |

字段的 getter 由 Java 写入、native 读取；setter 由 native 写入、Java 读取，实例/静态
ID 分别查找。静态 setter 先在 Java 清成不同值，避免继承上一条 getter 留下的值。
覆盖 byte/short 负数、char 0xffff、long 0x8123456789abcdef、double 原始位
0xc123456789abcdef，以及 float NaN payload 0x7fc12345，按原始位核对而非 NaN 相等比较。
静态 Int 读写已有旧 call oracle 覆盖，本批不重复增加入口用例。

八类 ArrayElements 分开检查普通取值、Release 写回、isCopy、**Release 前 Java 可见**、
JNI_ABORT 后写入仍保留、JNI_COMMIT 后继续使用同一 lease 并最终 Release；借用期间由
Java 保持原数组强引用，finally 释放，不跨调用保存局部 jobject。Critical 获取与最终释放
之间不调用其他 JNI；数组通过写入后 JNI_ABORT、Java 读取验证别名可见性。
字符串含 NUL、两/三字节字符及 surrogate pair；Region 只核对内容与守卫，不假设 UTF
Region 的额外终止符行为。字符串只读，不通过修改 String 存储来证明指针别名。

### 双后端结果

下表“规范级/dvm_”仅统计新增 164 条；旧 184 条沿用原名称及基线，不重新归类。

| 后端 | 新增规范级 | 新增 dvm_ | 新增合计 | 原用例 | 全套 |
| --- | --- | --- | --- | --- | --- |
| switch | **92/120（76.7%）** | **8/44（18.2%）** | 100/164 | 183/184 | **283/348（81.3%）** |
| threaded | **92/120（76.7%）** | **8/44（18.2%）** | 100/164 | 183/184 | **283/348（81.3%）** |

新增各组两后端一致：class_object 7/10、field 37/37、string 11/21、array 45/96。
原 184 条分类全部保持（含 DefineClass 的既有未实现），零 pass 回退、无排除项变更。

以下数量为**每个后端**；本轮全部失败的 ledger 都为空，未实现依据输出明确点名。
`fail:crash` 均为宿主错误退出 70，未观察到信号崩溃；仍按既有驱动分类记账。

| 组 | 分类/数量 | 代表用例 | 第一处差异 | ledger |
| --- | --- | --- | --- | --- |
| class_object | fail:unimplemented / 3 | `class_object.GetSuperclass` | 期望 Sub 的父类 Value，实际 `unbound JNI guest slot: GetSuperclass`；另 root 同槽，IsAssignableFrom 自身槽未绑定 | 空；输出点名 |
| array | fail:assert / 34 | `array.GetBooleanArrayElements.dvm_isCopy` | 期望 JNI_FALSE，实际 copy 标志非 false；八种类型各 isCopy/Release 前可见/ABORT/COMMIT 共 32 条失败，PrimitiveCritical 的 isCopy/alias 另 2 条失败 | 空 |
| array | fail:crash / 17 | `array.GetBooleanArrayRegion.bounds` | 负 start 期望 ArrayIndexOutOfBoundsException，实际 `GetBooleanArrayRegion range is outside the array` 后退出 70；八种 Get/Set Region 共 16 条，对象元素越界另 1 条 | 空 |
| string | fail:assert / 1 | `string.GetStringChars.dvm_isCopy` | 期望 JNI_FALSE，实际 copy 标志非 false；UTFChars 的 JNI_TRUE 判据通过 | 空 |
| string | fail:crash / 6 | `string.GetStringRegion.negativeStart` | 期望 StringIndexOutOfBoundsException，实际 `GetStringRegion range is invalid` 后退出 70；UTF16/UTF8 各三种越界均未返回 Java 异常 | 空 |
| string | fail:unimplemented / 3 | `string.GetStringCritical` | `unbound JNI guest slot: GetStringCritical`；Release 和 isCopy 用例也先被该获取槽阻断 | 空；输出点名 |
| version（既有） | fail:unimplemented / 1 | `version.DefineClass` | 原未绑定槽不变 | 空；输出点名 |
| **合计** | **assert 35 / crash 23 / unimplemented 7** | — | 新增失败 64，既有失败 1；timeout/missing/excluded 均为 0 | — |

### 覆盖和验证

- 静态调用覆盖 **206/233（88.41%）**，比 WU8-0 增 91 个函数；未覆盖 27 个，
  固定排除仍只有 DestroyJavaVM。目标数据族函数均有字面量调用；ExceptionOccurred 是
  核对异常类型所需辅助入口。头文件仍为 NDK 权威集合，并核对 API19 函数顺序一致。
  这是源码调用覆盖；ReleaseStringCritical 等被前置失败阻断的路径不声称已执行验证。
- 直接使用现有 `build/release/ogplay-cli`，**未重建 C++，未跑 dalvik 全量或游戏场景**。
  `jni_tests.py --self-test` 通过。先全量双后端 `--coverage --check-baseline`，退出 0、
  零回归；静态 setter 补独立初值后仅字段组双后端 37/37 定向复核，再全量
  `--coverage --update-baseline`，零回归并写入新基线。Boolean GetRegion 的目标缓冲区
  与守卫改为 true、源值 false，避免无复制也能通过；补跑该条双后端 1/1 定向核对。
- 报告目录 `.local/analysis/wu8-1a/`：根目录 summary/coverage 为最终更新轮，
  `check/` 保留检查轮，`field-verification/`、`region-verification/` 保存定向验证，`breakdown.json`
  保存新增/全体及普通名/dvm_ 统计。组及隔离 case 的 log/ledger/逐条 JSON 由原驱动生成；
  外层日志为 `.local/analysis/wu8-1a-{check,field,region,update}.log`。
- `git diff --check` 和原 184 条基线分类逐项核对通过；只提交源码、文档和 JSON 基线，
  dex/.so/日志不入库。未修改能力账本的范围、状态或验证入口。

### 未测及原因

- NULL array、错误对象/数组类型、失效 ID、实例/静态 ID 混用、非零长度的 NULL 字符数据：
  非 CheckJNI 的 Dalvik 有解引用/assert/未定义风险，不猜测异常，也不发起可能 abort 的探针。
  只测试实现明确处理的 NULL UTFChars/UTFLength、NewString(NULL,0)、IsInstanceOf(NULL)。
- 不测试超大长度/溢出/资源耗尽；先锁定可控边界值，不用宿主内存预算猜 Dalvik OOM 行为。
- 数组 Region 的 bounds case 按负 start、负 len、越过尾部依次断言；当前第一步就宿主退出，
  后两步尚未到达。字符串三类边界独立成 case，均已隔离到达。对象数组测试了正常/null 元素与越界，
  未把对象数组存储类型错误扩为本批新能力。
- COMMIT 用例已覆盖保留 lease、继续写、最终释放的公开可观察行为；PrimitiveCritical 的
  COMMIT 用例通过，也**不能据此证明内部 pin 计数正确**。不通过不确定的 GC 时机、回收压力
  或地址猜测证明“未解除固定”；跨 GC 的 pin 保活性留待有确定 GC 观测入口的后续工作。
- GetStringChars 的直接性通过 isCopy 判据检查；不修改不可变 String 验证别名。Critical 字符串
  获取未绑定，其字符内容、isCopy 与 Release 的独立行为尚未到达，不能用静态覆盖代替行为证据。
- 剩余 27 个未覆盖函数属于引用、线程、反射转换、monitor、NIO 等后续主题，见本轮
  `coverage-uncovered.txt`；不提前排除，不在本单修复。
