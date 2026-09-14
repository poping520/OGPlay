# Android 4.4.4 EGL/GLES 问题修复报告

日期：2026-09-14。状态：执行中；WU-1 已完成。

执行规模：默认 4 个 WU；设计、失败复现、实现和验收纳入所属 WU，不按函数或文件额外拆单。

## 1. 结论与证据边界

当前 GLES1/2 core 名称覆盖完整，但 Context、Surface、版本路由和部分固定管线行为
不完整，不能宣称完整兼容 Android 4.4.4 GLES。优先修复已发布能力的真实语义，随后
分批补齐 GLES3、Java 桥接及选定扩展；禁止以增加导出、返回成功或修改测试预期代替修复。

本报告依据当前工作区源码和本地 AOSP，未使用会话外记忆，未运行构建、测试或像素复现。
下列问题是静态代码发现；接手者须先建立失败回归再修复。未列出的接口不代表已经证明正确。

重新比对 `.local/aosp/framework/native/opengl/include/` 头文件与项目目录：

| 接口集合 | 本地 AOSP | 项目情况 |
| --- | ---: | --- |
| GLES1.1 core：`GLES/gl.h` | 145 | `data/gles/gles1.json` 名称集合一致 |
| GLES2.0 core：`GLES2/gl2.h` | 142 | `data/gles/gles2.json` 名称集合一致 |
| EGL 1.4 core：`EGL/egl.h` | 34 | `egl_exports.h` 名称覆盖全部 |
| GLES3.0 core：`GLES3/gl3.h` | 246 | 相比 ES2 新增 104 项未发布 |

以上只是名称集合，不是功能通过率、ABI 完整率或一致性认证结果。Android Bounds wrapper、
扩展和 Java overload 需单独核对。Android 4.4.4 的 ES3/扩展支持取决于设备驱动，不能把所有
厂商扩展都视为每台设备必选；两个纹理 stage 本身也不足以判定违反 GLES1 最低要求。

本轮未定位并读取用户描述的 GLES SO 压缩包。[旧设计](03-egl-gles-api19-completion.md)
记录了 ROM 导出计数和哈希，但本轮未独立复验，不把这些历史数字作为新测量结果。

## 2. 开始实施前

按 UTF-8 读取以下文件，不用历史完成状态代替代码核实：

- [当前快照](../../state/CURRENT.md)、[能力账本](../../../capabilities.toml)。
- [boundary 契约](../../../src/runtime/boundary/MODULE.md)、
  [gles 契约](../../../src/gles/MODULE.md)、
  [Java bridge 契约](../../../src/runtime/integration/dexvm_android/MODULE.md)，以及所改相邻模块契约。
- [原 EGL/GLES 设计](03-egl-gles-api19-completion.md)、
  [Java EGL 设计](../dexvm/08-egl-facade.md)、
  [BND-30](../../tasks/boundary/BND-30.md)、[BND-31](../../tasks/boundary/BND-31.md)、
  [BND-32](../../tasks/boundary/BND-32.md)、[DVM-155](../../tasks/dexvm/DVM-155.md)。
- `docs/tasks/optimization/WU-GLCTX-01.md` 至 `WU-GLCTX-05.md`：理解为何曾统一 GL 状态。
- [操作手册总览](../../playbook/README.md)，游戏排查/验收按对应手册执行。

架构冲突必须先处理：旧契约要求唯一 `GuestGlContext`、唯一 ANGLE surface/context。
修复真实多 Context 需要按 [ADR 规则](../../adr/README.md) 追加决策并同步 MODULE，明确
替代旧设计相关条款。保留“唯一权威 owner/registry”，改成每个 Context 独立状态，不能
让 Java、Native、GLES1、GLES2 各建一套互不关联的对象表。

继续使用 ANGLE 和 SDL3；不引入 Binder、system_server、完整 Android 窗口系统或手写
GLES→桌面 GL。guest 指针保持强类型，Clock 保持唯一，guest/host 线程保持 1:1。
本报告是修复交接，不是要求另写 Android 模拟器，也不直接替代正式 ADR。

## 3. 已发现问题与验收条件

下列路径均相对仓库根；行号为报告时定位，实施时以函数名重新定位。

### R1 · 高优先级：Context 隔离、share group 和线程所有权

证据：`src/runtime/boundary/modules/egl/egl_module.h` 的 `ContextState` 只保存元数据；
`egl_module.cpp:309` 的 CreateContext 只添加记录；`:333` 的 MakeCurrent 在唯一
`angle_frame` 尚不存在时才创建宿主对象。`services/graphics_boundary_context.h` 只引用
一份 `GuestGlContext` 和 `AngleFrame`。

