# 01 · 现状、目标与非目标

## 1. 现状

启动一个游戏目前需要用户手工完成整条 CLI 链：

```sh
ogplay run-apk <apk> [--profiles-dir <dir>] [--external-dir <host-dir>]
```

内核侧的事实是：**启动一个游戏所需的全部校验与运行机制都已存在且受检**——
APK 精确身份匹配（`profiles.apk_exact_identity_match`）、依赖闭包、quirk
交叉验证、external 数据挂载、窗口与输入（`src/frontend/MODULE.md`）。
缺的只是一层面向非技术用户的壳：

1. 用户要自己记住 APK、数据目录的路径，每次启动敲一遍。
2. 没有"我有哪些游戏"的持久视图；没有图标、名称等可辨识信息。
3. 失败信息在终端 stderr，非技术用户看不到也读不懂。

roadmap [06 §3.1](../../roadmap/06-user-experience.md) 已确定 GUI 形态与
技术选型（SDL3 + Dear ImGui）；本方案把其中**最小可用子集**落地。

## 2. 目标

1. **游戏库网格**：一个游戏一个磁贴（图标 + 名称），来自 APK 自身的
   manifest/resources 事实；点击磁贴即启动游戏，无需任何路径输入。
2. **导入**：通过系统文件对话框选择 APK（必选）与数据包目录（可选），
   导入时完成身份解析与精确 Profile 匹配，结果如实展示；缺 Profile 或缺
   必需数据包的游戏可以入库，但磁贴明确标注不可启动及原因。
3. **删除**：从库中移除游戏（含库内 APK 副本与元数据），带确认；
   绝不删除用户引用的原地数据包目录。
4. **双击启动入口**：产品入口是可双击的 GUI 可执行 `ogplay-gui`
   （Windows 免控制台窗口、macOS 为 `OGPlay.app`），用户全程不接触
   终端；终端子命令仅服务开发与测试。
5. **基本可用即止**：Android guest 系统库由程序内置选择，设置页只暴露可选的
   Profile 目录；其余一切留给 M7。
6. **明确失败**：导入、启动、图标提取的每个失败点都有用户可读的原因与
   可执行下一步，不静默、不伪造成功（对齐 roadmap README"不要让失败静默"）。

## 3. 非目标

| 不做 | 理由 |
| --- | --- |
| 乱结构压缩包/XAPK/APKM 自动归位导入向导 | M7 任务（roadmap 06 §1.2）；基础版由用户直接指认 APK 与数据目录 |
| 输入映射引擎与屏幕叠加编辑器 | M7 任务；且叠加编辑器画在游戏 surface 里，属 `run-apk` 进程侧 |
| 兼容性分级徽章（Perfect/Playable/…） | 依赖 M8 题库回归 CI 的数据面（roadmap 06 §3.2），不手填 |
| 存档管理、即时存档、快进/慢放 | M7 任务；存档基础设施由 ADR-0020 沙盒方案提供 |
| 游戏内设置（超采样、分辨率等）的 GUI 暴露 | 基础版一律用 `run-apk` 默认值；每游戏设置属 M7 |
| 多语言 UI、主题、自动更新 | 打磨项，不阻塞基本可用 |
| GUI 拥有任何内核行为 | 架构红线（`src/frontend/MODULE.md` 禁止项）；GUI 只编排公共内核 API 与子进程 |
| 库内多版本共存（同 package 不同 versionCode） | 键为 package name（与 ADR-0020 沙盒一致）；再导入同 package 明确失败，提示先删除 |
| 从库外路径直接"免导入"启动 | 该场景 CLI 已覆盖；主面板只服务库内条目，避免两套状态 |

## 4. 规模量级

- 新增生产代码集中在 `frontend/gui/`（窗口壳、视图、模型层、
  `ogplay-gui` 双击入口）、`frontend/cli`（`gui` 子命令入口）、`loader`
  （manifest application icon/label 两个属性的增量解析），外加 CMake 的
  Windows 子系统与 macOS bundle 目标配置，预估 1200–2000 行，分 7 个 WU
  （见 [05 · §3](05-verification.md)）。
- 新增第三方依赖仅 Dear ImGui（MIT，单目录源码，按 ADR-0007 以固定提交
  submodule 引入）。文件对话框、子进程管理复用 SDL3 既有 API，PNG 解码
  复用已入库的 stb，不再引入其他依赖。
