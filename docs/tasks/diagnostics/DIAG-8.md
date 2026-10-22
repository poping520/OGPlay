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

## WU8-1b · JNI 语义族与覆盖收口（2026-10-22）

**不修改 src/，不修复失败，不重建 C++。** 新增 references.c、exceptions.c、reflection.c、
monitor_nio.c、registration.c、javavm_threads.c，以及对应 Java 分组。补齐 WU8-1a 剩余
27 个函数的实际探针调用；ExceptionOccurred 从既有辅助入口扩为身份/pending/局部引用判据。
所有新断言注明 Dalvik `vm/Jni.cpp` 行号，委托实现另列文件/行号，不采用 HotSpot 作为 oracle。

### 判据核对与实现边界

- 引用（Jni.cpp:409–412、1418–1491、2651–2685）：local/global/weak 身份和释放，
  weak 的 Java 强可达条件，PopLocalFrame 将 result 带回外层，容量 16 成功、513 按
  Thread.h:74 的 512 上限失败并抛 OOME。只请求容量，不实际填满表触发溢出 abort。
  GetObjectRefType 独立检查 local/global/weak，避免未绑定辅助槽遮住其他引用探针。
- 异常（1333–1387）：Throw 保持原 throwable，ExceptionOccurred 连续两次仍返回同一
  pending 对象；局部引用类型独立成条。ExceptionDescribe 委托 Exception.cpp:564–592，
  打印时临时清除、结束**恢复原 pending**。除身份断言外，驱动要求日志包含唯一
  `describe-marker`；该输出探针单独运行，其他 case 的同名异常不能替它满足打印条件。
- 反射（1285–1327）：Method/Constructor/Field 双向转换；实例/静态方法调用、构造结果、
  字段读写及反射 Modifier.isStatic 均有 Java 判据，不混用静态与实例 ID。
- monitor（2525–2549）：持锁时回调 Thread.holdsLock、可重入及逐层退出；非 owner
  期望 JNI_ERR 与 IllegalMonitorStateException。即使持锁断言失败，也执行配对退出。
- NIO（2691–2759）：native 静态存储有进程生命周期；Java 写入/native 读取及反向写入
  可见，direct 地址/容量与原存储一致，heap 地址为 NULL。
  **源码纠正：非 CheckJNI Dalvik 的 GetDirectBufferCapacity 对 heap buffer 返回实际
  Buffer.capacity，未返回函数注释和任务描述中的 -1。** 普通用例验证 direct 容量，
  `.dvm_heapCapacity` 按执行代码 2758–2759 对长度 7 的 heap buffer 断言 7。
  OGPlay 返回 -1 是与该历史 Dalvik 行为的分歧，不据此宣称 JNI 标准行为错误。
- 注销（2507–2516 → Native.cpp:516–544）：恢复 dvmResolveNativeMethod；有导出名时
  从动态实现 29 重新解析为导出实现 17，纯动态方法无导出时抛 UnsatisfiedLinkError。
  在独立 Target 类上注销，不卸掉 runner 自身的 native 控制入口；两种均通过。
