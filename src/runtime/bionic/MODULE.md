# 子模块：runtime/bionic

## 职责

选择 API 19/22/23 Bionic profile，装载和自检真实 guest libc/libdl，建立 Bionic TLS，并只为
确有生产 handler 的 libc 热点提供宿主边界。

## 依赖

依赖 `loader`、`memory` 与 CPU 状态契约；不得依赖 JNI、framework、syscall、execution 或
integration。线程和 syscall 只通过上层装配接入。

## 不变量

- 普通符号默认执行真实 guest Bionic，选择性拦截必须有显式声明和真实 handler。
- TLS slot、自指针、thread info 与 API profile 必须精确。
- profile 只接受 Android API 19、22、23。

## 测试

对应 `tests/runtime/bionic_*_tests.cpp`。
