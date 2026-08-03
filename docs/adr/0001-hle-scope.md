# ADR-0001 · 采用进程级 HLE 兼容层

- 状态：Accepted
- 日期：2026-08-03

## 背景

目标是让 2010–2016 年 NDK 老游戏跨平台运行，同时避免完整 Android 系统的无限范围。

## 决定

翻译游戏 ARM 机器码，在 syscall、JNI、Android 框架和图形/音频边界使用宿主实现。
只实现游戏进程会直接调用的能力，不实现 Binder、system_server、Zygote 或完整 ART/Dalvik。

## 后果

启动和调试成本低，但兼容 API 必须逐项积累；所有缺失能力都必须显式、可观测、可回归。

