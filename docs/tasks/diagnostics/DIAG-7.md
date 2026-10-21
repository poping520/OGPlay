# DIAG-7 · WU6a dalvik/tests 一致性摸底

状态：全量试跑完成（run-dex 与驱动已实现）。依赖：ADR-0147/0148、DVM-255（BootDex 准入）、
DIAG-5/6。目标是用 AOSP 4.4 自带 `dalvik/tests` 的 expected.txt 作为与游戏无关的行为判据，
产出 DexVM 语义通过率与失败分类；本单不修复任何失败用例。

## 范围

- `ogplay-cli dexvm run-dex <dex|jar> [--class] [--backend] [--enable-assertions] [--ledger-json] [-- args]`：
  以生产相同的 bound core/Android catalog、BootDex、API19 骨架装配独立 VM 并调用 `main(String[])`。
  无 APK、Manifest，不启动图形或音频呈现；WU7-H 接入生产 guest 进程与 platform JNI/NIO，src/ 无测试专属分支。
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

以下为 `8b2c7b53` 的历史实施与验证；包隔离规则因原任务判据有误，已由本节末的修正取代。

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

### WU7-1a 修正 · 对齐 Dalvik 4.4 真机

- 原任务的包隔离要求有误。本次按 `vm/oo/Class.cpp:2867-2960` 的 createVtable 恢复
  b/7301030 兼容行为：同名同原型即覆盖，不以包名/loader 拒绝或另开槽；private 是 direct，
  不进入 vtable。允许缩小可见性、拒绝 final 与既有 intrinsic 准入保持不变。
- 包/loader 比较仅用于访问诊断：不可访问的覆盖发 `runtime.dexvm.override` 结构化 warn，
  含 parent_class、child_class、method、descriptor；message 含完整双方身份，
  RateLimitPolicy first_and_count 去重。Logger 显式注入 linker，Interpreter.SetLogger 同步转发；
  注入前的链接警告暂存，注入后发布，重新设置 logger 不重放。
- 原包隔离回归改为双后端检查同包、跨包、跨 loader 复用父槽并实际分派到子方法；
  private 不参与，跨包 final 覆盖链接失败，同时验证早期 warn 发布及重复警告的抑制计数。
  继承/intrinsic/skeleton 主题 50/50、1455 断言通过；只构建 ogplay-cli、ogplay_tests。
- 探针新增固定 ORACLE_DIVERGENCE，与 EXCLUDED 并列，300 记 excluded:oracle：
  expected.txt 对应 4.4 之后修复；4.4 createVtable 因 b/7301030 故意允许覆盖，源码位置 2919。
  只有表内 pass → excluded:oracle 可显式迁移，比较输出单列 oracle migrations；
  表外伪迁移、其他退步和缺失结果仍判回归，有回归仍拒绝写文件。
- --self-test 覆盖分类、合法迁移、非法退步、合法迁移与真实回归混合时拒写及原文件不变。
  目标 077/301/040 双后端 pass，300 双后端 excluded:oracle。
- 全量 --check-baseline 退出 0：0 回归、0 新增、0 其他变化，仅 300 两个后端的 oracle 迁移。
  随后 --update-baseline 成功，只改这两个分类；各 57/92（62.0%），分子/分母变化来自
  oracle 排除，非支持语义退步。日志 `.local/analysis/wu7-1a-correction-{self-test,tests,target,full,update}.log`。
- 九个基础场景正常模式、新沙盒各一次，复用 WU5 精确 fixture/显示参数；9/9 到达全部
  checkpoint，5/9 整体 passed（Lili、Wild Blood、Dungeon Hunter、Asphalt5、Angry Birds）。
  DT/Tales/PVZ 保持既有退出失败；TML 两次均达 1800 呈现帧后退出报 unknown guest thread id，
  与 CURRENT 的既有阻塞相同，但未达到原 6/9 计数要求，不能宣称九场景结果无变化。
  首跑与独立复跑均保留：`.local/analysis/wu7-1a-correction/scenarios-summary.json`、
  `scenarios/<id>/`、`tml-standalone/`；未修改预算、清理判据或线程退出实现。
