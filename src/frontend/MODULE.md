# 模块：frontend

## 职责

提供 CLI 与未来 Qt GUI；二者只编排公共内核 API，不实现兼容行为。

## 公共 API

CLI 支持版本、能力账本、结构化 Agent 请求，以及 M4 最小 API 19 NativeActivity APK
的交互窗口运行；通用 import/profile 与 GUI 留在 M6。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile。
- 用户可见输出可以写 stdout/stderr，但生产库不得裸输出。
- `run-apk` 要求显式 API 19 Bionic 目录；APK 必须恰有一个未压缩 armeabi-v7a native 库，
  不猜测多库入口。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI smoke 由 CTest 驱动；GUI 行为测试在 M6 增加。
