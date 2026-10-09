# 子模块：runtime/common

## 职责

保存多个 runtime 子模块共同消费、没有上层所有权的稳定 POD/枚举契约。

## 不变量

- 只依赖标准库和更低层公共类型；不得包含 session、integration、JNI、syscall 或 boundary
  实现。
- `SupervisorCallProgress` 只表达可观测进展分类，不决定 watchdog 预算或 teardown 策略。
- `GuestCpuConfig` 保存 guest 可见核数与标称 MHz，以及独立、默认关闭的
  `GuestCpuExecutionConfig` 策略；只有 integration 装配时才转换为下层执行预算。
  标称 MHz 不转换为 tick 或物理周期。
- GuestProcFacts/GuestMemoryPressurePolicy/GuestMemorySnapshot 是显式虚拟设备配置与
  值快照：默认总量 1 GiB、空闲 512 MiB，缓存缺省 total/4。64 位字节查询与 proc
  格式共享同一值对象；验证、进程所有权和发布只在 integration 装配，不采样宿主内存。

## 禁止

- 不把便利 helper、业务状态或可变全局状态堆入 common。