- 回退五份链接/执行源码及声明到提交前 HEAD `cbefa2eb`，核对每份 git blob 哈希后只构建 CLI，
  TML 同路径新沙盒对照也在 1800 呈现帧后退出报 unknown guest thread id，确认本次修改前
  同错已存在。证据 `tml-control/`、`control-source-proof.json`；修正源码随后按保存字节恢复并重建。
  因此没有证据表明该退出错误由本次 override 修正引入，但本轮 6/9 的计数验收仍未达到。
- Scenario validator（9 文件）、capabilities check、diff check 通过；当前通过率与契约按
  4.4 实际语义更新。未扩大到线程退出修复，保留上述场景验证缺口。

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


## WU7-3 · 异常、栈轨迹与线程（2026-10-21）

- 独立 worktree 开发；局部行为修复，未改链接器。Throwable snapshot 保留方法身份和
  DEX PC；物化时从该方法的 DEX unit 读取 source_file 与 debug_info 状态机，采用不超过
  saved PC 的最后一条位置，缺失调试信息保留未知位置，native 行号为 -2。
- 对照 `vm/Exception.cpp:dvmFillInStackTraceInternal/dvmGetStackTraceRaw` 和
  `vm/interp/Stack.cpp:dvmLineNumFromPC` 保留原有 Throwable 顶层裁剪与完整调用帧。
  run-dex 现在实际经 `NativeStart.main → 应用 main` 调用；不是打印时补造帧。
  007 的原因链公共尾帧折叠和 054 的线程/默认 handler 输出均与 expected.txt 一致。
- check-cast 双后端按 `Exception.cpp:1242/throwTypeError` 使用可读点分隔类型名，
  数组显示 `[]`；消息为 `X cannot be cast to Y`。独立 cast probe 双后端输出
  `Classes cannot be cast to MoreClasses` 和 `true`，003 已越过原 CCE 断言。
- ThreadGroup.uncaughtException 依次虚派 parent、默认 handler；没有 handler 时向
  System.err 打印线程名前缀和原版 Throwable 栈；ThreadDeath 不输出。
  **来源差异**：本地 libdvm `ThreadGroup.java:688` 默认分支仅调用 printStackTrace，
  不带线程名前缀；本次按任务明确要求保留 `Exception in thread "..." ` 前缀。
- 生产路径审计：`Thread.start` 的 dispatcher 只选实例/default handler，无 handler 返回
  false，随后 VmThreadRuntime 将线程标为 failed、保存原异常、关闭等待并由
  `PumpJavaThreads` 在帧边界报告进程致命失败；不进入 ThreadGroup 默认 stderr 分支。
  保持该 Android RuntimeInit 等价的既有致命出口，不安装系统 RuntimeInit/Binder。
  新增双后端回归锁住此行为；APK 自动 uncaught 分派保持不变。后续为 003 接通显式 GC 后，
  另跑 TML/Lili 场景检查生产路径。
- 050 根因是 run-dex 的 DestroyJavaVM 等待只算 running，漏掉已 start、还在 created 的
  worker，main 返回后提前 teardown。现等待这两种存活状态的非 daemon 线程。
  对照 `dvmThreadInterrupt`（本地实现位于 Sync.cpp）、waitMonitor、Thread sleep，现有
  interrupt flag、唤醒与重新获取 monitor 后清标记逻辑已正确，无需改写；补双后端 parked
  sleep 与 wait 回归，验证 isAlive、InterruptedException、清标记和 monitor 重获。
- 恢复 ThreadGroup 输出后，全量首轮暴露 029 旧假 pass：setDefaultAssertionStatus 未实现，
  原异常打印缺失掩盖了它。按 libdvm `ClassLoader.java:692` 明确空方法补齐此必要依赖，
  不实现逐 loader 断言策略；双后端验证 setter 不改变 desiredAssertionStatus。
- A 线 `a57f1d86` 合入后，003 越过 UnresStuff 解析错误，暴露
  InternedString.testDeadInternedString 的弱引用断言：System.gc 原为空操作；既有 sweep
  又将 referent 清空错误地依赖于 ReferenceQueue.head 已链接。为目标验收的必要依赖，
  System.gc 现复用现有 STW 普通收集（对照 Runtime.cpp native gc），无队列的弱引用亦清空；
  入队仍只在队列与字段存在时执行，未改 finalizer/PhantomReference。
  双后端新增回归验证动态 intern 经显式 GC 消亡、字面量仍存活、无队列弱引用 get 为 null。
  007/054/050/003 双后端均 pass；未改链接器或源码测试判据。
