# 已知问题与长期限制

本文件记录跨会话仍然有效、但不一定阻塞当前 Work Unit 的事项。已解决项由 Git 历史保留，
不在这里累积；`CURRENT.md` 只列当前阻塞。

| ID | 状态 | 目标阶段 | 说明 |
| --- | --- | --- | --- |
| KI-0001 | active | 持续 | `cpu.interpreter` 是确定性参考/诊断后端，未覆盖完整 A32/Thumb-2、VFP/NEON、原子访问和异常模型；主执行后端为 Dynarmic，缺失指令必须明确失败。 |
| KI-0003 | planned | M4 | Linux 无显示环境可使用 SDL Unix-console；可见窗口构建仍需要 X11 或 Wayland 开发依赖。 |
| KI-0006 | active | 持续 | hosted CI（`.github/workflows/ci.yml`）已在 GitHub Actions 运行；最近 3 次 main 推送（2026-10-11、10-12）均在约 1–2 分钟内失败，原因未诊断。本地与测试机结果不替代 hosted CI 状态。 |
| KI-0007 | upstream | 持续 | doctest 2.4.11 在 CMake 4.x 配置期产生上游旧 policy deprecation warning，不影响 warnings-as-errors 编译与测试。 |
| KI-0008 | planned | 下一次 | 生命周期主线程有几处不持 VM 锁读取 Java 对象记录：启动阶段的 StartDexApplication、SetSystemProperty、AttachAndroidActivityIdentity，以及每帧的 DexActivityLifecycle::CallActivity/CallOnView 和 AndroidUiTraversalRoot。这些对象有根，不会被回收，但另一条 Java 线程分配时对象表可能扩容，构成数据竞争。排查方法：在 JavaObjectModel 记录访问处检查 `VmExecutionLock` 的属主，跑一次纪念碑谷即可列出全部路径。主 Looper 泵的同类问题已修复。 |
| KI-0009 | planned | 下一次 | `tests/runtime/property_animation_tests.cpp` 的 DVM-337 断言已过时：它仍预期 View 带缩放时 `setAlpha` 抛 UOE，但 DVM-372 已把这种情况改为 RecordUiDegraded，不再抛出。该断言应改为验证降级，P0 的受影响测试列表没有覆盖这个文件。 |

## 开发验证约束

- Windows 本机只使用 MSVC 预设，不再使用 Cygwin。
- 测试机连接信息、凭据和外部绝对路径不得写入仓库文档。