影响：不共享的 Context 仍访问同一资源空间和状态；share_context 只是记录，未产生真实
共享组；全局 gl_owner 拒绝其他线程，即使它绑定的是不同 Context。销毁记录不等价于
销毁对应宿主资源。错误锁存、transfer state、fixed state 也必须随 Context 正确隔离。

修复：建立进程级 registry，分别拥有 display/config、Context、Surface 与 share group；
每个 Context 持有独立 guest 状态及真实 ANGLE context，共享关系传入宿主 EGL。
哪些对象可共享、哪些状态不可共享按版本规范分类，不能简单共享整个 GuestGlContext。
宿主实现可串行执行命令，但必须保留多个 guest thread 的合法 current 关系，不能用
单一全局 owner 把不同 Context 的合法使用拒绝掉。

验收：

- A/B 不共享：切换后 viewport、blend、program、GL error、GLES1 矩阵恢复各自值；
  A 创建并绑定的 texture 在未使用该名字的 B 中不成为已存在对象。
- A/B 共享：A 上传 texture/buffer 后 B 可读取/绘制，binding 等 Context 状态仍独立；
  share 创建者销毁后，其他成员按规范继续使用资源。
- 两宿主线程分别 current 不同 Context 成功；同一 Context 被另一线程占用时失败；
  失败 MakeCurrent 不破坏调用线程原有绑定，释放后另一线程可接管。
- current 对象延迟销毁、最后引用回收、线程退出、Terminate/重初始化有定向测试；
  GL error 按 Context、EGL error 按线程隔离。

### R2 · 高优先级：Surface backing 与 draw/read 分离

证据：`egl_module.cpp:361` 只首次创建 pbuffer；切换句柄未绑定不同 backing。
`:398` 的 QuerySurface 读取元数据宽高，`:414` 的 SwapBuffers 发布唯一 frame。

影响：第二个 Surface 的查询尺寸可能与实际 attachment 不同；A/B 内容无法独立；
draw/read 句柄不同虽能登记，但未实现不同渲染/读取目标。

修复：Context 与 Surface 生命周期解耦，MakeCurrent 绑定对应实际 draw/read backing；
window 和 pbuffer 区分发布行为，不能把离屏 swap 自动当成窗口 present。
frame recycling、超采样和 managed surface 必须读取实际目标，SDL presentation 仍由既有层拥有。

验收：创建不同尺寸 pbuffer A/B，分别绘制红/绿，切换后逐个 readback，验证尺寸与内容；
draw=A/read=B 时绘制和读取落到各自目标；销毁 B 不损坏 A；失败绑定保留原状态。
再覆盖 window/pbuffer 切换、超采样尺寸和 managed-surface teardown。
像素读取必须在有效/有定义的缓冲内容上断言，不依赖 swap 后未定义内容保留。

### R3 · 高优先级：缓存 proc-address 后的版本切换

证据：`egl_module.cpp:186` 按查询时 client version 返回某一库的固定 thunk；初始化后
无 current Context 直接返回 null。本地 AOSP `opengl/libs/EGL/eglApi.cpp:894`、`:922`
则解释了返回独立于查询时 Context、调用时读取 current hooks 的 forwarder。

修复：对受支持的查询入口返回稳定 guest callable，调用时按当前 Context 分派到对应
版本实现；无 current、未知名称及版本不支持入口的行为按 AOSP/规范和能力边界确定。
保留 sealed catalog、安全 guest 地址、fast/slow 一致性；不要恢复中央字符串热路径。
直接 ELF import 与动态 proc-address 的分派策略必须分别说明，不盲目改全部 SONAME 语义。

验收：仅查询一次并缓存同名 GL 函数指针，ES1→ES2→ES1 切换后调用，验证落入正确
handler（例如分别校验 GL_VERSION）；另在线程 B 的不同版本 Context 调用同一指针。
覆盖初始化前、初始化后未绑定、解绑后查询及未知名称；对合法已支持入口不得人为要求
先有 current。旧测试若断言“查询时固定 family”，必须用新行为回归替换并解释原因。

### R4 · 高优先级：GLES1 flat shading 与法线变换

证据：`gles1_dispatch.cpp:286` 的 ShadeModel 只保存，读取点在 query，draw 未消费；
`gles1_draw.cpp:548` 用 `UpperMatrix3(modelview)`；`gles1_support.h:232` 无条件 normalize。
AOSP `opengl/libagl/matrix.cpp:606` 明确使用 modelview inverse-transpose。

修复：实现 GL_FLAT/GL_SMOOTH 对实际光栅结果的影响，包括相关 primitive 的 provoking
vertex 规则；实现法线逆转置，并正确消费 GL_NORMALIZE/GL_RESCALE_NORMAL。
shader 路径、CPU 顶点准备等选型必须服从 ANGLE/GLES 契约，不只增加状态字段。

