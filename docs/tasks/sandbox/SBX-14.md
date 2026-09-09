# SBX-14 · 匿名 mmap 原子选址

## 目标

基于真实内存映射账本选址，消除线性游标撞固定区产生的伪 ENOMEM。

## 依赖

- SBX-13 故障诊断；memory AddressSpace 和 Android 内存 syscall 契约。

## 交付

- memory 提供 MapAnywhere：有界 first-fit 选址与映射共用账本锁，PROT_NONE
  同样占用，清零和页表/映射世代发布复用 Map。
- mmap2 删除独立游标，失败不消费地址，munmap 后空洞直接复用；brk 状态加锁。
- 保持匿名私有范围；不改 MAP_FIXED 覆盖限制，不补文件映射，不改 Profile 预算。

## 验证

- 定向验证 guard 避让、空洞复用与清零、分配失败不改变可用空间、最后一页、
  连续空间耗尽、并发分配唯一性，以及 syscall 真实返回值。
- Release ogplay/ogplay_tests 构建成功；定向 9 用例、65 断言通过。
- exact PvZ 探索实跑越过原内存故障到第 1 帧，新首错为
  `Intent.putExtras(Bundle)` 缺失，不代表完整启动场景验收。
- 重编译暴露 api19_guest_process_tests.cpp 缺少 ostream 完整类型，补齐直接 include。
  对应启动环境/固定布局回滚 5 用例、38 断言通过。
- no_raw_output/capabilities_monotonic 通过；platform_boundaries 被既有
  frontend/gui/process_manager.cpp 平台条件分支阻挡，未修改该无关文件。
