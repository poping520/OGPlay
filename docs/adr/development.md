# 构建、依赖与开发流程

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0005 · CMake、固定依赖与 doctest](#adr-0005)
- [ADR-0007 · 第三方源码依赖使用固定 Git submodule](#adr-0007)
- [ADR-0072 · Web UI 固定构建链与静态制品交付](#adr-0072)
- [ADR-0138 · 共享 C++ 外观、游戏库与桌面交付边界](#adr-0138)

<a id="adr-0005"></a>

## ADR-0005 · CMake、固定依赖与 doctest

- 状态：Accepted
- 日期：2026-08-03

### 背景

三平台需要一致的构建入口和机器可判定的验证。

### 决定

使用 CMake 3.25+、C++20 与仓库预设；Windows 使用 windows-msvc。
测试采用 doctest 与 CTest；只构建受影响目标，检查范围按改动风险选择。
依赖固定版本并显式准备，配置期不隐式联网，也不得依赖游戏文件才能构建。
源码依赖和预编译 SDK 分别按 ADR-0007 与各依赖交付契约消费。

### 后果

同一提交的构建输入可复现；缺失依赖明确失败，文档变更仅执行静态校验。

<a id="adr-0007"></a>

## ADR-0007 · 第三方源码依赖使用固定 Git submodule

- 状态：Accepted
- 日期：2026-08-03

### 背景

配置期下载或混用系统包会使相同提交得到不同源码，妨碍离线复现与许可证审计。

### 决定

参与构建的第三方源码以 third_party 下的 Git submodule 管理，gitlink 固定提交。
含子依赖的库递归初始化，未初始化时配置明确失败；CMake 不使用 FetchContent 自动获取。
ANGLE、FFmpeg 等独立生产的 SDK 消费固定预编译 submodule，不要求消费端构建完整上游。
Web UI 的 npm 依赖按 ADR-0072 以固定 Node 与 lockfile 管理。
依赖升级审查来源、许可证与真实接口变化，并执行受影响验证。

### 后果

默认配置不访问网络；克隆需准备 submodule，二进制依赖仍须通过各自 manifest 校验。

<a id="adr-0072"></a>

## ADR-0072 · Web UI 固定构建链与静态制品交付

- 状态：Accepted
- 日期：2026-09-21

### 背景

启动器、Dashboard 和按键编辑器共用 Web 技术栈，需要明确新增构建工具与运行时依赖边界。

### 决定

前端源码在 webui，Node 只用于构建；当前 Node 24.x 由 .nvmrc 与 package.json 固定。
npm 依赖提交 lockfile 与 integrity，安装使用 npm ci；新增依赖审查用途与许可证。
CMake 默认不调用 npm、不联网，显式 webui 目标才运行安装与构建。
产物写入 data/webui，附带源码、工具版本与逐文件 SHA-256 manifest，属于本地生成制品。
运行时消费静态产物，不依赖 Node、Electron 或打包浏览器；缺失或校验失败明确报错。
启动器主页面由系统 WebView 加载本地文件；Dashboard/编辑器复用游戏进程 loopback 服务。
前端只渲染 C++ 结构化事实，RPC 边界封闭，不承载兼容性判断。

### 后果

纯内核构建无需前端工具；发行构建需生成并校验产物，前端检查沿用 npm run check。

<a id="adr-0138"></a>

## ADR-0138 · 共享 C++ 外观、游戏库与桌面交付边界

- 状态：Accepted
- 日期：2026-10-15

### 背景

桌面与 CLI 要共用运行、设置和库领域，同时隔离界面、命令行交互与内核类型。

### 决定

ogplaycore 只导出 include/ogplay/app 的 ogplay::app C++ 值/Pimpl 外观，内部类不导出。
EXE/共享库必须同次构建、同工具链；MSVC 统一 /MD（Debug /MDd），不承诺跨版本 ABI。
core 聚合内核、运行服务、日志/诊断与游戏库；库领域拥有导入、设置继承、ResolveLaunch 及锁。
ogplay 拥有桌面模式分派、WebView/RPC、向导与子进程管理；ogplay-cli 拥有语法、stdio 和退出码。
两入口只消费外观；WebView 仅桌面链接，core/CLI 不依赖它，SDL3 使用同一动态库。
RunGame 自行 bootstrap SDL；C 回调捕获异常，在 C++ 外观返回前再抛出。
桌面通过 ResolveLaunch 与统一 FormatGameRunArgs 启动自身 --run-game 子进程；GUI 退出不杀游戏，
产品运行不依赖 CLI 文件。库根与实例运行租约使用 OS 文件锁，移除和单实例判断以锁为准。
Windows 使用系统 WebView2 Runtime，macOS 使用 WKWebView；平台 SDK 类型只归 HAL 宿主。
启动器只加载本地页面并限制导航/新窗口；辅助页面仅允许指定 loopback 地址，不绑定启动器 RPC。
macOS 交付 ad-hoc 签名 OGPlay.app：EXE 在 MacOS，运行库在 Frameworks，payload 在 Resources/data；
库通过 @rpath/@loader_path 定位，不依赖 cwd。Linux GUI 未接入，不打包浏览器。

### 后果

共享库和入口必须同版本交付；系统 WebView 与原生包装需各平台验证，Developer ID/公证未交付。
