# DexVM · 有界 DEX 解释器方案设计

本目录的早期实施篇（01、02、04、06、08、09、11、12、16）已退役并在篇首标明；当前实现见
[DexVM 契约](../../../src/runtime/dexvm/MODULE.md) 与 [integration 契约](../../../src/runtime/integration/MODULE.md)。

本目录是“有界 Dalvik 字节码解释器 + ROM 原样运行”架构的设计入口，
写给两类读者：**决策者**（判断投入与时机）和 **实施阶段的 AI**（作为该子系统
开发的长期上下文根节点）。架构方向已由 [ADR-0017](../../adr/dexvm.md#adr-0017)
冻结；本目录负责把该决定展开为可分片执行的工程方案。

## 效力声明

- 当前规则以 AGENTS.md、现行 ADR 与 MODULE.md 为准；已退役的阶段篇只保留设计溯源。
- 解释器、统一对象模型、JNI 与线程/Clock 的边界不因运行 ROM Java 而扩展为完整 ART/Dalvik。

## 一句话架构

> 在同一个 DexVM/JNI 对象模型中解释执行应用 DEX 与原样 API19 ROM jar；宿主实现 native
> 和进程内服务接口。ROM ActivityThread、主 Looper、ViewRootImpl 驱动应用与窗口，
> 软件 Canvas 由 Skia 绘制，Surface 经 WindowSystem 合成，GLES 使用 ANGLE。

Title Profile 只保留 identity，Manifest 决定入口与 ABI；宿主不重写应用胶水方法。
现存整类替换与非 native 覆盖仍待分阶段移除，新代码不得增加，查询见 [03](03-platform-intrinsics.md)。

实现不从零臆造：以 `.local/aosp/dalvik` 中固定 tag 的 AOSP Dalvik（KitKat，与
API 19 目标同代）为本地参考基线——目录类数据机器比对、指令语义逐 opcode 对照、结构校验
规则对标，但不移植其对象模型/GC/线程实现体（见 [07](07-aosp-reference.md)）。
这与项目既有姿态一致：CPU 用 dynarmic、GLES 用 ANGLE、PVRTC 用 PowerVR SDK，
从不凭记忆重写已有权威实现的语义。

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [01 · 目标、非目标与有界性](01-scope.md) | 解决什么、明确不做什么、规模量级 |
| [02 · 核心架构](02-architecture.md) | 模块划分、类链接、对象模型、解释器内核、异常 |
| [03 · ROM 平台边界](03-platform-intrinsics.md) | ROM 类、native 与进程内服务边界、存量查询、失败语义 |
| [04 · 运行时集成](04-integration.md) | JNI 双向、生命周期反转、线程/monitor、GC、预算 |
| [05 · 验证体系](05-verification.md) | dexasm 夹具、一致性测试、exact-title gate、能力条目 |
| [06 · 迁移与冲突裁决](06-migration.md) | 阶段划分、WU 分解、冲突裁决表、风险 |
| [07 · AOSP 参考策略](07-aosp-reference.md) | 本地参考资料、机器校验/语义参考/测试素材三模式、逐组件对照表、红线 |
| [08 · 解释执行的 EGL/GL façade](08-egl-facade.md) | 自带 GLSurfaceView 的通用形态：javax EGL 面驱动宿主 ANGLE surface |
| [09 · GC-B 精确标记清除](09-gc.md) | 04 §5 B 期的实施方案：现状盘点、执行锁即停世界、根集/标记/清扫、WU 分批 |
| [10 · 解释器 v2 threaded 内核](10-interpreter-threaded.md) | 预解码 + 线程化分派的第二内核：与旧 switch 内核并存可切换，bridge 渐进迁移，tick 逐位等价 |
| [11 · Class、Reflection 与 ClassLoader](11-class-reflection-loader.md) | API 19 class metadata、单 ClassLoader façade 与 reflection 闭环 |
| [12 · API 19 能力栈与 intrinsic 布局收敛](12-api19-capability-stack.md) | family TU 收敛、Context 类型体系、NIO、Java GLES、AudioTrack、Clock 与后续能力顺序 |
| [13 · KeyStore 通用能力开发规划](13-keystore.md) | 原版 JCA、自有 BKS Provider、真实密钥与持久化、互操作验收和 TLS 边界；DVM-172 约定范围已完成 |
| [14 · 信任验证与 TLS](14-tls-trust.md) | AOSP API、自有 Provider、默认 CA、BIO/网络边界与分阶段验收；规划完成 |
| [15 · SQLite 与真实数据库](15-sqlite.md) | BootDex Java 栈、host amalgamation、唯一 VFS、旧格式边界及 SQL-01..03 连续交付；已完成 |
| [16 · 音频审计与开发规划](16-audio.md) | 应用音频语义、BootDex 选择、OpenSL/混音/时钟/退出、CLI 职责收敛与逐批验收；历史阶段设计，当前范围见音频及 Android 契约 |

## 当前运行路径

APK 启动、配置重建、窗口与输入现状见 [CURRENT](../../state/CURRENT.md)，
P3/P4 的验收边界分别见 [P3-window](../../tasks/dexvm/P3-window.md) 和
[P4-myboy](../../tasks/dexvm/P4-myboy.md)。类集合与制品身份以启动类路径、配方及 manifest 为准。

## 术语

| 术语 | 含义                                                                               |
| --- |----------------------------------------------------------------------------------|
| dexvm | 本方案的运行时子模块名（`src/runtime/dexvm/`），有界 DEX 解释器                                     |
| intrinsic / 内建类 | 宿主 catalog 的 VM 原语/native 绑定及待移除的存量平台替换                                    |
| VM 对象 | 由解释器按 DEX 类布局分配、字段可直接读写的 Java 对象                                                 |
| 宿主背衬对象 | Java owner 关联的宿主资源状态；普通 Java 字段与协议仍归 ROM                                          |
| JavaObjectModel | session 级统一对象模型，VM 对象与宿主背衬对象共用引用身份                                               |
| 生命周期反转 | 由解释执行的真实 `onCreate`/`onDrawFrame` 驱动游戏，取代 profile `native_call` 序列               |
| L1 / L2 | roadmap 04 §7 的 DEX 分级：L1 只读解析（已完成），L2 解释执行（本方案）                                 |
| AOSP 基线 | `.local/aosp/dalvik` 中对齐 `android-4.4.4_r2` 的 `platform/dalvik` 源码树，仅作参考，不编译进运行时 |