验收：同一多色三角形在 flat/smooth 下读取明确不同的内部像素；覆盖 DrawArrays 与
DrawElements 及相关 strip/fan；非均匀缩放 + 斜法线光照与独立数学参考相符；
normalize/rescale 开关有可观察差异。只检查 shader 文本或 getter 值不算通过。

### R5 · 中优先级：EGL core 参数、查询与失败行为

证据：`egl_module.cpp:398` 的 QuerySurface 只支持 WIDTH/HEIGHT；GetConfigAttrib 把
CONFIG_CAVEAT/TRANSPARENT_TYPE 与数值属性合并返回 0，需修正枚举 EGL_NONE 的事实；
ChooseConfig 仅识别部分合法属性；SwapInterval 只保存，暂无消费点。
pixmap、texture pbuffer、OpenVG client buffer 入口当前明确拒绝。

修复：逐函数建立“合法参数/合法枚举/输出/错误/生命周期”表，先补已宣告 config 和
surface 的必需查询及合法输入处理。核对 min/max swap interval、bind-to-texture 属性、
wait 无 current、Terminate、pbuffer 尺寸边界等；尚未复现的项标为待验证，不先写成定论。
不支持的可选配置应准确不宣告，不必为补数量强行实现 pixmap/OpenVG。
若实现 texture pbuffer，必须包含真实绑定、释放、生命周期与像素结果。

验收：表驱动测试合法与非法属性、EGL_NONE 与零的区别、count-only 查询、失败输出与
错误码；SwapInterval 的承诺与统一 Clock/present 行为相符。guest 内存 fault 不得被吞成
普通 EGL false；已有真实 ANGLE GL error 仍精确回送 guest。

### R6 · 后续能力：GLES3 与版本能力一致性

证据：`egl_module.cpp:326` 只接受 version 1/2；`src/gles/egl_lifecycle.cpp:272` 固定
宿主 client_version=2；当前 catalog 未发布 ES3 新增 104 项。

修复：先在 ADR 明确把旧设计排除的 ES3 纳入分期目标，再按纹理/像素传输、VAO/实例化、
query、transform feedback、UBO、sync 等分批接入真实 ANGLE。补齐 IDL 的宽值、指针、
返回对象与 A32 ABI，不允许简单透传 host 指针。版本字符串、GLSL 版本、config bits、
函数查询和真正可执行能力必须一致，不得把 ANGLE 最大版本直接当 guest 已支持版本。

验收：新增 104 项有完整机器清单和逐项状态；ES3 Context 创建、GLSL ES 3.00 编译、
各批次真实 draw/readback/query 与错误测试；ES1/2/3 切换不污染状态。不因 ES3 可用就
改变 ES2 Context 可接受的 API。未闭合前不能宣称 GLES3 完整支持。

### R7 · 后续能力：Java EGL/GLES 与扩展

证据：`src/runtime/integration/dexvm_android/android_gl.cpp:357` 明确拒绝 shared context；
`:1200` 的 pbuffer 等仍 gap；EGL14/GLES30 未接通。Native EGL 扩展字符串为空。

修复：Java EGL10 优先复用 R1/R2 的 registry，避免第二套 current/object/error 事实；
再按 API19 AOSP 的 public 签名补 EGL14/GLES30，保留数组/NIO/direct buffer 的准确搬运。
扩展按 AOSP wrapper、底层驱动可用性和 guest 直接调用需求建矩阵，EGLImage、fence sync、
external texture、presentation time 等分别立项；不能因 ANGLE 有符号就向 guest 宣告。

验收：Java/native 交叉操作同一 Context/资源，结果与错误一致；Java pbuffer/shared context
真实绘制；数组 offset、NIO position/limit、direct/heap buffer 输出正确；每个宣告扩展
至少有一次真实行为测试和失败测试。ROM 符号存在不等于设备承诺实现所有扩展行为。

## 4. 实施顺序：合并为 4 个 WU

R 编号是问题编号；以下 WU-1..4 是报告内执行编号，不占用正式 BND/DVM 编号。
接手者核查最新任务目录后映射正式编号，不再把设计、Context、share group、Surface、
路由、查询分别立单。每个 WU 内按“复核 → 失败回归 → 实现 → 定向验收”推进。

