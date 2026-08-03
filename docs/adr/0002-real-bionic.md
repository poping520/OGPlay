# ADR-0002 · 默认加载真实 AOSP Bionic

- 状态：Accepted
- 日期：2026-08-03

## 背景

DEMO 重实现 libc 的细节偏差会令 guest 静默走入错误路径，并难以维护 Android 多版本行为。

## 决定

正式版默认加载 API 19/22/25 的 AOSP Bionic，只选择性拦截性能热点、pthread 和宿主边界，
在 `svc #0` 处 HLE 约 120 个 syscall。

## 后果

ABI 保真度由真实 Bionic 提供；项目必须先完成 syscall、TLS、futex 与真线程。发行库必须
来自可追溯的 AOSP 构建，设备提取物只能作为开发期行为 oracle。

