# 当前状态

更新：2026-08-04 · M2 出口完成

## 进行中

- M2 出口已闭合；当前交接到 M3 JNI 与最小框架阶段。
- 本机开发只使用 Windows/MSVC 预设；跨平台总体验收在里程碑出口执行。
- 首次 hosted CI 仍待仓库建立远端后确认，不阻塞后续开发。

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
- [WU-0053] `clock_gettime/gettimeofday` 已绑定统一 Clock 与受检 guest 内存，非法 ID、
  时间溢出和坏指针返回明确 Linux errno。
- [WU-0054] 完成匿名私有 `mmap2`、`munmap`、`mprotect` 与 `brk` 基线，确定性地址分配、
  页取整、Linux errno 和 W^X 拒绝均有契约测试。
- [WU-0055] 将 futex WAIT/WAKE 装配到 M1 真线程同步核心；PRIVATE flag、精确唤醒、
  值不匹配、坏地址和未支持超时模式均返回明确结果。
- [WU-0056] 完成大小写不敏感 Android VFS 索引与隔离文件描述符核心；路径逃逸、
  read/write/seek/close、只读和 create/truncate 错误均携带 Linux errno。
- [WU-0057] 将 `open/openat/read/write/lseek/close` 绑定 VFS；guest 路径和 buffer 有界
  复制，坏指针、未支持相对 openat 及 VFS 错误均返回明确 errno。
- [WU-0058] 完成 `DT_INIT/FINI`、init/fini arrays 与 `PT_ARM_EXIDX` 事实解析；重复、
  残缺、非对齐、歧义和非 file-backed 输入均明确失败。
- [WU-0059] 将 API 19/22/23 Bionic profile 接入统一 ELF 命名空间；libc 热点拦截与
  虚拟 HLE 边界库使用固定 thunk provider，地址可反查且缺失符号明确失败。
- [WU-0060] 完成 ELF32 `PT_TLS` 模板事实；初始化字节、BSS 大小、对齐、唯一性、
  `PT_LOAD` 覆盖和地址回绕均进入受检契约。
- macOS AppleClang `dev` 预设构建成功，全量 CTest 110/110 通过。
- [WU-0061] 完成 ELF32 GNU 符号版本事实；dynsym 的 local/global/definition/requirement、
  hidden bit、版本名、依赖库及所有链表边界均可校验。
- macOS AppleClang `dev` 预设构建成功，全量 CTest 112/112 通过。
- [WU-0062] 完成 ELF 命名空间事务式动态扩展；既有索引/主作用域保持稳定，新根拥有
  独立作用域，版本化导入按依赖库与版本名精确匹配。
- macOS AppleClang `dev` 预设构建成功，全量 CTest 115/115 通过。
- [WU-0063] 完成 `dlopen/dlsym/dlclose` 链接器状态机；稳定 handle、重复引用、共享依赖、
  handle 作用域及依赖优先 init/根优先 fini 顺序均已冻结。
- macOS AppleClang `dev` 预设构建成功，全量 CTest 117/117 通过。
- [WU-0064] guest TLS 基址已成为 CPU 状态中可快照的强类型 thread pointer；线程启动、
  一致性校验和 join 退出状态使用同一份值。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0065] 解释器与 Dynarmic 已从线程独立 CPU 状态实现 A32 TPIDRURO 读取；解释器
  同时覆盖 Thumb-2 编码，未知 CP15 操作不被放宽。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0066] 完成 API 19/22/23 旧版 Bionic 的 64-slot ARM TLS block；self、thread info、
  可选 preinit、零初始化、映射冲突与销毁均有 guest 内存契约测试。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0067] 链接器模块顺序已展开为受检 guest 生命周期调用；DT_INIT、正序 init array、
  逆序 fini array、DT_FINI、load bias、Thumb 位和 0/-1 哨兵规则均已冻结。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0068] `__ARM_NR_set_tls` 已绑定当前 guest 线程的强类型 pointer setter；跨线程、
  零线程、setter 异常与空绑定均明确失败。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0069] 建立线程安全的 guest 生命周期状态机；thread pointer、clear-child-tid、
  单线程/组退出、complete、reap 与非法状态迁移均已冻结。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0070] `set_tid_address/exit/exit_group` 已接入 guest 线程状态机；退出 syscall 只
  产生明确停止请求，不伪造宿主线程已经退出。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0071] guest 线程退出完成阶段已实现 clear-child-tid 写零和精确唤醒一个 futex
  waiter；未对齐/坏地址明确报告且不回退 exited 状态。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0072] CPU `SVC #0` 已按 r7 + r0-r6 桥接 Android ARM EABI dispatcher；PC、LR、
  thread ID、TPIDRURO 与有符号 r0 返回值均有端到端解释器契约测试。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0073] 建立预算化 Android ARM guest 线程执行循环；连续 Linux SVC、exit completion、
  累计 tick、未处理 SVC 与普通 CPU stop 均有端到端解释器测试。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0074] 完成 Linux ARM `clone` 的 pthread 形态约束及五参数 ABI 解码；parent/child
  TID、TLS 指针仅按 flags 暴露，实际创建由显式 spawner 决定且零结果不会伪装成功。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0075] guest 生命周期可在同一临界区校验 running parent、child TID 唯一性，并以
  初始 TLS 与 clear-child-tid 原子注册 child；失败不会留下部分 child 状态。
