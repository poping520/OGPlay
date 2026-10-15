# 构建、依赖与开发流程

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0005 · CMake、固定依赖与 doctest](#adr-0005)
- [ADR-0007 · 第三方源码依赖统一使用 Git submodule](#adr-0007)
- [ADR-0008 · 里程碑出口不得依赖后续阶段能力](#adr-0008)
- [ADR-0012 · 有界 CURRENT 与里程碑任务归档](#adr-0012)
- [ADR-0014 · ANGLE 预编译 SDK 交付](#adr-0014)
- [ADR-0015 · ANGLE 维护者工作区归属](#adr-0015)
- [ADR-0072 · Web UI 使用固定 Node 构建链，产物按生成制品交付](#adr-0072)
- [ADR-0137 · 共享应用库与桌面/CLI 双入口](#adr-0137)
- [ADR-0138 · 共享库 C++ 外观接口与游戏库下沉](#adr-0138)
- [ADR-0073 · Windows 启动器 WebView 宿主边界](#adr-0073)

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

<a id="adr-0072"></a>

## ADR-0072 · Web UI 使用固定 Node 构建链，产物按生成制品交付

- 状态：Accepted
- 日期：2026-09-21
- 关联：[GUI v2 设计](../design/gui/README.md)、[运行时 Dashboard 设计](../design/gui/dashboard.md)、
  [ADR-0007](#adr-0007)、[ADR-0026](diagnostics.md#adr-0026)

### 背景

启动器主界面与运行时 Dashboard 都决定采用 Web 前端：前者由 C++ 宿主经系统 WebView
（WebView2 / WKWebView / WebKitGTK）加载，后者由 `run-apk` 既有 loopback 服务以静态路由交付。
两者需要 TypeScript 编译与打包，这引入项目此前没有的 Node 工具链；ADR-0007 只约束参与
C++ 构建的源码依赖，未覆盖 npm 注册表依赖与前端产物的归属。

### 决定

2026-09-22 目录调整：按用户决定，产品前端源码统一放在仓库根 `webui/`；生成产物仍为
`data/webui/`，不入库。此调整只修正工作区位置，构建与依赖约束不变。

1. **范围**：Node 只用于构建 `webui/` 下的前端工作区（`packages/ui-kit`、`apps/gui`、
   `apps/dashboard`），产物为纯静态文件。运行时不依赖 Node，不引入 Electron 或任何打包浏览器；
   GUI 宿主不启动本地 HTTP 服务，Dashboard 复用 ADR-0026 已有的 loopback 传输。
2. **固定版本**：Node 使用当前 LTS 主版本并在 `webui/.nvmrc` 与 `package.json`
   `engines` 中固定；依赖以 lockfile（含 integrity 哈希）提交，安装只允许 `npm ci`，禁止
   `npm install` 漂移。直接依赖限定为 vite、typescript、preact、uplot、vitest 及其类型包；
   新增依赖须在 Work Unit 中说明并更新 [Web UI 依赖许可](../../webui/README.md#依赖许可)记录。
3. **与 C++ 构建的关系**：CMake 配置期不调用 npm，默认不访问网络（与 ADR-0007 一致）。
   可选目标 `webui`（`OGPLAY_BUILD_WEBUI=ON`）显式调用 `npm ci && npm run build`；未开启时
   构建与测试消费已存在的产物，缺失时相关 GUI/Dashboard 目标明确失败，不静默降级。
4. **产物归属**：构建输出写入 `data/webui/gui/` 与 `data/webui/dashboard/`，附带 `manifest.json`
   （源码 commit、Node/npm 版本、文件 SHA-256）。与 `data/android/19/framework/` 同类，
   属本地生成制品，不纳入版本控制；发行包与 CI 由 `webui` 目标生成并按 manifest 校验。
5. **验证**：前端以 `npm run check`（`tsc --noEmit` + `vitest`）作为机器可判定门禁；
   宿主侧由 CTest 校验 manifest 存在、哈希匹配与 RPC schema 闭合。前端不承担任何
   兼容性判断逻辑，只渲染 C++ 侧结构化事实。

### 后果

- 开发者首次构建 GUI/Dashboard 需要安装固定版本 Node；纯运行时开发不受影响。
- 一份 lockfile 决定同一提交的前端依赖集合，可离线复现（`npm ci --offline` 配合缓存）。
- 前端与 C++ 的边界固定为 JSON-RPC 结构化消息；未来替换 WebView 宿主或传输不影响前端产物。
- ImGui 视图层与 `ogplay_imgui` 目标在 GUI v2 落地时删除；`third_party/imgui` submodule 随之移除。

<a id="adr-0073"></a>

## ADR-0073 · Windows 启动器 WebView 宿主边界

- 状态：Accepted
- 日期：2026-09-22
- 关联：[GUI-1](../tasks/gui/GUI-1.md)、[GUI v2](../design/gui/README.md)

### 决定

用户授权按 GUI v2 推进并暂缓 Linux。第一工作单交付 Windows；macOS WebView 宿主后续接入。
webview 0.12.0 以 Git submodule 固定源码；WebView2 1.0.1150.38 为预编译 SDK/header 包，
由显式脚本下载并校验固定 SHA-256，CMake 不使用 FetchContent 或自动下载。
系统提供 WebView2 Runtime，不打包浏览器。前端 Node 构建沿用 ADR-0072。

Windows/WebView2 API 只在 `hal/windows`，上层经 `WebViewHost` 的显式字符串请求、轮询、
脚本投递接口编排，不泄漏 native handle。GUI 只加载本地静态页面，限制导航与新窗口，
不启动 HTTP 服务。模型层继续维护身份、状态、配置与 LaunchPlan。

### 后果

ImGui 视图与依赖移除，原模型回归保留；导入/设置/删除 UI 按后续阶段恢复，第一工作单不
宣称整个 GUI v2 完成。原 `--smoke-frames` 兼容为成功 RPC 响应数，另保存真实 WebView PNG。

### GUI-3 文件选择补充（2026-09-22）

Windows 采用 IFileOpenDialog，由 HAL 的独立 STA 线程托管，通过 future 返回路径或取消。
不在 WebView RPC 回调中运行原生模态循环；模型不接触 Windows 类型。拖放通过有界分块
传入本地快照，复用相同后台分析与入库任务；没有新增依赖。现有模型只支持单体 APK，
XAPK/APKM/APKS 拆包与目录自动识别不在本工作单实现，入口明确报不支持。

<a id="adr-0137"></a>

## ADR-0137 · 共享应用库与桌面/CLI 双入口

- 状态：Accepted
- 日期：2026-10-15
- Supersedes：ADR-0071 中独立 ogplay-gui/ogplay CLI 的入口命名与同目录 CLI 启动条款。

### 决定

`ogplay` 是桌面产品入口，默认 GUI，通过自身 `--run-game` 模式启动独立游戏进程；
`ogplay-cli` 提供开发、自动化与诊断。共享 target `ogplaycore` 在 Windows 生成
`ogplaycore.dll`，包含应用实现（包括 GUI 模块）并聚合内部静态库。
两个 EXE 仅适配系统入口与 UTF-8 参数，通过两个显式 C ABI 入口调用共享库；不直接链接
内核或 SDL，不导出内部 C++ 对象，也不承诺跨版本 ABI。共享库不在加载时启动应用资源。

公共运行选项、校验和执行只有一份，GUI 只生成自身子进程的启动计划。GUI 与游戏继续
保持进程隔离、GUI 退出不杀游戏、MCP/编辑器既有边界。产品运行不依赖 CLI 文件。
Windows 完整交付；其他平台仅维护共享库和 CLI，GUI 宿主接入另行实施。不保留旧命令别名。

### 后果

EXE 与共享库必须同版本发布；内部静态库继续供单元测试使用，真实 EXE 的集成验证负责
覆盖共享库边界。SDL 只链接到共享应用实现，避免 EXE/DLL 各带一份静态状态。

<a id="adr-0138"></a>

## ADR-0138 · 共享库 C++ 外观接口与游戏库下沉

- 状态：Accepted
- 日期：2026-10-15
- Supersedes：ADR-0137 中“仅两个 C 入口”“EXE 只做系统入口适配”“GUI 在共享库内”条款。

### 决定

ogplaycore 只导出 `ogplay::app` C++ 外观，公开头位于 `include/ogplay/app/`，类型采用纯值或
Pimpl，只出现 std/app 类型。EXE/DLL 必须同次构建、同工具链；MSVC 统一 /MD（Debug /MDd），
不承诺跨版本 ABI。异常和所有权按同一 C++ 运行时传递；内核内部类不导出，移除旧 C main。
STL/Pimpl 与标准异常跨同构建边界是有意约定，外观 target 关闭 MSVC C4251/C4275 提示。

core 聚合内核、游戏运行宿主、统一运行选项校验与编解码、游戏库领域、能力/agent、外部诊断、
bundled payload 定位与日志服务。游戏库包含 APK 检视、导入分析、图标、Profile 摘要、全局/
实例设置 schema 和继承、ResolveLaunch、跨进程锁；保留原配置和存档布局。

ogplay 拥有模式分派、WebView 外壳、RPC/视图模型、导入向导与子进程/端口/令牌管理；
ogplay-cli 拥有语法、usage、错误块、stdio JSON-RPC、JSON 输出和退出码。两个 EXE 的静态
应用库只消费 core 外观；WebView 单独 target 仅由桌面链接，core/CLI 无 WebView 依赖。
`OGPLAY_BUILD_GUI=OFF` 不构建 ogplay；测试可编译可移植 GUI 模型，不新增其他平台 GUI。

SDL3 使用单份动态库，core 和桌面共同链接，随 Runtime staging/install；ANGLE/FFmpeg 的
既有交付方式不变。RunGame 自行完成 SDL bootstrap，SDL C 回调先捕获异常，再在外观返回前
重新抛出。桌面只调用 ResolveLaunch，并用 FormatGameRunArgs 格式化，禁止另写参数协议。

### 并发与进程边界

库根 `library/.lock` 使用 HAL 非阻塞 OS 排他锁（Windows LockFileEx、POSIX flock）。导入
从暂存到发布、移除及残留清理均持锁；List 只在拿到锁时清理，否则跳过 `.importing` 并正常
返回已发布列表。库内配置读写和 CLI 设置 patch 也受该锁保护；冲突明确给出 busy 和重试提示。

库启动显式携带 library-root/installation-id，在库锁内获取实例 `.running.lock`，由
RunningLease 保持到 RunGame 返回或异常，进程崩溃由 OS 释放。移除和单实例以 OS 锁为准，
GUI 进程表仅补充跟踪；裸 APK 运行不加库运行锁。锁文件不在竞争期间 unlink。

保留 ADR-0137 其余不变量：GUI 与游戏分进程，GUI 退出不杀游戏，产品不依赖 CLI 文件；
MCP/Dashboard/按键编辑器边界不变；存档位于库根 sandbox，移除条目不删存档或 external。

### 验证边界

内核单元测试继续链接内部静态库；ogplay_app_tests 实际链接共享 core 与 EXE app 库。
OS 锁/HTTP 的集成夹具独立 target，避免把内部夹具引入外观测试。导出检查只允许 app
命名空间，检查 EXE 内核导入与 CLI WebView 导入；最终链接命令另外核对静态依赖。
原生 Windows GUI 和 Linux 仍需各自在目标环境实构建/实跑；macOS 外观与 CLI 验证不替代它们。
