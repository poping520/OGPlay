# BND-43 · 原生 guest CPU 执行控制（第二阶段）

状态：第二阶段完成（原生执行控制）。决策：[ADR-0081](../../adr/runtime.md#adr-0081)。

## 范围

- 显式启用按虚拟核数限制 A32/T32 同时执行数量，guest/host 线程仍为 1:1。
- 独立的进程总速率上限，单位为百万 backend tick/秒；不将标称 MHz 换算为真实周期。
- 主线程、clone、DexVM native 工作线程、JNI 重入、音频 guest callback 共用进程预算。
- 排队公平，速率等待经 Clock 计时；guest 时间及原有 watchdog 预算保持独立。
- 进入 HLE/JNI/syscall 前释放执行名额；限额模式关闭快速 SVC 内联分派，防止阻塞/重入死锁。
- 取消唤醒等待者，teardown 解除限额以允许已有生命周期清理和 finalizer 正常收尾。
- 默认关闭；CLI/单游戏设置接入。Java 解释器仍由原有 VM 锁串行，不对 Java 字节码限速。
- 不处理 coprocessor 首错、CPU 热插拔、`/proc/stat` 或精确物理周期模拟。

## 验证

- Windows `windows-msvc` Release 的 `ogplay_tests`、`ogplay`、`ogplay-gui` 构建通过；
  本机沿用 `_CL_=/wd4996 /wd4834` 屏蔽既有测试告警。
- 执行/查询/runner/clone 定向 41 用例、496 断言通过；GUI 设置与 RPC 6 用例、187 断言通过。
  覆盖真实两个 Dynarmic CPU 的 1/2 名额上限、单进程总速率、虚拟 Clock 计量、FIFO、
  配额退款/异常、取消/RequestHalt、关闭时解除名额及速率等待、快速 HLE 转慢路、
  真实 API19 libc 在限额下调用与停止，以及默认模式不变。
- CLI 拒绝 0、负数、超界和非数字速率；GUI 保留设置并只传递开启的执行参数。
- 原 APK/OBB，无 Profile/无 survey、临时沙盒、3 帧上限，配置 2 核、1500 MHz，
  开启并发限制和 20 百万 tick/秒速率。仍到达已有 coprocessor 断言（退出 `0xC0000409`），
  cpuinfo ENOENT 未复发，`/proc/stat`/GC 警告保留；不代表完整游戏兼容验收。
- 为容忍 Windows 等待粒度，token bucket 保留 50 ms 的有限突发余量，每片最多使用半桶
  且不超过 50000 tick；这仍是 backend 计费 tick 上限，不保证达到设定速度或等于物理周期。
- 证据：`.local/cpu-execution-{build-verified,tests,gui-tests,cli-tests,startup-verified}.log`。

## 使用

在原 run-apk 参数后追加 `--cpu-cores 2 --cpu-limit-parallelism
--cpu-max-mticks-per-second 20`：最多两个原生 CPU 同时执行，全部线程合计每秒
2000 万 backend tick。单游戏设置位于“性能”；标称 MHz 仍位于“设备”。
