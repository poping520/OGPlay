# DIAG-7 · WU6a dalvik/tests 一致性摸底

状态：探针已实现（run-dex 与驱动）；全量试跑与报告待执行。依赖：ADR-0147/0148、DVM-255（BootDex 准入）、
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

## 剩余

- 全量 98 用例双后端试跑与 `.local/analysis/wu6/REPORT.md`（交由测试 Agent）。
- libcore/JUnit 测试、src-ex 二级 dex（068/071）、多 dex 不在本单。
