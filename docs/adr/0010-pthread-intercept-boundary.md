# ADR-0010 · pthread 保留 Bionic ABI并在 syscall 边界映射宿主线程

- 状态：Accepted
- 日期：2026-08-04
- Supersedes：ADR-0002 中“pthread 函数默认由宿主拦截”的部分，不改变真实 Bionic 主路线

## 背景

API 19/22/23 的 `pthread_t`、mutex、condition variable、TLS 和退出清理均为版本相关的
Bionic 内部 ABI。若在 `pthread_create/join/mutex_*` 函数入口替换成另一套宿主对象模型，
就需要伪造这些内部结构，并重新引入 DEMO 已出现过的“返回成功但没有真实语义”风险。

M2 累计样本证明，真实 Bionic 可以通过受检的 `clone`、futex、set_tls、signal、mmap 与
线程退出 syscall，在每个 guest 线程对应一个宿主线程的模型上正确运行。

## 决定

- pthread 函数保持 guest execution，由真实 Bionic 维护其版本 ABI；
- 线程创建、等待、TLS 和内存的宿主映射发生在 syscall/CPU 边界；
- 函数级选择性拦截只发布确有 handler 和性能基准的 mem 热点；
- mmap/open 继续在 syscall 层拦截，log 继续作为结构化 HLE 边界库。

## 后果

- 三个 Bionic 版本共享同一宿主线程内核，不需要伪造三套 pthread 内部结构；
- pthread 真并行和 join 语义由累计样本验证，未实现 syscall 仍进入能力账本；
- 新增函数级拦截前必须同时提供生产 handler、行为测试和性能基准，不能只登记 thunk。
