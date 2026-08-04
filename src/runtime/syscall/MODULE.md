# 子模块：runtime/syscall

## 职责

实现 Android ARM Linux syscall 目录、SVC bridge、ARM kernel helper、VFS syscall 适配，以及
exit/exit_group/clear-child-tid 所需的 guest 线程生命周期状态。

## 依赖

依赖 `cpu`、`memory`、`hal` 与 `runtime/vfs`；不得依赖 Bionic、JNI、framework、execution
或 integration。

## 不变量

- 未实现和未知 syscall 统一返回 `-ENOSYS` 并可观测。
- guest 地址必须经受检内存访问；时间源只使用统一 Clock。
- 线程状态只能按 running → exit-requested → exited → reap 前进。

## 测试

对应 `tests/runtime/syscall_tests.cpp`、guest thread lifecycle 与 SVC bridge 测试。
