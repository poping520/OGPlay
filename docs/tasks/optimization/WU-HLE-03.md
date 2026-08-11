# WU-HLE-03 · Seal dispatch table

目标：初始化完成后封口 GLES1 与 extension handler 表，让已绑定正常调用无锁读取固定 slot。

验收：

- [x] `Seal()` 以 release/acquire 语义发布不可变 handler 表。
- [x] 封口后禁止新增或替换绑定，重复封口保持幂等。
- [x] 已绑定 Invoke/IsBound 不获取 dispatch mutex。
- [x] 未绑定调用继续在线程安全计数后明确失败。
- [x] HLE 阶段统一构建与测试：`windows-msvc` 构建通过，CTest 503/503。
