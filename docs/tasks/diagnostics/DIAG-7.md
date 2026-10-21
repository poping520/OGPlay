# DIAG-7 · WU6a dalvik/tests 一致性摸底

状态：全量试跑完成（run-dex 与驱动已实现）。依赖：ADR-0147/0148、DVM-255（BootDex 准入）、
DIAG-5/6。目标是用 AOSP 4.4 自带 `dalvik/tests` 的 expected.txt 作为与游戏无关的行为判据，
产出 DexVM 语义通过率与失败分类；本单不修复任何失败用例。

## 范围

- `ogplay-cli dexvm run-dex <dex|jar> [--class] [--backend] [--enable-assertions] [--ledger-json] [-- args]`：
  以生产相同的 bound core/Android catalog、BootDex、API19 骨架装配独立 VM 并调用 `main(String[])`。
  无 APK、Manifest、native 进程、图形或音频；src/ 无测试专属分支。
- 语义对齐 dalvikvm：guest System.out/err 原样写宿主 stdout（run-test 本就合并 2>&1）；
  main 返回 0、未捕获异常经线程 handler 或 ThreadGroup 分派后 1、System.exit(n) 为 n；
  main 返回后 root 线程死亡并唤醒 joiner，再等待非 daemon 线程；VM/装配错误为 70。
- 统一 Clock：realtime `hal::RealtimeClock` 推进 Android 单调时钟，sleep/wait/System 时间同源。
- `-ea` 只作 dalvikvm 全局开关（无逐类规则），APK 运行保持关闭。
- 驱动 `tools/conformance/dalvik_tests.py`：复制用例→javac 1.7（API 19 android.jar）→dx `--debug`
  →双后端运行→逐字节比较→分类；7 个自定义 build 已移植，排除表固定且每项写明理由。

## 实施记录

- LogOutputStream 的提交带“是否行尾”：AOSP PrintStream autoflush 每次 print 都会 flush，
  原日志端点把片段当整行且丢空行，无法还原字节。日志路径行为不变（空片段不记）。
- d8 1.5.13 编译时删除 javac 断言代码且无开关，改用同一 build-tools 包里的 dx（即 default-build 原工具）。
- 小样本试跑已暴露的语义缺口（只记录，不修）：Throwable 栈缺行号与 `Main.main` 帧；
  ThreadGroup.uncaughtException 明确未实现；应用 dex 内 `android.*` 包类不可加载（004）；
  链接器拒绝缩小可见性的重写（077，与 Angry Birds onDestroy 失败同类）；包私有方法被跨包覆盖（300）。

## 验证

- `ogplay_app_tests -sf="*run_dex_tests.cpp"`：3 例 15 断言（参数、双后端原样字节、退出码）通过。
- `tools.dalvik_tests_self_test` 通过；`capabilities.py check` 通过。
- 回归：android_logging、intrinsics_p1、thread_intrinsic、vm_thread、application_exit、
  activity_window_lifecycle、DVM-255 过滤器与 surface_export 全部通过。network_sqlite（7，
  `Invalid path: /data`）、reflection（1）、vm_monitor（1）失败在未改动的 HEAD 上同样复现，与本单无关。
- 全量试跑（`dalvik_tests.py --jobs 4 --timeout 60`）：switch 52/93（55.9%）、threaded 52/93（55.9%），分母为未排除 93 例；报告 `.local/analysis/wu6/REPORT.md`。

## WU7-0 · 只升不降基线（2026-10-21）

- 从 `.local/analysis/wu6/summary.json` 生成仓库基线
  `tools/conformance/dalvik_baseline.json`，保存逐用例、逐后端分类。
- `--check-baseline` 将 pass 退步判为回归并退出 1，新增 pass 与其他分类变化分别报告；
  `--only` / `--backend` 仅比较选中范围。`--update-baseline` 有回归时拒写并退出 1，
  无回归时合并本次结果，保留未运行项。分类逻辑、排除表不变，不接 CTest 全量门禁。
- `--self-test` 通过：覆盖分类变化、各类回归、缺失结果、子集/后端比较、拒绝更新及子集合并。
- 全量 `--check-baseline --out .local/analysis/wu7-0/full`：退出 0，双后端仍各 52/93，
  0 回归、0 新增 pass、0 其他变化；日志 `.local/analysis/wu7-0-full.log`。