- Windows/MSVC warnings-as-errors 增量构建与相关契约测试通过。
- [WU-0076] clone committer 已串行预检 guest parent/child TID 地址，按 flags 写回 TID，
  再以 TLS/clear-child-tid 发布 child；坏地址、重复 TID 与失效 parent 不发布 child。
- Windows/MSVC warnings-as-errors 全量 CTest 138/138 通过；架构与远程增量工具门禁全绿。
- [WU-0077] SVC syscall frame 与 clone 请求已保留调用时完整 A32 状态；child 创建无需猜测
  Thumb/CPSR、返回 PC 或被调用方保存寄存器，缺失/跨线程上下文明确返回 EINVAL。
- [WU-0078] ARM clone 已从 parent 状态派生 child 的 r0=0、SP、TLS 与 TID，通过真实宿主
  线程进入统一 SVC 循环；端到端样例完成 parent 返回、child exit 与 clear-child-tid。
- [WU-0079] 新增 ELF32 多模块总装载器，一次完成事实解析、依赖排序、映射、版本化解析
  与 ARM REL 应用；任一失败通过内存快照恢复整个调用前地址空间。
- [WU-0080] 建立 API 19/22/23 真实 Bionic 自检：libc/libdl 必须完成多模块装载与重定位，
  并导出 malloc/free、pthread、文件 IO 契约符号，保留版本、生命周期及 exidx 事实。
- [WU-0082] Android ARM syscall 分组目录扩展到 120 项；新增调用仍默认 ENOSYS 并进入
  能力账本，目录规模、分组覆盖和未实现可观测性均由契约测试判定。
- [WU-0083] VFS 以同一事务挂载入口统一 APK、OBB 与外置文件，保留来源及权限事实；
  大小写不敏感索引、只读包资源、可写外置数据和整批失败回滚均有契约测试。
- [WU-0084] 新增 API 19 armeabi-v7a 无界面 NDK 累计载荷；普通 C 入口实际使用
  pthread create/join、主/子线程 malloc/free 与文件写入读回，且不依赖 JNI/Activity/GLES。
- [WU-0085/0086/0087] 三版本累计载荷已接入真实 Bionic/Dynarmic 总装；旧 provider
  版本回退与跨线程 exclusive monitor 闭合，入口返回 guest 结果、真线程及 syscall 事实。
- [WU-0088..0095] 根据 API 19/22/23 真实执行轨迹补齐 madvise/PROT_NONE、8 字节 clone、
  child TLS、线程信号状态、ARM kuser helper、映射存在性与 PR_SET_VMA 元数据。
- [WU-0096] `dladdr` 已接入与 dlopen/dlsym 相同的活跃链接命名空间和模块生命周期。
- [WU-0097/0098] memcpy/memmove/memset/memcmp/strlen 生产拦截及吞吐基准完成；pthread
  保留真实 Bionic ABI，在 clone/futex/TLS syscall 边界映射宿主线程，决定写入 ADR-0010。
- Windows/MSVC warnings-as-errors 全量 CTest 158/158；API 19/22/23 累计样本均返回 0、
  创建一个真实 child、读回 32 字节 VFS 输出且未实现 syscall 命中为零。
- [WU-0100/0101/0102] 修复 GCC/Clang 严格告警发现的 ELF 聚合初始化、VFS 迭代器偏移
  与无头请求完整初始化，行为契约保持不变。
- [ADR-0011/WU-0103] Android guest 页固定为 4 KiB，宿主后备按实际 4/16 KiB 粒度提交；
  Apple Silicon 上共享宿主页的相邻 guest 页仍能独立映射、保护和释放。
- [WU-0099] M2 出口通过：Windows/MSVC、Linux/GCC、macOS/AppleClang 均在
  warnings-as-errors 下完成构建并通过 CTest 159/159；API 19/22/23 真实 Bionic 自检与
  无界面 NDK 累计载荷在三平台全部通过。

## 下一步（按优先级）

1. 按 M3 任务单建立最小 JNI 类型、引用、异常与线程附着契约，不提前引入完整 VM。
2. 继续复用持久远端增量流程，在 M3 出口执行三平台总体验收。

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