- JavaVM/pthread（2555–2558、2780–2925）：GetJavaVM 与 JNI_OnLoad 保存的指针一致；
  未附着 GetEnv=EDETACHED/*env=NULL；NULL attach 的名字由 Java 回调核对为 Thread-<id>，
  具名 args 为 `{1.6,"OGPlayJNIProbe",NULL}`；重复 attach 保持 JNIEnv 和 daemon 身份，
  daemon、detach 及 detach 后 GetEnv 均验证。所有创建成功的 worker 都 pthread_join，
  已附着 worker 在返回前 detach；若错误地自动附着，也尝试清理再报告差异。
- 附着线程 FindClass（1225–1247）：按 NativeStart/system loader 查找 -cp 应用类并回调
  appMarker 验证。**run-dex 等同 dalvikvm -cp；APK 进程的 system loader 不等于应用
  loader，本条不代表 APK native 线程的应用类查找已经支持。**

### 预期死亡、输出检查和覆盖门禁

`jni_tests.py` 增加固定 `EXPECTED_DEATH`，所有死亡 case 独立进程：

| case-id | Dalvik 判据 | stderr 签名 | 本轮结果（两后端一致） |
| --- | --- | --- | --- |
| `exception.FatalError` | Jni.cpp:1406–1409 输出传入消息后 ReportJniError | `OGPLAY_JNI_FATAL_WU8_1B` | pass；exit 70、stderr 含消息、无 PASS |
| `reference.GetObjectRefType.dvm_popped` | 2683–2685 → 663–678 → 324–329，失效 local 查询触发 ReportJniError | `use of deleted local reference` | fail:unimplemented，先命中 GetObjectRefType 未绑定 |
| `javavm.DetachCurrentThread.dvm_javaThread` | 2880–2891 → Thread.cpp:2013–2029，Java 栈未退出则 abort | `detaching thread with interp frames` | fail:assert；exit 70 但实际是 detach 后 `JNI exception thread is not attached` 清理错误，缺预期签名 |

死亡判定必须同时满足非零退出、**stderr** 包含固定签名、无 PASS；超时不算成功，
stdout 独有消息也不算成功。不能用“任意 crash”替代预期死亡。失效引用只传给类型查询，
不在 C 中解引用其 handle；因 Dalvik 查询自身会 abort，使用独立进程观察该结果。

Main 保持既有 list/group/case 和 PASS/FAIL/DONE 协议，新增内部 `--cases <逗号列表>`
供驱动从普通组进程剔除死亡/输出探针；这两类始终独立运行。原始 stdout/stderr 各存
`.stdout.log` / `.stderr.log`，合并 `.log` 保持诊断阅读入口；普通分类语义保持不变。

`--coverage --require-full` 在未覆盖列表非空时退出 1。self-test 验证缺覆盖拒绝、普通/隔离
分区、死亡消息/通道/退出码/无 PASS/超时规则、Describe 输出要求，并保留基线回退拒写、
所有分类和禁止 token-pasting 的检查。

### 通过数与失败表

本批新增 **51 条：规范级 43、dvm_ 5、dvm_pending 3**。dvm_pending 单列，不与其他
Dalvik 行为重复计数；旧 case-id 不重命名，下表全体普通名统计沿用既有归类。

| 后端 | 新增规范级 | 新增 dvm_ | 新增 dvm_pending | 新增合计 | 全套 |
| --- | --- | --- | --- | --- | --- |
| switch | **38/43（88.4%）** | **0/5** | **0/3** | 38/51 | **321/399（80.5%）** |
| threaded | **38/43（88.4%）** | **0/5** | **0/3** | 38/51 | **321/399（80.5%）** |

全套按名字分类两后端各：规范级 **313/347（90.2%）**、其他 dvm_ **8/49（16.3%）**、
dvm_pending **0/3**。已有 348 条的分类全部保持，零回归；反射 10/10、注册 2/2，
引用 9/13、异常 3/8、monitor 3/4、NIO 4/5、JavaVM 7/9。FatalError 的预期死亡计 pass。

以下为每后端新增失败，ledger 均为空；未实现由输出明确点名，非零宿主错误按既有分类记录。

| 组 | 分类/数量 | 代表用例 | 第一处差异 / 证据 |
| --- | --- | --- | --- |
| reference | fail:unimplemented / 2 | `reference.GetObjectRefType` | `unbound JNI guest slot: GetObjectRefType`；popped local 死亡用例也先被此槽阻断 |
| reference | fail:crash / 2 | `reference.EnsureLocalCapacity.dvm_limit` | 期望 JNI_ERR+OutOfMemoryError，实际 `JNI local reference capacity exceeded` 宿主退出 70；PushLocalFrame 同为宿主容量错误 |
| exception | fail:unimplemented / 1 | `exception.ExceptionOccurred.localRefType` | 身份/pending 子项通过，类型观察被 GetObjectRefType 未绑定阻断 |
| exception | fail:assert / 1 | `exception.ExceptionDescribe` | pending 身份恢复通过；独立进程未打印 describe-marker，驱动将 Java PASS 改判为缺诊断输出 |
| exception | fail:crash / 3 | `exception.GetMethodID.dvm_pending` | 严格门禁拒绝 pending 下的调用；GetMethodID/CallVoidMethod 首错 GetObjectClass，NewStringUTF 首错 NewLocalRef；**契约分歧，待决策** |
| monitor | fail:crash / 1 | `monitor.MonitorExit.nonOwner` | 期望 JNI_ERR+IllegalMonitorStateException，实际 `JNI monitor exit by non-owner` 宿主退出 70 |
| nio | fail:assert / 1 | `nio.GetDirectBufferCapacity.dvm_heapCapacity` | Dalvik 执行代码期望 7，实际 -1；历史 Dalvik 与 JNI 标准处理分歧，待兼容策略决定 |
| javavm | fail:unimplemented / 1 | `javavm.AttachCurrentThread.named` | `non-null JavaVM attach arguments are not implemented`，未走到具名线程回调 |
| javavm | fail:assert / 1 | `javavm.DetachCurrentThread.dvm_javaThread` | 非零退出但不是 Dalvik 的 Java 栈保护签名；实际 detach 后 JNI exception thread is not attached |
| **新增合计** | **assert 3 / crash 6 / unimplemented 4** | — | 13 失败；timeout/missing/excluded 均为 0 |

全套每后端：pass 321、fail:assert 38、fail:crash 29、fail:unimplemented 11，其他分类为 0。
三条 pending 用例保留机器分类供基线比较，但不作为普通兼容缺陷排期；先决定是否维持严格
门禁、引入可解释的兼容策略或明确不支持，不在本单改变契约。

### 100% 证据、排除项与验证

- NDK 权威头文件共 229 JNIEnv + 5 JavaVM 函数；**调用覆盖 233/233（100%），未覆盖 0**。
  覆盖被前置失败阻断的源码调用不等于该行为已验证，例如 GetObjectRefType 相关链路；
  不用静态覆盖宣称 100% 语义通过。函数名均为字面量，没有空调用或伪断言。
- **仅排除 DestroyJavaVM**：run-dex 的宿主管理进程/VM 生命周期，guest 销毁当前 VM 不在
  该诊断运行器的生命周期范围。无新增函数排除，无 case 排除。
- 使用现有 build/release/ogplay-cli；15 秒每组/隔离进程预算，两后端一致。
  self-test 通过；全量 `--coverage --require-full --check-baseline` 零回归；识别并拆分
  类型查询的辅助依赖后，新增独立 localRefType case，相关四组定向检查通过基线约束，
  再全量 `--coverage --require-full --update-baseline`，399 条零回归并写入基线。
  最后对死亡/输出隔离调度作 self-test 与 exception 组双后端定向复核，不扩大其他测试矩阵。
- `.local/analysis/wu8-1b/` 根目录为最终 399 条 summary/coverage/逐条日志；`check/` 保留
  初始 398 条检查轮，`semantic-verification/` 为依赖拆分后的定向证据，
  `scheduler-verification/` 为最终死亡/输出调度复核；`breakdown.json` 和 `panorama.json`
  保存分类统计与互斥失败主题。外层日志为 `.local/analysis/wu8-1b-{check,semantic,scheduler,update}.log`。
- capabilities 检查、差异静态检查通过；未跑 dalvik 全量或游戏场景，未修改 src/。

## WU8 失败全景（WU8-2 排期依据）

以下以**每个后端的 78 个失败 case**为分母，按第一处差异互斥归属，不能把一个未绑定
辅助槽同时算作引用/异常多个缺陷。两个后端分类和代表差异相同；疑似模块是定位线索，
不是已经确认的修复方案。优先级先看稳定且通用的判据，再看历史语义/契约选择。

| 主题（建议顺序） | 失败用例数 | 代表用例 | 疑似根因模块 / 后续边界 |
| --- | --- | --- | --- |
| 数组与字符串指针语义 | **35** | `array.GetIntArrayElements.dvm_alias`、`string.GetStringChars.dvm_isCopy` | runtime/jni_guest 的数组/字符串 guest-memory lease 与 runtime/jni 存储采用复制缓冲区；核对直指针、isCopy、ABORT/COMMIT 和 GC 固定，不靠取消断言提高通过率 |
| 异常输出与 Java 异常映射 | **25** | `array.GetBooleanArrayRegion.bounds`、`monitor.MonitorExit.nonOwner`、`exception.ExceptionDescribe` | runtime/jni_guest 的边界/monitor 宿主错误转 pending Java throwable；runtime/jni 的 ExceptionDescribe 日志/guest stack 输出链路。23 个数组/字符串越界 + 1 个 monitor + 1 个 Describe；不是 25 个独立任务 |
| 未绑定槽 | **10** | `class_object.GetSuperclass`、`reference.GetObjectRefType` | runtime/jni_guest 的 class/reference/string-critical slot 绑定与底层语义；DefineClass 1、GetSuperclass 2、IsAssignableFrom 1、StringCritical 3、GetObjectRefType 链路 3。先补共享槽，再重跑被它阻断的独立行为 |
| 契约分歧，待决策 | **4** | `exception.CallVoidMethod.dvm_pending`、`nio.GetDirectBufferCapacity.dvm_heapCapacity` | pending 三条为 OGPlay 有意严格门禁；NIO 一条为 Dalvik 非 CheckJNI 的容量历史行为。对应 runtime/jni pending gate、runtime/jni_guest NIO；先定兼容边界，不自动当普通缺陷修 |
| 引用容量与局部帧 | **2** | `reference.PushLocalFrame.dvm_limit` | runtime/jni reference table 与 guest 绑定的容量错误映射；请求超过 Dalvik 512 上限的明确失败需保留 OOME/JNI_ERR 语义，不测试分配溢出 abort |
| 线程与 JavaVM | **2** | `javavm.AttachCurrentThread.named`、`javavm.DetachCurrentThread.dvm_javaThread` | runtime/jni_guest JavaVM attach args 解码、runtime/jni attachment 与 DexVM Java 栈/线程生命周期；分别补具名 args、核实 Java 栈存活时拒绝 detach 的策略 |
| 调用 ABI | **0** | boundary/call 原 176 条全部通过 | 本轮无 ABI 失败证据，不顺带重构调用路径 |
| 字段 | **0** | field 37/37 | 负数/高位/NaN payload、实例/静态读写均通过，无当前修复项 |
| 反射与 native 注册 | **0** | reflection 10/10、registration 2/2 | 双向转换及注销后的动态/导出重新解析通过，无当前修复项 |

WU8-2 不按报错逐条开单：先按指针 lease、Java 异常映射、共享未绑定槽建立可单次会话
验收的主题任务；pending 和 NIO 历史语义先作契约决策，再决定是否实施兼容行为。

## WU8-2a：未绑定槽、具名 attach 与 Java 帧 detach 保护（2026-10-22）

### 实现边界与结果

按 Dalvik `Jni.cpp` 接入 DefineClass（1198）、GetSuperclass/IsAssignableFrom（1264–1280）、
GetObjectRefType（662–679、2683–2685）及 StringCritical（2623–2645）。DefineClass 返回 NULL、
不挂异常，独立累计 `runtime.jni.DefineClass.dalvik_unsupported` warn 命中；run-dex ledger
的 `warnings` 数组提供 id、level、count 与首末 LR，未实现数组保持为空。
引用查询只查表及 handle 类别，不解引用失效 handle；严格模式删除 local 后按 ReportJniError
退出。Critical 与 Chars 复用 UTF-16 copy lease 实现、共享预算，但 kind/arena 独立，禁止交叉释放。
当前复制实现如实返回 isCopy=TRUE，直接指针仍留给 WU8-2c。

attach 按 `Jni.cpp:2785–2790、2815–2842` 解码 version/name/group：重复 attach 忽略参数，
显式 version 只接受 1.2/1.4/1.6，否则 EVERSION；NULL group 使用默认组，非空 group 明确失败并
记入 `runtime.jni.attach_thread_group`。名称校验后保留 Modified UTF-8 码元，交给 VM 物化
Thread；具名探针增加重音、编码 NUL 与代理对，经 Java getName/GetStringUTFChars 完整读回，
判据来自 `Jni.cpp:2831–2842 → Thread.cpp:1868–1871`。
JavaVM detach 在改动附着状态前，经 owner hook 检查 Java/native frame，按
`Jni.cpp:2880–2891 → Thread.cpp:2013–2029` 致命退出；环境 teardown 继续走既有清理入口。
aggregate 精确槽集合现为 226 JNIEnv（注入 NIO 后 229）+ 4 JavaVM，expected-unbound 只剩
DestroyJavaVM；无 NIO 的单测夹具仍不注入 NIO 三槽。

两后端各 **332/399（83.2%）**，较 WU8-1b 各增加 **11 pass**，既有 321 pass 零回归：

| 后端 | 规范级 | 其他 dvm_ | dvm_pending | 全套 |
| --- | --- | --- | --- | --- |
| switch | 322/347（92.8%） | 10/49（20.4%） | 0/3 | 332/399（83.2%） |
| threaded | 322/347（92.8%） | 10/49（20.4%） | 0/3 | 332/399（83.2%） |

转 pass 清单（两后端相同）：

- `version.DefineClass`
- `class_object.GetSuperclass`、`class_object.GetSuperclass.root`、`class_object.IsAssignableFrom`
- `reference.GetObjectRefType`、`reference.GetObjectRefType.dvm_popped`
- `exception.ExceptionOccurred.localRefType`
- `string.GetStringCritical`、`string.ReleaseStringCritical`
- `javavm.AttachCurrentThread.named`、`javavm.DetachCurrentThread.dvm_javaThread`

12 条目标中，11 条转 pass；`string.GetStringCritical.dvm_isCopy` 从未绑定转为断言失败，
期望 FALSE、实际 TRUE，属于本单允许保留的指针语义差异。两条死亡用例均退出 70，stderr
分别包含 `use of deleted local reference`、`detaching thread with interp frames`，且没有 PASS。
全套每后端剩余 **assert 38 / crash 29 / unimplemented 0**；数组与字符串指针语义 36、
异常输出与 Java 异常映射 25、引用容量 2、NIO 历史语义 1、pending 契约分歧 3；
调用 ABI、字段、线程与 JavaVM、未绑定槽主题当前均无失败。pending 分歧继续待决策。

### 验证与场景对比

证据根目录：`.local/analysis/wu8-2a/`。

- 仅构建 `ogplay-cli`、`ogplay_tests`；JNI 主题单测 **151/151、10,080 条断言通过**。
  新增分支检查涵盖接口/父类与 assignability 方向、引用类别/失效/弱引用清除、warn 查询、
  Critical 交叉释放与重复释放、attach 版本/幂等/Modified UTF-8/非空 group，以及拒绝 detach 后
  原附着状态仍完整。最终构建与单测日志为 `build-final.log`、`jni-unit-final.log`。
- 提交前对本次 C/C++ 修改行运行 clang-format（命令存 `format-command.txt`），`git diff --check`。
- `jni_tests.py --self-test` 通过；全量双后端先以 `--coverage --require-full --check-baseline`
  检查，再执行带 `--update-baseline` 的最终全量检查，报告为 `check/`、`final/`。
  **静态调用覆盖 233/233（100%），未覆盖 0**；排除项仅 DestroyJavaVM，理由未变。
  修改后的具名 Unicode 探针另有 `named/` 双后端通过证据。
- 使用 WU7-6 原 `run_scenarios_final.py` 的相同 fixtures、选项与两 worker 流程，仅重定向证据
  输出路径；旧摘要保存为 `scenarios-before.json`，最终证据为 `scenarios-final/`、
  `scenarios-final-summary.json`，逐项比较存 `scenarios-comparison.json`。

| 场景 | WU7-6 | WU8-2a | 对比 |
| --- | --- | --- | --- |
| tml | pass | pass | 无新增失败 |
| lili | pass | pass | 无新增失败 |
| wild_blood | pass | pass | 无新增失败 |
| tales | shutdown fail | shutdown fail | checkpoint 全过，仍为退出码 1 |
| dead_trigger | shutdown fail | shutdown fail | checkpoint 全过，仍为退出码 1 |
| dungeon_hunter | pass | pass | 无新增失败 |
| asphalt5 | pass | pass | 无新增失败 |
| pvz | shutdown fail | shutdown fail | checkpoint 全过，仍为退出码 1 |
| angry_birds | pass | pass | 无新增失败 |

最终 **6/9**，与 WU7-6 逐项相同，checkpoint/断言没有新增失败。未跑 dalvik 全量。
能力状态保持不变，按本单要求不改能力条目；`tools/capabilities.py check` 通过（416 项）。