- A 线恢复 bootstrap 按实际身份优先后，Thread/Monitor 测试夹具按生产契约先登记 BootDex
  再登记含平台重名类的 interp.dex；仅修正测试装配顺序。
- 仅记录后续失败：070 在 Main.java:29 抛 UOE `direct buffer allocator is unavailable`；
  094 在 Main.<clinit>(Main.java:72) 抛 PatternSyntaxException（invalid escaped character），
  外层 EIIE；079 在 PhantomWrapper.<init>(Bitmap.java:115) 暴露 PhantomReference 构造
  未实现的 UnsatisfiedLinkError，随后仍 wall timeout 60s。三个用例未扩修。
- 只构建 ogplay-cli、ogplay_tests、ogplay_app_tests（独立 worktree 首次补齐依赖构建）；
  Throwable/Thread/interpreter 主题 61 例、2578 断言通过（含 WU7-1a 修正）；新增 WU7-3 回归 6 例、179 断言
  通过；monitor interrupt 2 例、37 断言与 run-dex 4 例、19 断言通过。
  初次扩大到 class_name_codec 全文件时，既有 DVM-94 可见性拒绝旧断言失败，未扩修。
- 独立版本全量 `--check-baseline` 退出 0：0 回归，双后端各 62/93；007/050/054 新 pass，
  096 本轮也 pass，但上一轮曾 timeout，因此不提升该非目标的基线。只将 007/050/054
  的双后端新 pass 用 `--update-baseline` 合并。集成 A 线后的最终结果另见下项。
  证据 `.local/analysis/wu7-3/`，日志 `.local/analysis/wu7-3-{full-final,update,tests-refined,regressions-refined,monitor-tests,app-tests-refined}.log`。


- 最终集成验证：Throwable/Thread/interpreter/GC 行为筛选 73/73、3110 断言通过；
  新增 WU7-3 回归 7/7、199 断言通过；monitor interrupt 2/2、37 断言，run-dex 4/4、19 断言
  通过。GC 主题全文件初跑 75 例中仅两条未修改的侧表计数断言失败（预期 9/10，实际 8/9）；
  行为筛选显式排除这两项，未修改清单或侧表机制。日志 `wu7-3-final-{tests,behavior-tests,regressions,monitor,app}.log`。
- TML、Lili 正常 basic 场景各一次，复用 WU5 fixtures/显示参数、新沙盒；两者均达到
  1800 呈现帧且 no_guest_fault 通过，shutdown.clean=true、退出 0、整体 passed。
  证据 `.local/analysis/wu7-3/scenarios/{tml,lili}/`、`scenarios-summary.json`；不等于正式 title gate。

- 最终集成全量 `--check-baseline` 退出 0：0 回归、6 个新增后端 pass（003/072/096），
  0 其他变化、0 oracle 迁移；switch/threaded 各 72/92（78.3%）。007/050/054 在独立分支
  前阶段已提升；072/096 是显式 GC 接通后的直接改善，未单独扩修其算法。
  本轮对 003/007/050/054/072/096 再执行 `--update-baseline`；证据
  `.local/analysis/wu7-3/{integrated-full,final-update}/`，日志 `wu7-3-{integrated-full,final-update}.log`。
  CURRENT/能力通过率据本轮更新；capabilities check、场景校验、diff check 通过。



## WU7-T：HEAD 测试清理（2026-10-21）

基线 `155bf394`，独立 worktree；沿用 DVM-255 的分类方法。macOS arm64 Release，
只构建 ogplay_tests、ogplay_app_tests；Windows 未复跑。

