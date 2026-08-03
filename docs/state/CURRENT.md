# 当前状态

更新：2026-08-03 · M2 Bionic 基线

## 进行中

- M1 出口已闭合；M2 已开始，首批工作为 Bionic API 基线、ELF loader、syscall 与 VFS。
- 本机开发只使用 Windows/MSVC 预设；跨平台总体验收在里程碑出口执行。
- 首次 hosted CI 仍待仓库建立远端后确认，不阻塞 M2 开发。

## 最近完成

- 已完整读取 `docs/roadmap/`，确认正式版为重写内核、迁移知识。
- 已核对本地 `docs/demo/` 的构建与目录，确认不迁移巨型文件和游戏分叉；该目录被 Git 忽略。
- [WU-0001] 顶层规则、ADR、文档索引与交接协议。
- [WU-0002] C++20/CMake、安装目标、固定 doctest 与三平台构建入口。
- [WU-0003] 12 个正式版模块契约与单向依赖边界。
- [WU-0004] 日志环形缓冲、能力账本、Control Service 和 CLI。
- [WU-0005] 三平台 CI、配置/Profile/Quirk/Bionic/黄金帧数据骨架。
- Windows/MSVC Debug 与 Release（warnings-as-errors）编译成功；CTest 13/13 通过。
- 架构门禁通过：`src/` 无裸输出、无游戏特判、无超过 800 行的源文件。
- [WU-0008] 文档迁移路径已同步，本地 Git 仓库初始化并排除 `docs/demo/`。
- [WU-0006] 完成日志 sink、限流、帧标记、符号化 provider 与诊断包。
- [WU-0007] 完成 FixedStep Clock 与确定性空会话。
- [WU-0009] 完成 stdio JSON-RPC 2.0 Agent 闭环。
- [WU-0010] 完成无 GPU SoftwareSurface 黄金帧后端。
- [WU-0011] 完成能力账本相对 Git 基线的单调性门禁。
- [WU-0012/0013] 完成 null-call 账本及 `sym`/`hle` 主动查询。
- Windows/MSVC Release（`/WX`）CTest 27/27；Cygwin GCC Release（`-Werror`）CTest 27/27。
- [WU-0015] doctest 2.4.11、SDL 3.4.10、Dynarmic 验证提交以递归 Git submodule 固定。
- [WU-0016] CMake 移除 FetchContent，默认离线使用 submodule；CI 递归 checkout。
- [WU-0017] 完成实时/固定步长、精确倍率、暂停统一 Clock，Session 同步暂停时间源。
- [WU-0018] 建立 Windows/Linux/macOS HAL 目录契约及平台代码泄漏自动门禁。
- Windows/MSVC warnings-as-errors 与 Cygwin GCC 14.4 `-Werror` CTest 31/31。
- [WU-0019] 完成 SDL3 窗口生命周期和键盘、鼠标、手柄、退出事件 HAL 映射；
  SDL 类型不泄漏，关闭 SDL 的构建会明确失败。
- 目标平台矩阵 warnings-as-errors 构建与 CTest 34/34。
- [WU-0020] 完成强类型 `GuestAddress`、完整 4 GiB 半开区间、低地址保护与溢出契约。
- [WU-0021] 完成 Windows/Linux/macOS 虚拟内存 HAL；Windows 已验证，另外两平台待 M1 出口验收。
- [WU-0022] 完成 4 GiB guest 地址空间、页权限、映射生命周期和结构化 `MemoryFault`。
- [WU-0023] 完成确定性小端 `CheckedMemoryBus`、跨页原子验证及访问观察器。
- [WU-0024] 完成带版本和权限的事务式内存快照骨架。
- [WU-0025] 冻结 A32/T32 核心/扩展寄存器、运行结果、fault 和 CPU 快照契约。
- [WU-0026] 分离 execute 取指与 data read 权限，补齐 `Fetch16/Fetch32`。
- [WU-0027] 完成 A32/T32 条件执行、立即数算术、分支/BX 和同步陷阱解释器基础。
- [WU-0028] 完成 A32/T32 word/byte 单次 load/store、writeback 和数据 fault 原子失败。
- [ADR-0008] 修正里程碑出口依赖：M1 不要求画面，NativeActivity APK 归入 M4 累积集成出口。
- [WU-0029] 完成 M4 API 19 `armeabi-v7a` NativeActivity 载荷；离线构建器已验证
  ELF32/ARM、`android_main`、APK 内容、zipalign、debug 签名和 API/ABI 元数据。
- [WU-0030] 完成 M1 裸 A32/Thumb mailbox 样本；解释器已验证标准化输入、确定性写回、
  guest 线程号传播及 CPU+内存快照复跑。
