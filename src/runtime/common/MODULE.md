# 子模块：runtime/common

## 职责

保存多个 runtime 子模块共同消费、没有上层所有权的稳定 POD/枚举契约。

## 不变量

- GuestNetworkInterface 只表达 guest 接口索引、flags、MTU、链路及 IP 地址值；
  拓扑派生、发布与生命周期归 integration，不含宿主网络资源。

- GuestFileSystemStats 保存 API19 statvfs 的 11 个无符号数值字段；块/条目单位、
  来源与支持范围由 provider 决定，Java 回填检查 long 范围；不含宿主路径或指针。
- 只依赖标准库和更低层公共类型；不得包含 session、integration、JNI、syscall 或 boundary
  实现。
- `SupervisorCallProgress` 只表达可观测进展分类，不决定 watchdog 预算或 teardown 策略。
- `GuestCpuConfig` 保存 guest 可见核数与标称 MHz，以及独立、默认关闭的
  `GuestCpuExecutionConfig` 策略；只有 integration 装配时才转换为下层执行预算。
  标称 MHz 不转换为 tick 或物理周期。
- GuestProcFacts/GuestMemoryPressurePolicy/GuestMemorySnapshot 是显式虚拟设备配置与
  值快照：默认总量 1 GiB、空闲 512 MiB，缓存缺省 total/4。64 位字节查询与 proc
  格式共享同一值对象；验证、进程所有权和发布只在 integration 装配，不采样宿主内存。

- GuestProcessMemorySnapshot 独立表达 API19 进程 KiB 七列、三类及明细，保留注入/虚拟
  提交来源；不是系统 availMem 或宿主 RSS。校验和计账模型在 integration 实现。

## 禁止

- 不把便利 helper、业务状态或可变全局状态堆入 common。

Guest call 默认看门狗额度由 guest_call_limits.h 唯一定义为 100 亿 tick；上层可用
显式请求做机器测试，不把它解释为 guest 设备参数或每游戏设置。
