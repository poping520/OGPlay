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
