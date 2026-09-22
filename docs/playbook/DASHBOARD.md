# 用 Dashboard 定位首错与停滞

适用 Windows `run-apk`；Linux 暂缓。Dashboard 只读，控制仍走
[MCP](MCP-TESTING.md)。完整宿主栈和退出卡住取证沿用
[排查手册](TROUBLESHOOTING.md#停滞与退出卡住)。

## 1. 打开页面并保留取证入口

已有构建可直接使用；静态制品缺失或过期时，按 [Web UI 构建](../../webui/README.md#构建)
生成后运行 `cmake --build --preset windows-msvc --config Release --target ogplay ogplay_stage_data`。
更新制品后重启会话，HTTP 服务不会热加载。无需启动 Vite/Node 开发服务器。

在仓库根启动（把 `game.apk` 换成目标 APK）：

```powershell
./build/windows-msvc/Release/ogplay.exe run-apk game.apk `
  --mcp --mcp-manual-step --diag --diag-dir .local/diagnostics `
  --diag-on-teardown-timeout 5
```

等控制台打印 MCP/Dashboard ready，再打开 `http://127.0.0.1:15971/dash/`。
自动运行时去掉 `--mcp-manual-step`。端口冲突时用 `--mcp-port <端口>` **替换** `--mcp`，
同步修改浏览器与 MCP 客户端地址，不同时传两个开关。

GUI 从运行实例或详情的 Dashboard 入口打开；需启用 MCP、实例仍被跟踪且服务就绪。
自动打开也有这些前提。要使用诊断写盘，启动时选择诊断模式或显式传入 `--diag`。
只有 MCP/Dashboard 不会创建诊断 coordinator；取证入口无法在卡住后补开。

核对 package、installation_id、profile、API 和端口。需要确定 PID 时读取下节响应的
`process_id`，不要把 PowerShell 自带的 `$PID` 当成游戏进程。

## 2. 定位首错

1. 先清除筛选回到实时，确认连接正常；记录 lifecycle、frame、presented 和 guest ticks。
   手动模式尚未发 `step`，或已经 suspend 时，不前进是预期状态。`step` 返回只代表排队，
   要等 `session_state` 达到 targetFrame 才确认完成。
2. 出现 guest fault 时先保留 `guestFault` 和结构化日志中的原始异常，再查看能力账本。
   点击 capability 只关联同名结构化键；“无匹配日志”不等于没触发，也不能用最后一条
   JNI 错误替代原始 Java 异常。判读见 [通用症状](TROUBLESHOOTING.md#已知易误判的症状)。
3. 点击 guest 线程或 native/A32 表中的 context/host，查看 Java 栈、在途 native 和 syscall。
   monitor owner 可跳到线程；futex address 可缩小等待者。只有真实共享键才关联。
   不同模块的 generation 不因数值相同就属于同一代。
4. 按症状查看相关面板：资源失败看 VFS fd/node_id 与文件 syscall；音频看 player 的
   written/consumed/queue/underrun；加载失败看动态库 state/failure；图形看 GLES trace/pacer。
   不扩大到无关模块，也不因拓扑可用就认定游戏兼容。
5. 悬停帧采样图选择历史快照；带实际时间键的事件按所选窗口过滤。记录结束后点
   “清除 / 回到实时”。历史被淘汰会提示，不能用当前状态补齐历史证据。

在进程退出或环覆盖前保存证据。以下只读请求保存原始 JSON-RPC 响应，保留大整数精度：

```powershell
$dashUri = 'http://127.0.0.1:15971/dash/rpc'
$dashEvidence = Join-Path '.local' ('dash-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $dashEvidence -Force | Out-Null
$dashUtf8 = [System.Text.UTF8Encoding]::new($false)
$dashRequest = '{"jsonrpc":"2.0","id":1,"method":"dash.snapshot","params":{"sections":["session","diagnostics","capabilities","log"]}}'
$dashResponse = Invoke-WebRequest -UseBasicParsing -Uri $dashUri -Method Post `
  -ContentType 'application/json' -Body $dashRequest
[System.IO.File]::WriteAllText((Join-Path $dashEvidence 'snapshot.json'), $dashResponse.Content, $dashUtf8)
$dashRequest = '{"jsonrpc":"2.0","id":2,"method":"dash.events","params":{"since_sequence":0,"limit":1000}}'
$dashResponse = Invoke-WebRequest -UseBasicParsing -Uri $dashUri -Method Post `
  -ContentType 'application/json' -Body $dashRequest
[System.IO.File]::WriteAllText((Join-Path $dashEvidence 'events.json'), $dashResponse.Content, $dashUtf8)
```

检查响应有 `result` 而非 `error`；HTTP 200 不等于查询成功。响应超预算（`-32003`）时缩小
sections，竞争（`-32002`）时重试一次并保留失败事实。按需要把 sections 换为 `vfs/audio/cpu`
等相关来源。`dash.thread` 的参数为 `{"guest_tid":线程整数}`。

单页事件不是完整历史：继续取页时使用响应的 `next_sequence`；检查 `gap/dropped` 和
来源丢失计数。快照和事件分别采样，不承诺跨请求原子一致；配对时核对 `stream_id`，
进程重启后另建证据目录并从游标 0 开始。

## 3. 从疑似停滞转入独立取证

| 观察 | 判断与下一步 |
| --- | --- |
| frame 不变，但处于 manual-step 无额度或 suspended | 先按 MCP 流程确认模式/命令，不判卡死 |
| 已有执行额度，间隔采样仍无进展 | 比较 lifecycle unchanged、active execution 的 PC/进展、Java 栈及 native enter/return；重复 PC 只是线索 |
| 某区 unavailable/busy | 来源锁竞争或局部不可读，不是零活动；转诊断快照保留缺口 |
| monitor confirmed_cycles 非空 | 已确认 entry→owner 等待环，按 context 找线程；Object.wait 和 futex 等待不能单独证明死锁 |
| 页面断线或 RPC 超时，进程还存活 | 不依赖页面继续操作，直接走下述 OS 取证入口 |

MCP 仍响应且已启用诊断时，调用工具 **`diag.snapshot {}`**。成功返回 `path`，读取对应
JSON 和同序号 TXT；超时/未装配会明确失败。该工具不推进帧，也不是 `/dash/rpc` 的方法。

主循环或 HTTP 不响应时，在另一终端使用真实游戏 PID 和**同一个**诊断目录：

```powershell
$guestProcessId = 12345 # 替换为该会话的 process_id
./build/windows-msvc/Release/ogplay.exe diag snapshot --pid $guestProcessId `
  --diag-dir .local/diagnostics
```

控制入口来自启动时打印的 `diag-control-<pid>.json`。先核对 PID/目录和进程是否仍存活；
未启用诊断时此命令不能补建控制入口，下一次复现再加 `--diag`。
`--diag-on-teardown-timeout` 只对 teardown 超时触发取证，不是运行期 watchdog，也不会强杀。

先读 `diag-<pid>-<seq>.txt` 首行与 sections，再按相同 context/host_tid 对照 Dashboard。
不能把不同采样时刻拼成确定因果。需要宿主栈时保留进程，按
[停滞与退出卡住](TROUBLESHOOTING.md#停滞与退出卡住) 使用 procdump/WinDbg 和
`tools/diagnostics/align_host_threads.py`；Dashboard 不替代完整线程 dump。

## 4. 读数边界

| 数据 | 可以得出的结论 | 不能得出的结论 |
| --- | --- | --- |
| complete / partial / unavailable | 来源可用程度；busy 是本拍缺口 | 游戏兼容、无错误、上一拍仍有效 |
| 8 Hz、600 个历史采样、4096 个客户端事件 | 有界观测窗口，实际间隔可能变长 | 每帧完整记录、精确 FPS 或帧耗时 |
| 空心 GC/欠载/能力缺口/flush 标记 | 累计计数在观测间隔内变化，首次从零基线观察 | 精确发生帧/时刻；首次增量全部刚发生 |
| 空泳道、空表、无匹配记录 | 可能未装配、缺共享键、被筛选或已覆盖 | 没有事件、没有缺口 |
| CPU 缓存 | 最近完成 Run 的 owner 发布值，行时间戳可查 | 当前运行中的即时值；未发布不等于零 |
| GL error | 当前 FrameService 未记账，字段 null、事件来源 unavailable | GL 错误为 0，或由 trace 无 error 证明无错 |
| AudioTrack / VideoView / UiTree | 已接入 player 统计、视频基准位置、树/dirty/focus | 全部音源、实时解码队列或输入 capture/队列 |

FFmpeg 不可用原因在视频面板显示；解码未装配不是播放器没有请求。
GLES trace 没有线程/帧键时不强行归属；VFS FD 可以复用，node_id 缺失时不能跨时间认定同一文件。
更多支持边界见 [DASH-02](../tasks/gui/DASH-02.md) / [DASH-04](../tasks/gui/DASH-04.md)。

## 5. 记录、回归与收尾

一条排查记录保留：APK/Profile/实例、提交版本与启动参数、PID/stream_id、复现操作、
最后有进展的 frame/ticks、首个原始错误、相关共享键、section 状态，以及 snapshot/events/
diag/必要 dump 的路径。证据放 `.local/`，不把临时地址写进生产代码。

局部修复后复跑相同触发路径，记录原错是否消失及下一首错；这只是 reached-fault 证据。
正式兼容验收使用 [Scenario 三轮复跑](SCENARIO-RUNNER.md)，不能用 Dashboard 的空泳道代替。
取证完成后通过 MCP `shutdown`（已装配会话控制时）或关游戏窗口收尾，确认进程和 listener
退出。关闭 Dashboard 页面本身不会停止游戏；卡住的进程先保留所需 dump，再按排查流程处置。

契约与验证入口：[agent](../../src/agent/MODULE.md)、[Web Dashboard](../../webui/apps/dashboard/MODULE.md)、
[capabilities.toml](../../capabilities.toml) 的 `agent.dashboard` / `frontend.dashboard`，
[Dashboard 测试](../../tests/agent/dashboard_tests.cpp)、
[HTTP 测试](../../tests/frontend/mcp_http_server_tests.cpp)、
[ADR-0026](../adr/diagnostics.md#adr-0026)。