| 分类 | 用例及处理 |
| --- | --- |
| 夹具与生产装配不一致 | network_sqlite 7 例缺 SandboxStore quota volume，SQLiteGlobal 的 StatFs("/data") 因而失败；改用独立临时沙盒并验证 /data 容量。AndroidValue/AndroidVm 补 VmThreadRuntime，Configuration 用例单独启用固定骨架；不改变其他无骨架对照。vm_thread 调整 BootDex/应用 DEX 登记顺序。ICU 日期/自定义时区/ChoiceFormat 与异常序列化的正向验证放到真实 guest JNI/ICU/crypto 夹具，core-only 夹具保留缺桥/缺 native loader 的精确失败检查。TLS oracle 在 POSIX 使用 python3，并显式终止、回收子进程，修复原 pclose 永久等待；服务端补 close_notify，避免 LibreSSL 因 raw shutdown 丢弃 session，原 session 身份/ID/超时判据保留。 |
| 线程夹具归属错误 | conditional swap pacing worker 借用了主线程仍在使用的 root context；阻塞释放执行锁不转移 context 所有权。对照 interpreter_context.cpp、interpreter.cpp 与生产 dexvm_bridge.cpp，改用独立 worker context，双后端验证主 context 跨线程调用仍被拒绝及两次 swap 顺序。全量另发现 AudioTrack 阻塞写同源问题，会在主线程异常后的 join 卡死；同样修正 worker context，断言前完成解除等待与 join。生产线程归属保护正确，保持原样。 |
| 断言过时 | core catalog 补 BaseDexClassLoader/ProxySelector/Posix 等精确未实现白名单；Android catalog 对 XmlResourceParser/FileUtils 明确失败使用精确白名单，对 guest native 放行但仍检查签名/来源。核对 SSLSocketFactory/File/stream/Animation 的 BootDex 来源与真实字段、方法；String/GC/guest override/boundary 编号以必要重载、签名唯一性、注册增量、实际虚表槽和独立 ID 验证不变量。Activity 使用当前仍不支持的自定义 affinity 作为拒绝对照；Intent null Bundle 按 ROM 字节码验证不改变状态；UI 用显式 WRAP_CONTENT 测量，保留原几何判据。ApplicationInfo installed 位、Configuration LTR 位、Resources 值强转、协作 shutdown onDestroy、SurfaceHolder generation、JNI 具体缺类 cause 与现行契约对齐。EGLImage 按后端扩展验证成功或明确拒绝，不新增 skip。 |
| HEAD 已修复 | reflection DeclaredMethods 缺类型异常、APK Support 的 Activity 期待在当前 HEAD 已分别验证缺失类型异常及 bootstrap Activity/app support 来源，无需再改。 |
| 真实生产缺陷 | Posix.access 忽略 W_OK，且 access/stat 缺文件系统混同 ENOENT；现消费 VFS 可写事实、缺装配抛 ENOSYS，原版 File 保留 false/null/IOException 判据。Posix.open 漏 O_EXCL：经 IoRuntime/adapter 传入 VFS，在索引锁内原子检查存在，拒绝早于截断；双后端验证重复创建与旧内容，VFS 并发 8 线程只允许 1 个赢家。EGL native context 影子状态漏 GL_DITHER 默认 true。独立原生探针复现 ANGLE Metal 忽略不同 EGL_READ drawable；默认 framebuffer 的 ReadPixels/CopyTexImage2D/CopyTexSubImage2D 局部绑定读 Surface 并恢复 currency，用户 read FBO 不改；原定向用例验证 surface 内容、复制和 draw/read 保留。 |

- HEAD 初始定向 network/reflection/catalog：76 例中 8 失败（network 7、catalog 1）；monitor
  独立复现 SIGABRT。初始全量在 AudioTrack 阻塞写挂起，采集宿主栈后停止，未作完整结果。
  清理中间轮完整全量为 1902 例、49 失败；其中统一骨架夹具尝试新增 3 例，已收窄装配撤回。
  后续定向 235 例仅余 6 项断言细节，已修正；倒数一轮全量 1903 例仅 Animation
  缺席旧断言失败，原始 DEX 确认为 BootDex 抽象类后改为来源/类型不变量。未新增 skip。
- 证据均在 `.local/analysis/`：`wu7-t-{known-before,monitor-before,before-tests,refined-tests,last-assertions}.log`；
  宿主栈 `wu7-t-{before,native}-sample.txt`；原生 Metal 复现 `wu7-t-egl-native-probe.{py,log}`；TLS oracle 独立探针 `wu7-t-tls-oracle-probe.{py,log}`。
  最终无过滤器全量：ogplay_tests 1903/1903、338188 断言通过，0 失败，保留原有 7 skip；
  ogplay_app_tests 89/89、1873 断言通过，0 失败/skip。两项均退出 0，日志
  `wu7-t-final-green-{tests,app}.log`；最终构建 `wu7-t-build12.log`。
  Python oracle 语法检查、UTF-8 与 diff check 通过。未改链接器、准入规则或生产 uncaught 路径。