| WU | 一句话目标 | 覆盖问题 | 依赖 |
| --- | --- | --- | --- |
| WU-1 | 统一修复 Native EGL 对象生命周期、Context/Surface 隔离、共享、线程绑定和版本路由。 | R1、R2、R3、R5 | 无 |
| WU-2 | 修复 GLES1 flat shading、法线变换及 normalize/rescale 的真实绘制行为。 | R4 | WU-1 |
| WU-3 | 让 Java EGL10/EGL14 复用 Native registry 并支持已闭合的 EGL 行为。 | R7 的 Java EGL 部分 | WU-1 |
| WU-4 | 闭合 GLES3 Native/Java 调用面、版本能力声明和选定扩展。 | R6、R7 剩余部分 | WU-1、WU-2、WU-3 |

各 WU 的内部范围与出口：

- **WU-1**：基线复核、ownership ADR、契约更新与 registry 改造一起做；同时闭合
  share group、独立 Surface、draw/read、proc forwarder、查询/错误/节拍。R1/R2/R3/R5
  的定向断言通过，旧单 Context 和 managed-surface 路径仍可用，才算完成。
- **WU-2**：合并修改 shader/顶点准备与固定状态消费；R4 的像素、数学参考和
  GLES1/GLES2 状态恢复测试通过，不能只验证 getter 或 shader 文本。
- **WU-3**：EGL10 pbuffer/shared context 与 EGL14 同批复用 registry；对象 identity、
  API19 overload、数组参数、错误和 teardown 一并验收，禁止第二套图形状态。
- **WU-4**：Native ES3 的 104 项、IDL/A32 ABI、ANGLE 转发、Java GLES30 和测试放在
  同一任务内部按 API family 推进，不逐 family 新建 WU。开始时冻结选定扩展清单；
  未纳入扩展保留状态与理由，不无限扩张到全部厂商扩展，也不省略已经承诺的项目。
  R6/R7 对应逐项清单、真实执行和错误测试通过，版本与能力声明一致，才算完成。

默认不超过上述 4 个 WU，不单设准备、测试或收尾 WU。保持项目“单 WU 可在单次会话
完成”的要求：若实际复核发现超出单会话规模，只按具体阻断点作最小必要拆分并记录理由；
不预先展开成九阶段计划，不为减少编号而省略验收或把未完成事项标成完成。

WU-1..3 完成只说明本报告列出的既有语义缺口和 Java EGL 补齐已闭合，不能自动宣布
整个 GLES1/2 达到完整规范一致性；仍需完整参数/行为矩阵。WU-4 未完成时明确列出
GLES3、Java GLES30 和扩展的剩余范围。

## 5. 定向验证与交付

优先复用：

- `tests/runtime/boundary/integration/android_boundary_hle_tests.cpp`：Native EGL/GLES。
- `tests/gles/egl_lifecycle_tests.cpp`、`tests/gles/angle_frame_tests.cpp`：ANGLE 生命周期与执行。
- `tests/runtime/boundary/modules/gles1_fixed_tests.cpp`：fixed pipeline。
- `tests/dexvm/egl_facade_tests.cpp`：Java EGL，相关 Java GL 测试按实际改动选择。

Windows VS2026 使用 `windows-msvc`，只构建受影响目标，例如：

```powershell
cmake --build --preset windows-msvc --target ogplay_tests
```

实际执行路径和测试筛选先从当前构建产物/测试注册确认；只运行新回归与直接受影响的
EGL/GLES/Java 测试，不执行无筛选 CTest 或全量测试。缺少 ANGLE、跳过用例不能记为通过。
要宣称游戏回归通过，按既有 scenario 流程执行，不用 survey/手工画面替代正式证据。

每个 WU 交付：修复点与代码位置、修复前失败/修复后通过的机器断言、准确命令与结果、
剩余限制；同步 MODULE 与 CURRENT，并核对 capabilities。不要将既有 complete 状态倒退，
也不要把 catalog complete 扩大解释为语义 complete；必要时新增范围准确的能力项。
测试若曾编码旧兼容策略，修改时必须指出规范依据，不能直接删掉不通过的测试。

## 6. 可复制给新 Agent 的指令

> 请按 docs/design/boundary/04-egl-gles-repair-report.md 修复 EGL/GLES 问题。
> 不参考记忆，以当前源码、本地 AOSP 和规范为准。先复核静态发现并建立失败回归，
> 默认只用报告第 4 节的 4 个 WU：Native EGL 整体修复、GLES1 绘制、Java EGL、
> GLES3/Java GLES30/选定扩展。设计、复现、实现和验收归入各 WU，不按函数另拆任务。
> ownership ADR/模块契约在 WU-1 内更新；只有实际超出单会话规模时才作最小必要拆分。
> 不要把导出数量当功能完成，不省略剩余问题，不引入完整 Android 系统。
> 仅构建受影响目标并运行定向测试，禁止全量测试；每批同步状态、能力账本和剩余任务。
> 保留已有工作区修改。输出简洁，说明完成项、验证结果和下一项。