- `--only 012-math --check-baseline --out .local/analysis/wu7-0/subset`：退出 0，
  双后端均 pass，0 回归、0 新增；日志 `.local/analysis/wu7-0-subset.log`。
- `capabilities.py check`、`git diff --check` 通过；用法与语义改动后的全量回归要求已补入工具说明和开发参考。

## WU7-1a · vtable override 判定（2026-10-21）

- 局部行为修复，依赖 WU7-0：取消缩小可见性的拒绝；扫描继承槽，private 不覆盖，
  包私有只在同 defining loader、同包名时覆盖，否则追加槽并保留父槽。intrinsic 父类同规则，
  final 与 intrinsic 显式覆盖准入仍保留；无调用方或游戏特化。
- 来源差异：本地 AOSP `vm/oo/Class.cpp:createVtable` 因 b/7301030 刻意允许跨包包私有覆盖，
  与本地 300 的 `expected.txt` 及本任务明确要求冲突。本次采用任务指定的隔离规则；
  loader/包名判定对照 `AccessCheck.cpp:dvmInSamePackage`，不声称逐项复刻该源码的历史 bug。
- 去掉可见性拒绝后，077 另暴露 static/virtual 错配被报为宿主解析错误；这是目标用例
  转 pass 的必要依赖。本次仅补反向类别命中的 ICCE 原因，并在两后端共用执行出口转为
  Java `IncompatibleClassChangeError`，不扩大缺失方法、字段或访问检查的修复范围。
- miranda/abstract 审阅：abstract 槽按同一访问规则参与覆盖，040/301 双后端 pass。
  当前 miranda 按签名判槽，缺少 AOSP `createIftable` 的“非 abstract 接口实现必须 public”
  链接检查；此差异只记录，未扩修。
- 只构建 `ogplay-cli`、`ogplay_tests`；继承主题新增双后端回归，覆盖缩小可见性、
  intrinsic 同包/跨包、同包不同 loader，以及跨包后返回祖先包时复用较早槽。
  `ogplay_tests -sf='*interpreter_tests.cpp,*intrinsic_contract_tests.cpp,*skeleton_surface_tests.cpp'`：
  49/49、1261 断言通过；证据 `.local/analysis/wu7-1a-linker-tests.log`。
- 五个目标用例双后端：077/300 新转 pass，040/301 保持 pass；037 保持 fail:output
  （`64.000000` 与 `64.0` 差异）。日志 `.local/analysis/wu7-1a-target.log`。
- 全量 `--check-baseline`：退出 0，0 回归、4 个后端结果新增 pass、0 其他变化，
  switch/threaded 各 54/93（58.1%）；日志 `.local/analysis/wu7-1a-full.log`。
  随后以 `--only 077-method-override --only 300-package-override --update-baseline`
  合并提升基线，退出 0，仅这两个用例的双后端分类改变；日志 `.local/analysis/wu7-1a-update.log`。
- 九个 `.basic` 正常场景各一次，复用 WU5 `fixtures.json` 的 exact APK/资源及显示参数，
  每场景独立新沙盒；未改场景、预算或断言。TML、Lili、Wild Blood、Dungeon Hunter、
  Asphalt5、Angry Birds 整体 passed；Dead Trigger 退出 cleanup 超时、Tales 退出读
  `0x315f0001`、PVZ 退出 `ContextWrapper.stopService` 未实现，整体 failed。
  九款的目标帧与 no_guest_fault checkpoint 均通过；6/9 整体通过不等于 title gate 验收。
- 对照 DIAG-6 初始记录：TML/Wild Blood/Angry Birds 从 failed 变为 passed，Dead Trigger
  仍退出失败；PVZ 的退出失败已由 DIAG-6 后续音频停滞修复记录确认。Tales 相比旧记录
  确有 passed→failed，但 CURRENT 已记该原生退出故障；临时恢复 HEAD 的三份链接/执行
  源文件并仅构建 CLI，同路径对照也失败于 `0x315f0001`，排除本次修改引入。当前实现已恢复。
  对照源码哈希与证据：`.local/analysis/wu7-1a/control-source-proof.json`、`tales-control/`；
  九款结果/命令：`.local/analysis/wu7-1a/scenarios-summary.json`、`scenarios/<id>/`。
