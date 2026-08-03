# ADR-0003 · 图形使用 ANGLE，窗口输入使用 SDL3

- 状态：Accepted
- 日期：2026-08-03

## 背景

DEMO 的手写 GLES→桌面 GL 转译产生能力上报、固定管线和跨平台语义问题。

## 决定

GLES 语义交给 ANGLE；guest 边界调用由 IDL 生成。窗口、键鼠和手柄统一使用 SDL3。

## 后果

Windows/Linux/macOS 共用实现，CI 可接软件后端；禁止迁移 DEMO 的 `gl_bridge.cpp`。

