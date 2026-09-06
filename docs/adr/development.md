# 构建、依赖与开发流程

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0005 · CMake、固定依赖与 doctest](#adr-0005)
- [ADR-0007 · 第三方源码依赖统一使用 Git submodule](#adr-0007)
- [ADR-0008 · 里程碑出口不得依赖后续阶段能力](#adr-0008)
- [ADR-0012：有界 CURRENT 与里程碑任务归档](#adr-0012)
- [ADR-0014 · ANGLE 预编译 SDK 交付](#adr-0014)
- [ADR-0015 · ANGLE 维护者工作区归属](#adr-0015)

<a id="adr-0005"></a>

## ADR-0005 · CMake、固定依赖与 doctest

- 状态：Accepted
- 日期：2026-08-03

### 背景

三平台需要一致构建和可机器判定的验证。DEMO 自制测试框架不利于发现、筛选和 CI 报告。

### 决定

使用 CMake 3.25+、C++20 与 Ninja 预设；依赖固定版本。M0 使用 doctest + CTest，后续通过
包管理器接入大型依赖。构建不得隐式依赖游戏文件。

### 后果

首次配置可能需要获取 doctest；离线构建可提供 `OGPLAY_DOCTEST_SOURCE_DIR` 指向预置源码。

<a id="adr-0007"></a>

## ADR-0007 · 第三方源码依赖统一使用 Git submodule

- 状态：Accepted
- 日期：2026-08-03
- Supersedes: ADR-0005 中的依赖获取方式

### 背景

M1 开始引入 SDL3 与 Dynarmic。配置期下载和混用系统包会让同一提交在不同机器上得到
不同依赖源码，也不利于离线复现与许可证审计。

### 决定

所有需要源码参与构建的第三方库以 Git submodule 放在 `third_party/`，主仓库 gitlink
固定到明确提交。包含自身 submodule 的依赖必须递归初始化。CMake 不使用 FetchContent，
CI checkout 必须启用 recursive submodules；依赖未初始化时配置明确失败。

### 后果

- 克隆后必须执行 `git submodule update --init --recursive`。
- 依赖升级通过单独 Work Unit 更新 gitlink、许可证记录并完成全量构建测试。
- 默认配置不访问网络，同一主仓库提交对应唯一第三方源码集合。

<a id="adr-0008"></a>

## ADR-0008 · 里程碑出口不得依赖后续阶段能力

- 状态：Accepted
- 日期：2026-08-03

### 背景

原 M1 出口要求最小 NDK 样例在三平台跑出画面并响应输入，但 NativeActivity APK 的运行
同时依赖 M2 的 ELF/Bionic/syscall、M3 的生命周期边界和 M4 的 EGL/GLES/ANGLE。
该出口无法在 M1 能力范围内达成，也会诱导提前实现后续模块。

### 决定

每个里程碑的出口只能依赖本阶段及此前已经完成的能力：

- M1 使用无 APK/ELF/Bionic/JNI/EGL/GLES 依赖的裸 guest mailbox 样本，不要求画面；
- M2 使用导出普通 C 入口的无界面 NDK `.so` 验证 ELF/Bionic 与 syscall；
- M3 使用无界面生命周期/JNI 契约样本，不要求真实 present；
- M4 才以 NativeActivity NDK APK 三平台画面和输入作为累积集成出口。

### 后果

- 各阶段出口可独立、机器化验收，不需要临时桩冒充后续能力。
- 已完成的 `minimal_ndk` APK 保留，但重新归类为 M4 出口载荷。
- M1 新增裸 guest 样本，以标准化 mailbox 输入和确定性状态写回连接 CPU 与 memory；
  SDL 窗口/输入、线程和时钟仍由各自契约测试验证。

<a id="adr-0012"></a>

## ADR-0012：有界 CURRENT 与里程碑任务归档

- 状态：Accepted
- 日期：2026-08-04

### 背景

截至 M2，`CURRENT.md` 已经累计 103 个 Work Unit 简介并增长到约 16 KiB。它既是每次
会话的必读入口，又承担永久变更日志，导致后续阶段的启动上下文随历史线性增长。全部 WU
位于同一目录也降低了阶段定位效率。

### 决定

- `CURRENT.md` 只保存当前阶段、当前任务、最近完成、下一步和阻塞，大小上限为 6 KiB。
- 长期未解决事项进入 `KNOWN-ISSUES.md`；能力状态继续以 `capabilities.toml` 为准。
- 每个完成里程碑建立 `M*-ACCEPTANCE.md`，保存出口结论、测试证据和范围边界。
- Work Unit 从创建起放入 `docs/tasks/m<里程碑>/`，编号全局递增，完成后不移动。
- 既有 WU-0001..0103 执行一次性 Git 路径迁移，任务内容与历史不改写。
- CTest 对 CURRENT 大小、历史 WU 分区、编号唯一性、验收文档和旧扁平路径进行门禁。

### 结果

新会话的必读状态保持有界，历史仍可通过里程碑验收、单个 WU 和 Git 精确追溯。新增
里程碑只增加新目录和验收文档，不再扩大 `CURRENT.md` 或单个任务目录。

<a id="adr-0014"></a>

## ADR-0014 · ANGLE 预编译 SDK 交付

日期：2026-08-04

Supersedes：ADR-0007 中 ANGLE 必须由消费端源码构建的部分；其他第三方源码依赖仍遵循
ADR-0007。

### 背景

ANGLE 的顶层源码并不大，但官方 gclient 同步会展开 Chromium 工具链、CIPD 包和约 74 个
依赖 checkout，本机完整工作目录已达到 15.5 GB。普通 OGPlay 开发者只需要稳定的 EGL、
GLESv2、运行时依赖和公共头，不应在每台电脑重复下载及编译完整依赖图。

### 决定

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

### 包布局

```text
<root>/<windows|linux|macos>-<x64|arm64>/<release|debug>/
  manifest.json
  include/{EGL,GLES,GLES2,GLES3,KHR}/
  lib/
  bin/          # Windows 运行时文件
  symbols/      # 可选，不属于默认 Release 包
  licenses/
```

### 后果

常规 checkout 只获取目标平台包，ANGLE 的 15.5 GB 维护工作区可以独立清理或保留用于升级。
二进制仓库需单独决定可见性并创建远端后，才能把 gitlink 纳入主仓库；在此之前主仓库先固定
包契约、校验器和消费路径，不使用不可移植的本地路径冒充 submodule。

<a id="adr-0015"></a>

## ADR-0015 · ANGLE 维护者工作区归属

日期：2026-08-04

Supersedes：ADR-0014 中保留 `third_party/angle` 源码 submodule 的部分。

### 背景

普通 OGPlay 开发已经只消费 `angle-prebuilt`，继续在主仓库保留 ANGLE 源码 gitlink 会让
checkout、CI 和依赖边界仍然暴露一个无需构建的源码依赖。完整 gclient 工作区和原始归档
只服务二进制 SDK 的维护者生产流程。

### 决定

- OGPlay 删除 `third_party/angle` submodule，只保留 `third_party/angle-prebuilt` 消费依赖。
- 本机 ANGLE checkout 与原始归档归入未跟踪的 `.local/angle-prebuilt-repo/` 维护工作区；
  它们不得提交到二进制仓库。
- 源码 commit 固定在 `tools/build_angle.py`，构建前必须与本地 checkout 完全匹配；生成的
  SDK manifest 继续记录相同 commit 和 GN 参数，升级时二者必须同一 WU 更新。
- `--source` 允许其他维护者使用任意本地绝对目录，但不放宽 commit 校验。

### 后果

普通 OGPlay clone 不再获取任何 ANGLE 源码；维护者仍可复用既有 15.5 GB 增量工作区生成
三平台 SDK。源码工作区的备份与清理由二进制仓库维护流程负责，不属于 OGPlay 消费流程。