## WU7-H · run-dex 平台运行时装配（2026-10-21）

- 诊断能力扩展：复用现有 `AndroidGuestProcess::Start → AndroidGuestCallSession::AdoptProcess`
  和 `DexVmGuestBridge`，未新增入口或测试分支。按 API19 Bionic profile 读取 system ELF，
  与 APK 相同通过 NativeLibraryLoader 执行 guest JNI 的 constructors/JNI_OnLoad；
  ICU 数据、tzdata 注入 VFS。JNI 双向桥、native 注册、NIO 和 VM 状态表均由生产 bridge 装配。
- 没有 APK/.so inventory，仅提供系统库来源；应用 System.load/loadLibrary 缺库抛
  UnsatisfiedLinkError。保留 stdout 字节、dalvikvm 退出码、-ea、真实 NativeStart.main
  帧、main 返回后的 root 终止及非 daemon 等待；关闭 bridge 的帧钟快进，继续由 realtime
  Clock 推进 Android 时钟。未修改 VM 字节码语义或 APK 启动流程。
- 初轮全量发现装配接入暴露的回退：051 Java-only worker 提前占 native 池（含 OpenSL 保留槽），
  065 未使用的非法 interface 图在 JNI 预发布阶段失败并触发构造清理崩溃，096 数组运输反复
  扫描类成员/反向身份导致超时。作为零回归的必要依赖，bridge 改为首次 native 操作才分配
  独占 CPU/TLS（VM 锁外，保留 OpenSL 槽），预发布的 Java 链接错误延至实际使用；
  值传输复用稳定的双向类身份缓存，显式 JNI 解析仍补齐新物化成员。采样进一步确认同一
  Object[] 的 arraycopy 被重复 JNI 校验拖慢：现由对象模型/store 在完整范围检查后批量移动，
  重叠方向正确、引用身份不变；跨数组失败前缀不变。补双向重叠、null、空区间/自复制与
  越界不写入回归。未放宽失败分类或 60 秒预算。
- 仅构建 ogplay-cli、ogplay_app_tests、受 bridge 影响的 ogplay_tests；run_dex_tests.cpp
  6/6、29 断言通过，含原有全部用例，
  新增双后端 `String.format("%,d", 1234567)` 输出 `1,234,567`、direct buffer putInt/getInt
  输出 42，以及应用 native 库拒绝。Locale.getDisplayName 重载尚未实现，未扩修。
  JNI 线程/对象、native loader、对象数组主题 30/30、1654 断言通过；arraycopy 的本地存储
  与失败前缀主题 2/2、52 断言通过。构建与最终定向证据：
  `.local/analysis/wu7-h-{build-finalized,app-finalized,jni-finalized,arraycopy-verified}.log`。
- 全量 `--check-baseline --jobs 4 --timeout 60` 退出 0：零回归、零新增 pass，
  仅 092 双后端由 fail:crash 变为 fail:unimplemented；双后端仍各 72/92（78.3%）。
  报告 `.local/analysis/wu7-h/full-final/`，日志 `wu7-h-full-final.log`。
  随后 native context 准备/退役补同一互斥，定向复验保持全通过；再次全量
  `--update-baseline --jobs 4 --timeout 60` 退出 0、零回归，仍各 72/92，仅更新 092
  两个分类。最终报告 `update-final/`、日志 `wu7-h-update-final.log`；capabilities/UTF-8/diff
  检查通过，未改分类器或排除表。
  初轮与定位证据：`wu7-h-full.log`、`wu7-h-065-lldb-bt.log`、`wu7-h-096-sample.txt`；
  051/065 双后端恢复见 `wu7-h-regressions-final.log`（该阶段 096 仍超时），096 最终恢复
  见 `wu7-h-copy-regression.log`（switch 36.57s、threaded 23.25s）。

以下两后端结果一致，作为 WU7-6 输入；本次只消除装配遮挡，不修后续语义缺口：

