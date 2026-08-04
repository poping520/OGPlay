# ADR-0014 · ANGLE 预编译 SDK 交付

日期：2026-08-04

Supersedes：ADR-0007 中 ANGLE 必须由消费端源码构建的部分；其他第三方源码依赖仍遵循
ADR-0007。

## 背景

ANGLE 的顶层源码并不大，但官方 gclient 同步会展开 Chromium 工具链、CIPD 包和约 74 个
依赖 checkout，本机完整工作目录已达到 15.5 GB。普通 OGPlay 开发者只需要稳定的 EGL、
GLESv2、运行时依赖和公共头，不应在每台电脑重复下载及编译完整依赖图。

## 决定

- 消费端默认使用独立 Git 仓库发布的 `third_party/angle-prebuilt` 浅 submodule；仓库只保存
  按 `平台-架构/配置` 划分的可重定位 SDK，不保存 ANGLE 源码、中间文件或构建工具链。
- 首选 Release ANGLE 同时服务 OGPlay 的 Debug/Release 构建。EGL/GLES 是 C ABI，OGPlay
  调试构建不要求 ANGLE 同为 Debug；需要调试 ANGLE 内部时才单独发布含符号包。
- 每个 SDK 必须包含 EGL/GLES 公共头、链接库、运行时文件、许可证、ANGLE commit、完整
  GN 参数以及每个文件的大小和 SHA-256；CMake 配置阶段验证清单后才能创建 imported target。
- `third_party/angle` 源码 submodule 只保留为维护者升级输入。构建、打包、验证脚本必须能从
  固定 gitlink 重现二进制包，普通配置和远端增量测试不得依赖其 gclient 工作目录。
- 平台和 CPU 必须显式匹配宿主；禁止跨目录回退、从系统目录猜测 EGL/GLES 或校验失败后
  继续构建。

## 包布局

```text
<root>/<windows|linux|macos>-<x64|arm64>/<release|debug>/
  manifest.json
  include/{EGL,GLES,GLES2,GLES3,KHR}/
  lib/
  bin/          # Windows 运行时文件
  symbols/      # 可选，不属于默认 Release 包
  licenses/
```

## 后果

常规 checkout 只获取目标平台包，ANGLE 的 15.5 GB 维护工作区可以独立清理或保留用于升级。
二进制仓库需单独决定可见性并创建远端后，才能把 gitlink 纳入主仓库；在此之前主仓库先固定
包契约、校验器和消费路径，不使用不可移植的本地路径冒充 submodule。