- Windows/MSVC warnings-as-errors 全量 CTest 63/63 通过；架构门禁全绿。
- [WU-0031/0032] 以轻量 `ext-boost` 与单模块 Boost.Pool submodule 接入 Dynarmic，
  完成遵守公共内存/CPU 契约的 A32/T32 JIT 和解释器对拍。
- [WU-0033/0034/0035] 完成真实宿主线程 HAL、guest 一对一线程组及 futex WAIT/WAKE N，
  八线程与十六线程压力测试通过。
- [WU-0036] 完成 gfx/audio 纯契约和标准宿主文件系统边界，不提前实现 M4 图形音频栈。
- [WU-0037/0038/0039] 修复 GCC/Clang 严格类型、聚合初始化和 macOS W^X 差异。
- [WU-0040] M1 出口通过：Windows、Linux、macOS 均 warnings-as-errors 构建成功，
  全量 CTest 73/73；A32/Thumb 裸样本在解释器与 Dynarmic 结果一致。
- [WU-0041] M2 Bionic 支持矩阵改为 API 19/22/23；开发期 ROM 库已导入 Git 忽略的
  本地 oracle，并通过文件集、ELF 类型、体积和 SHA-256 校验。
- [WU-0042] Linux/macOS 远端流程改为持久源码、子模块和构建目录的增量构建工具；
  连接信息只在调用时注入，不写入仓库。
- [WU-0043] 完成不可信 ELF32/ARM 头、`PT_LOAD`、`PT_DYNAMIC` 事实解析；三平台严格
  增量构建与 CTest 78/78，通过后远端验证耗时降至秒级。
- [WU-0044/0045] 完成 file-backed 动态字符串表、`DT_NEEDED`、`DT_SONAME` 解析及
  跨编译器字段解码修复；三平台 warnings-as-errors CTest 80/80。
- [WU-0046] 完成 `PT_LOAD` 页计划、临时 RW 装载、BSS 清零、最终 W^X 保护和失败回滚；
  三平台 warnings-as-errors CTest 83/83。
- [WU-0047] 完成 SysV/GNU hash 边界校验、dynsym 数量推导及符号可见性事实解析；
  三平台 warnings-as-errors CTest 86/86。
- [WU-0048] 完成普通与 PLT ELF32 ARM REL 表解析；目标、符号索引、原始类型及表类别
  均保真，RELA、表重叠与越界输入明确失败；Windows/MSVC CTest 88/88。
- [WU-0049] 完成常用 ARM32 REL 原子应用；预解析符号注入、load bias、回滚和重定位期
  RW/最终 W^X 权限恢复均纳入契约测试。
- [WU-0050] 完成多层 `DT_NEEDED` 闭包、依赖优先装载、根模块广度查找作用域，以及
  local/weak/hidden/protected/absolute 符号地址解析。
- [WU-0051] 建立 Android ARM EABI syscall 分组目录与分派基线；未知及未实现调用均记账
  并返回 `-ENOSYS`，身份类调用使用一致可注入 guest 身份。
- [WU-0052] 冻结 API 19/22/23 Bionic profile，以及真实 guest 执行、选择性拦截、HLE
  边界三路符号路由；其他 API 和空符号均明确失败。

## 下一步（按优先级）

1. 按优先级实现 syscall 内存、线程、时间、文件组。
2. 将 Bionic profile 接入 ELF 链接命名空间和 HLE 边界符号 provider。
3. 以无界面 NDK `.so` 为累计样本，按依赖顺序补 pthread、文件 IO 与 malloc 闭环。

## 已知问题

- `cpu.interpreter` 仍为 partial：它是确定性参考/诊断后端，未覆盖完整 A32/Thumb-2、
  VFP/NEON、原子访问和异常模型；M1 主执行后端为 Dynarmic，缺失指令不会伪装成功。
- 最小 NDK APK 已可构建和签名，但被明确归类为 M4 载荷；当前不能宣称已在 OGPlay 中运行。
- Linux 可用 SDL Unix-console 配置执行无显示服务契约；可见桌面窗口构建仍需 X11 或
  Wayland 开发依赖。
- 黄金帧使用无 GPU SoftwareSurface，不包含 ANGLE/SwiftShader；后者属于 M4。
- Agent Control 已有 stdio JSON-RPC；TCP/UDS/MCP adapter 后续按需要补齐。
- 当前只有本地 Git 仓库，三平台 hosted CI 尚无可执行远端，因此不能宣称远端 job 已绿。
- doctest 2.4.11 在 CMake 4.x 配置期会发出上游旧 policy 的 deprecation warning，
  不影响 warnings-as-errors 编译和测试。
- 后续本地开发验证不再使用 Cygwin，只使用 Windows/MSVC 预设。
