# WU-HLE-04 · A32CallFrame

目标：统一 A32 HLE 参数帧，以固定数组保存 r0-r3，并一次批量读取最多五个栈参数。

验收：

- [x] base 与生成 GLES descriptor 发布精确 parameter count，最大值为 9。
- [x] 9 参数调用只执行一次 guest stack bulk read，不按参数重复加锁/验证。
- [x] GLES1 Invoke 直接使用固定 call-frame span，不再构造临时 vector。
- [x] Android、EGL、GLES1/2 handler 删除重复 `StackWord()` helper。
- [x] 测试覆盖 9 参数、线程/LR 元数据、越界参数数和越界访问。
- [x] HLE 阶段统一构建与测试：`windows-msvc` 构建通过，CTest 503/503。