| 用例 | 分类 | 第一处差异 / 后续首错 | ledger |
| --- | --- | --- | --- |
| 070-nio-buffer | fail:output | 第 1 行 `has array: true` → `false`；继续至 Main.storeValues:139，负 byte put/get 报 `byte get/store failed` | 空 |
| 092-locale | fail:unimplemented | 第 3 行应为 `France(GMT): Monday, January 2, 2012`，实际 `UnsupportedOperationException: locale is outside the audited ICU set`（LocaleData.initLocaleData）；捕获后继续至 Main.testNormalizer:126，NFD 骨架字段未实现 | `dexvm.skeleton.field.Ljava/text/Normalizer$Form;->NFD:Ljava/text/Normalizer$Form;` ×1 |
| 094-pattern | fail:output | 第 1 行应为 `str1 matches: true`，实际 ExceptionInInitializerError；Main.<clinit>:72 的 WEB_URL Pattern.compile 抛 PatternSyntaxException（invalid escaped character / trailing escape） | 空 |
| 062-character-encodings | fail:unimplemented | 第 1 行应为 `Missing: []`，实际 UnsatisfiedLinkError：Charset.availableCharsets 未实现（Main.main:17） | `dexvm.intrinsic.Ljava/nio/charset/Charset;.availableCharsets()Ljava/util/SortedMap;` ×1 |

## WU7-6 · guest ICU51 补齐（2026-10-21）

实现、APK 证据及退出验收见 [DVM-257](../dexvm/DVM-257.md)。区域资源取消白名单，
补 double/digit-list、API19 Pattern/Matcher 与 Normalizer native，统一走 guest ICU51 C ABI；
Charset 枚举只发布六种已支持的实际 codec。未引入宿主 ICU/locale/regex。

- 062、092、094：双后端逐字节 pass，ledger 空。
- 070：双后端 fail:output，ledger 空；byte/short 符号扩展已修，后续数值输出全部一致。
  唯一差异为第一行 direct buffer `has array: true` → `false`；共享 byte[] backing 仍缺。
- 全量检查零回归，更新基线后各 75/92（81.5%，原 72/92）；报告
  `.local/analysis/wu7-6/{full,baseline-updated,full-final}/`。未改分类器、oracle 或排除。
- WB 新沙盒 GAThread 已越过 formatDouble；下一首错为
  `ContentValues$1.<init>()V`，不扩修独立能力。正常九场景的退出依赖与最终结果详见任务单。

## WU7-4 · 堆、finalization 与引用队列（2026-10-21）

实现与证据见 [DVM-258](../dexvm/DVM-258.md)，装配决定见
[ADR-0151](../../adr/dexvm.md#adr-0151)。Runtime 内存查询、ActivityManager 内存档位和
VMRuntime 的利用率、growth limit、native allocation 记账共用 JavaObjectModel 堆配置。
4.4 已退役的 external allocation 方法执行原版；其余未支持 native 仍明确记账失败。

- 原版 FinalizerReference/Daemons/PhantomReference 进入 BootDex；分配登记、weak/soft 清除、
  finalizer zombie 保活、复标记、phantom 入队依次执行，支持复活与一次 finalize。
  runFinalization 等待进入时的 pending/active 批次；原版吞异常、10 秒 watchdog 经统一
  Clock 执行，超时请求 guest exit(2)，不杀宿主。030 的卡死输出亦逐字节符合 oracle。
- 074 同时修正两后端过期 invoke result 根和回收后复用 Thread 句柄的身份判断。
  native owner 在 JNI 关闭前占有 token、成功退役、异常恢复，避免异步 finalize 与 GC 重复关闭。
- 030、036、059、061、074、079、098 双后端全部 pass，ledger 空；079 不再超时。
  全量检查零回归，基线由各 75/92（81.5%）升至 82/92（89.1%），增加 14 个后端 pass；
  报告 `.local/analysis/wu7-4/{full-native-final,baseline-updated}/`。
- 仅构建受影响 CLI/测试目标；GC、内存、线程、ICU、zlib 等主题 98/98、7851 断言通过，
  真实 guest crypto/BKS 1/1、3528 断言通过；BootDex build/check、payload 与工具检查通过。
  未修改 guest JNI 源码，不重建 libogplay_jni。
- 九个新沙盒正常场景保持 6/9 passed，与 WU7-1b 相同通过集合、零回退，全部运行检查点通过。
  既有退出失败仍为 Tales 读 0x315f0001、Dead Trigger 清理超预算、PVZ stopService；
  比较证据 `.local/analysis/wu7-4/scenarios-comparison.json`，非正式 title gate。

## 剩余

- libcore/JUnit 测试、src-ex 二级 dex（068/071）、多 dex 不在本单。