- Angry Birds：5000 呈现帧 checkpoint 通过，onDestroy 不再被可见性校验拒绝，
  `shutdown.clean=true`、退出 0；本路径未出现下一处失败，不宣称视觉验收。
- Scenario validator（9 文件）、`capabilities.py check`、`git diff --check` 通过；
  CURRENT 更新 Angry Birds 到达点及探针通过率。

## WU7-2 · 数组存储与 core 字符串语义（2026-10-21）

- 按局部行为修复处理；未改链接器、分类器、排除表或游戏逻辑，未跑游戏场景。
- 数组：两后端的 aput-object 原已有兼容性检查，本次去掉未知 value class 可跳过检查的条件，
  并补双后端 ASE/不写入、null、越界优先级回归。011/083 的实际根因是 System.arraycopy
  引用路径直接写入：现按 Dalvik `java_lang_System.cpp` 逐元素校验，首个不兼容元素前
  的前缀保留后抛 ASE，重叠拷贝仍安全；非数组先抛 ASE，再检查范围。
- 浮点：String.valueOf(float/double) 的 std::to_string 产生六位定点尾数；
  Float/Double.toString 原另用宿主 to_chars 近似。两者及实例 toString 现统一委托 pinned
  BootDex RealToString，与原版 StringBuilder/StringBuffer.append 使用同一算法、ThreadLocal
  和既有 native digit generator。测试夹具按该依赖挂载 VmThreadRuntime。
- String.regionMatches：原有普通重载，缺 boolean 重载；现共享 libdvm String.java 的
  null/范围/非正 length 规则，逐 UTF-16 char 比较。忽略大小写采用 lower(upper(char))，
  Character 的 ASCII 快路保持原版阈值，其余通过原版 toLowerCaseImpl/toUpperCaseImpl
  接入 guest ICU51 u_tolower/u_toupper，不使用宿主 locale 或整串大小写映射。
- Enum：valueOf 及 Enum.name/toString 正常；String.valueOf(Object) 原直接返回 `@句柄`，
  现在 null 返回 "null"，其余虚派实际对象 toString 并保留异常，常量子类覆盖也生效；
  根因不在链接器/vtable。
- 目标五例双后端：011、083、037、055 均 pass；003 从 fail:unimplemented 变为 fail:output，
  ledger 为空。当前停于 Classes.checkCast：CCE 消息为 `LClasses; cannot be cast to LMoreClasses;`，
  原版断言要求消息前七字是 `Classes`，故 regionMatches(false,...) 正确返回 false 后断言失败。
  双后端独立取证 `.local/analysis/wu7-2/cast-probe/`；未扩修异常消息。
- 定向 C++：interpreter/core_catalog/intrinsics_p1 的相关 59 例、3237 断言通过；
  `-tce` 排除两项未改逻辑的结构检查：catalog 旧未实现白名单缺 BaseDexClassLoader /
  ProxySelector / Posix 等项、APK Support 测试期待已由 skeleton 提供的 Activity 不存在。
  初跑详情保留 `.local/analysis/wu7-2-tests.log`，最终相关结果 `wu7-2-tests-final.log`；
  未用这些结构检查的失败替代本次行为验证，也未顺带维护其无关断言。
- Unicode/guest native：在现有 guest crypto/ICU 主题补双后端 regionMatches 回归，覆盖
  土耳其 I、final sigma、Kelvin、ß 不展开、surrogate 与 supplementary Character 简单映射；
  该测试 1 例、3386 断言通过（`wu7-2-unicode-tests.log`）。复用 builder 原版转换检查
  1 例、1176 断言通过（`wu7-2-builder-tests.log`）。
- 只构建 ogplay_tests、ogplay-cli 及受影响的统一 guest JNI；guest 库两次独立构建一致，
  已更新库及 manifest；Android payload 校验通过（`wu7-2-guest-build.log`、`wu7-2-payload.log`）。
- 全量 `--check-baseline` 退出 0：0 回归、8 个后端结果新增 pass，003 双后端仅分类变化；
  switch/threaded 各 58/93（62.4%）。随后对上述五例执行 `--update-baseline`，无回归才
  合并写入，保留其余项；日志 `.local/analysis/wu7-2-{target,full,update}.log`。
- capabilities check、diff check 通过；更新受影响 MODULE 的语义边界与 CURRENT 通过率。

## 剩余

- libcore/JUnit 测试、src-ex 二级 dex（068/071）、多 dex 不在本单。
