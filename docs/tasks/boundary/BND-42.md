# BND-42 · 可配置虚拟 CPU 查询（第一阶段）

状态：第一阶段完成。决策：[ADR-0080](../../adr/runtime.md#adr-0080)。

## 范围

- 每个进程冻结 `GuestCpuConfig`：1..32 核、1..10000 MHz，默认 1 核/1000 MHz。
- 同源发布只读 `/proc/cpuinfo`、CPU sysfs 拓扑与固定标称频率（kHz）。
- Bionic 导出的 `sysconf` 核数查询、Java `Runtime.availableProcessors` 与
  `Posix.sysconf(_SC_NPROCESSORS_CONF/_SC_NPROCESSORS_ONLN)` 使用同一核数。
- CLI `--cpu-cores`、`--cpu-frequency-mhz`；单游戏设置持久化并传给 CLI。
- 指令能力由固定 ARMv7/Dynarmic 支持面决定，不复制真机厂商/序列号/Features。
- 不改变宿主线程数量、VM 锁、Clock、tick 预算或实际执行速度；不实现 CPU 热插拔、
  动态调频、CPU 利用率和 `/proc/stat`。全局设备预设选择仍是独立待接入功能。

## 验证

- Windows `windows-msvc` Release `ogplay`、`ogplay_tests`、`ogplay-gui` 构建通过。
  本机沿用 `_CL_=/wd4996 /wd4834` 屏蔽既有测试告警。
- 定向 26 用例/514 断言通过：1/4/32 核、频率单位、只读/冲突/隔离/撤销、隐式目录
  清理、真实 API19 libc 的核数与 page size/非法 selector、ARM/Thumb 尾调用、
  VFP/NEON 基本算术、Java 查询与显式失败、GUI 参数传递及原有 proc/meminfo 路径。
- CLI 五组越界/非法值均拒绝；WebUI TypeScript 检查、实例配置 3 项测试与 GUI 制品构建通过。
- 原 APK/OBB，无 Profile/无 survey、临时沙盒、3 帧上限，使用 4 核/1500 MHz 复跑：
  `/proc/cpuinfo` 的 FileNotFoundException 消失；仍止于 Dynarmic
  `Should raise coproc exception here`，进程退出 `0xC0000409`。GC 直接读取 `/proc/stat`
  的警告仍存在（未经过 sysconf）；不宣称解决该统计入口或通过游戏验收。
- 证据：`.local/cpu-facts-{build-final,test-build,tests,cli-tests,webui-check,webui-build,startup}.log`。

## 修订 · 2026-10-29：API19 cpuinfo 完整格式

- `.local` 未发现内核源码，核对上游 [Linux v3.4 ARM setup.c 的 c_show](https://github.com/torvalds/linux/blob/v3.4/arch/arm/kernel/setup.c#L988-L1044)，
  按 SMP 分支输出（含单核 processor/BogoMIPS 组、Features 尾空格、制表符及空行）。
- `GuestCpuEnvironment` 的不可变虚拟 Cortex-A9 r0p0 模型统一提供 MIDR、架构、
  Processor/platform、既有 Dynarmic Features 与虚拟 Hardware；variant/revision 固定为零。
  板级 Revision/Serial 使用内核无板级信息时的零值。BogoMIPS 按虚拟 delay loop
  每周期一轮及内核 `lpj × HZ / 500000` 公式得到 `2 × MHz`，不采样宿主。
  sysfs/sysconf 行为不变，不实现 nvmap 或 NV system clock。
- macOS Release `ogplay-cli`、`ogplay_tests` 构建通过；定向 14 用例/285 断言通过，
  包含 1 核/2 核 cpuinfo 全文逐字节比较、不同频率、CPU 字段、既有 Features、
  只读拓扑、真实 API19 sysconf 与进程 proc 发布。账本检查通过（433 条）。
  命令：`build/release/ogplay_tests --source-file='*guest_cpu_environment_tests.cpp,*android_guest_call_session_tests.cpp' --test-case='*BND-42*,Android guest proc*,legacy Android guest call session delegates process ownership'`。
- GTA3 在 `build/release` 沿用 `./ogplay-cli library run com.rockstar.gta3`，临时 MCP
  端口 15972；EULA 已接受，无需重复点击。原线程 2 读 `0x8` 不再出现，进入
  `PlayMovieInWindow` / `movieViewCreated surfaceCreated firsttime`；下一首错为
  `Canvas.drawColor(int, PorterDuff.Mode)` 未实现，位于 `WarMedia$11.surfaceCreated`（pc 17），
  退出 1，未修。EGL NV system clock 提示仍在；MCP 设置内容及原文件存在状态精确恢复。
  此为 reached-fault 证据，不是 title gate。
- 证据：`.local/gta3-cpuinfo/` 的 `build.log`、`focused-tests.log`、
  `capabilities-check.log`、`gta3.log`、`gta3-result.json`、`settings-restored.json`。
- 九个场景各跑一次，6/9 通过，无新增失败；与 `.local/gta3-dialog-visibility/` 基线
  比较，全部 status/firstFailure 一致，三项失败的相关原始日志行也完全一致：
  Tales 退出读 `0x315f0001`，Dead Trigger teardown cleanup wall-time budget exhausted，
  PVZ `stopService` 未实现。TML、Lili、Wild Blood、Dungeon Hunter、Asphalt 5、Angry Birds 通过。
  证据见 `scenarios-cpuinfo/`、`scenarios-cpuinfo-summary.json`、`scenario-comparison.json`；
  未跑全量测试，未进行正式 title gate 或 Windows 验收。
