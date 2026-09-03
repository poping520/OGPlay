# 排查手册

按“症状 → 结构化查询 → 判断 → 修复 → 回归”记录可复用经验。新增条目必须链接相关测试、
能力账本项和 ADR；不得只记录某个游戏的绝对地址或临时环境变量。

建议索引词：黑屏、缺失 UI、guest fault、死锁、资源缺失、音频欠载、JNI 异常、GL error。

dexvm 适配过程中的失败信息判读表见 [NEW-TITLE.md](NEW-TITLE.md) §3；本篇只收跨
title 的通用症状。手册总览见 [README.md](README.md)。

## 停滞与退出卡住

启动时显式打开诊断；超时值只触发取证，不会强杀 guest：

```powershell
.\build\windows-msvc\Release\ogplay.exe run-apk game.apk `
  --diag --diag-dir .local\diagnostics --diag-on-teardown-timeout 5
```

控制台会打印 `diag-control-<pid>.json`。主循环已卡住时，从另一终端触发 OS 路径：

```powershell
.\build\windows-msvc\Release\ogplay.exe diag snapshot --pid <pid> `
  --diag-dir .local\diagnostics
```

先读 `diag-<pid>-<seq>.txt` 首行和 `sections`。`unavailable: busy` 表示局部证据缺失；futex
只有 waiter/wake 事实，没有 owner 就不能断言死锁。随后按 lifecycle unchanged 时间、active
execution 的 ARM PC/last progress、Java 栈、最后一个未闭合 native enter、monitor
`confirmed_cycle`、pacer blocked 与最近 GLES/syscall 顺序缩小范围；不要跨不同 section 的
采样时刻拼成确定因果。需要宿主栈时保留进程并执行：

```powershell
procdump -accepteula -ma <pid> .local\diagnostics\ogplay-<pid>.dmp
windbg -z .local\diagnostics\ogplay-<pid>.dmp
```

WinDbg 中用 `~* k` 列出全部线程。其 `Id: <pid>.<tid>` 默认是十六进制，而快照
`host_tid` 是十进制；可以人工换算，或把输出保存后运行：

```powershell
python tools\diagnostics\align_host_threads.py --snapshot <diag.json> --stacks <windbg.txt>
```

Linux/macOS 使用 `lldb -p <pid>`，再执行 `thread backtrace all`；对齐工具识别
`tid = 0x...`/十进制。PDB/DWARF 缺失时只记录 `module+offset`，禁止按相邻符号猜函数。
Windows 定向诊断构建使用 `windows-msvc` 预设并保留对应配置目录的 PDB；Release 是否含
完整符号取决于构建配置，不把缺符号解释为无线程活动。dump 和诊断 JSON 可能含运行现场，
交付前按需要脱敏，确认无用后删除。

## 已知易误判的症状

| 症状 | 真实含义 | 处置 |
| --- | --- | --- |
| native 侧报 `GetObjectClass` 之类无关的 JNI 错 | 解释方法抛出的 Java 异常按 JNI 语义置为 pending，下一次 native 调用被严格门禁挡下，报出的却是那次无关调用 | 入向桥会把 pending 异常打成结构化 `warn`（类/消息/解释器栈）；先看这条 warn，不要查报错的那个 JNI 函数 |
| `register vX out of range`，而 X 比方法声明的寄存器数只大 1 | `GetWide(vN)` 需要 `vN`/`vN+1`，越界消息报的是 `vN+1` | 先核对方法的 `registers`/`ins`，再怀疑 precheck 规则（k22b 类 bug 有先例） |
| 缺资源条目时抛宿主 C++ 异常，游戏直接崩 | 游戏期望的是可捕获的 Java 异常 | 平台侧缺失必须抛游戏真能 catch 的异常（如 `AssetManager.open` 缺条目抛 `java.io.IOException`），既不伪造成功也不炸宿主 |
| 长时运行后 GC 预算耗尽 | 瞬态资源数组把 GC-A 预算打满（Asphalt 5 换语言/进赛道路径实测） | 先按 profile `[runtime.dexvm]` 兜住并记录，根治靠精确标记清除（GC-B），不要在解释器里打特例 |
