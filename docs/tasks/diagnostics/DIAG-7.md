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

## 剩余

- libcore/JUnit 测试、src-ex 二级 dex（068/071）、多 dex 不在本单。
