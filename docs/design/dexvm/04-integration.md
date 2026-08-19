# 04 · 运行时集成

> **启动规则已被取代：** 本文的 DexVM 内核、intrinsic 与生命周期验收事实继续有效；
> §1/§7 中 v1/v2 exact Profile 作为启动安全边界、Profile/native-call 驱动入口的假设，
> 已由 [APK Startup 设计](../apk-startup/README.md)取代。当前启动由 Manifest 与
> `AndroidAppProcess` 驱动，Profile 只提供 optional compatibility override。

## 1. JNI 双向互通

**解释器 → native（出向）**：`invoke-*` 解析到带 `native` 标志的方法时，经
JNI native registry 取得目标 guest 地址——两条既有解析链原样复用：
`RegisterNatives` 动态注册（存量）与 `Java_` 导出名匹配（存量，含 short/long
名与 Unicode 转义）。参数按签名编组为 A32 调用帧交给现有执行器，编组表
（与 WU-M8-003/005 已验收的 A32 soft-float word-pair 约定同一口径）：

| descriptor | tagged 值 → A32 表示 | 宽度 |
| --- | --- | --- |
| `Z` | 0/1 零扩展为 32 位 | 1 word |
| `B` / `S` | 符号扩展为 32 位 | 1 word |
| `C` | 零扩展为 32 位 | 1 word |
| `I` / `F` | 原位 32 位（soft-float，F 不走 VFP 寄存器） | 1 word |
| `J` / `D` | wide 对 → 对齐 word pair（寄存器从偶数对起，栈上 8 字节对齐） | 2 words |
| `L…;` / `[…` | 对象句柄 → 当前线程 local reference（JNI 引用表存量机制） | 1 word |

调用帧固定前缀：r0 = JNIEnv guest 指针（存量发布地址）、r1 = jclass
（static）或 receiver local ref（instance/nonvirtual），实参自 r2 起按上表
排布，溢出进 8 字节对齐栈——与既有 `CallStatic*/CallNonvirtual*` 家族的
解码互为镜像（同一编组代码，方向相反）。返回值按签名反向解码
（cat1 取 r0，J/D 取 r0:r1，对象经 local ref 表解回句柄）；返回后检查
pending exception 并转为解释器异常传播。参数检查顺序与引用表语义对照
AOSP `vm/Jni.cpp`（[07 §2](07-aosp-reference.md)）。

**native → 解释器（入向）**：guest 机器码经既有 SVC trap 进入 JNI invocation
engine。现状是两路：implementation handler 或 missing-handler 失败。本方案
增加第三路由：

```text
解析方法 → ① intrinsic handler（平台类）
          → ② 解释方法（应用类）→ 在当前宿主线程压 VM 帧执行
          → ③ 均无 → missing 失败（语义不变，携带规范 ID）
```

嵌套（解释器 → native → JNI 回调 → 解释器）是常态路径，VM 帧栈天然支持；
嵌套深度计入统一帧深度预算。`CallStaticIntMethodV` 等 233 槽 ABI 完全不变，
变的只是解析目的地。

**方法级接管（第三路由的过渡形态，阶段 2 出口）**：第三路由不依赖生命周期
反转——在 v1 profile 的 `native_call` 生命周期下即可生效。方法解析顺序为：
v1 `[[java.class]]` impl 映射（存量优先，保证已迁移 title 行为不变）→
intrinsic → 解释执行 → missing 失败。由此，迁移动作退化为**从 v1 profile 里
删除一行胶水映射**：被删除的方法自动落到解释执行，其方法体触到的平台面走
intrinsic，行为敏感的返回值（license/billing 类）来自真实字节码而不是人工
猜测。v1 schema 保持冻结（只删行不加键），粒度精确到单个方法，每删一批跑
同一 Scenario gate 持平即为验证。这是"补 Java 胶水没完没了"问题最早的
结构性止血点——不必等 profile v2 与 `dex_activity`。

**方法级接管的 intrinsic 最小集**由存量 profile 的 impl id 语料校准
（[03 §3](03-platform-intrinsics.md)）：胶水叶子方法普遍只触达
SharedPreferences（首启标志、启动计数、解锁状态的持久化）、Telephony/
Settings（设备身份）、Locale、PackageManager 受限面——远小于 onCreate
全链路所需的面。

## 2. 生命周期反转

新增 lifecycle 模板 `dex_activity`（与既有 `native_activity` / `gl_surface_view` /
`custom_jni` 并列，最终成为新 title 默认）。精确启动序列：

1. **bootstrap**（存量）：identity 三重精确匹配、payload 预检、ANGLE/SDL/
   audio 装配不变。
2. **入口发现**：二进制 Manifest 解析扩展出 application/activity 组件读取
   （`loader.apk_manifest_identity` 的增量：launcher activity 类名、屏幕
   方向、`android:name` 等已声明事实），不再靠 profile 手写。
3. **DEX 装配**：classes.dex 注册 + intrinsic 目录合并（02 §5 第 1 步）；
   bionic 闭包 ELF **预装载**（存量机制，只装载不执行 JNI_OnLoad）。
4. **实例化 Activity**：`new-instance` + `<init>` 解释执行。注意此步触发
   Activity 子类 `<clinit>`——这代游戏普遍在 `static {}` 里
   `System.loadLibrary`，所以库加载发生在**这里**而不是固定的 bootstrap
   点位。`loadLibrary` intrinsic：库名 → 预装载模块查找 → 首次命中按存量
   顺序执行 constructors → `JNI_OnLoad`（`runtime.jni_guest_library_lifecycle`
   版本校验不变）→ 未预装载的库名明确失败。`RegisterNatives` 可发生在
   OnLoad 内或其后任意 Java 执行点，native registry 存量语义覆盖。
5. **onCreate**：解释执行。全部 `nativeInit` 副作用（Asphalt 6 的 C2DM、
   Bundle、InAppBilling 初始化链）在此真实发生。
6. **视图捕获**：`new GLSurfaceView` + `setContentView` + `setRenderer(r)`
   由 intrinsic 捕获，`r`（VM 对象，实现 Renderer 接口）登记为渲染回调。
7. **onStart → onResume**：解释执行；宿主 surface 就绪后经 vtable 派发
   `onSurfaceCreated / onSurfaceChanged(w, h)`。
8. **稳态帧循环**：宿主渲染循环（存量 ANGLE pbuffer + SDL present）逐帧
   解释执行 `onDrawFrame`；SDL → mouse/touch mapper 事件派发到解释执行的
   `onTouchEvent` / `onKeyDown` override（游戏 Java 侧自行转发 native——
   这正是被解释的胶水逻辑）；Scenario step/click/lifecycle action 语义
   不变。
9. **挂起/恢复**：lifecycle action → `onPause` / `onResume` 解释调用，
   挂起期拒帧拒输入（存量语义）。
10. **teardown**：`onDestroy` 解释调用后进入存量顺序
    （interrupt → join child → guest fini → root detach → monitor shutdown）。

对比：现状 profile 用 16 条 `native_call` 声明模拟第 4–10 步；dexvm 路线里
这 16 条全部变成"被游戏自己的 Java 代码真实调用"。

## 3. 线程

- `Thread` intrinsic：`start()` 创建真实宿主线程（hal 存量），新 VM 线程态
  + JavaVM attach（存量机制，状态迁移对照 AOSP `vm/Thread.cpp`），入口
  解释执行 `run()`（含 Runnable 目标与子类 override 两种形态）；
  `join/sleep/interrupt/currentThread/isAlive` 对接宿主线程与统一 Clock；
  优先级记录不强制（记账）。
- 1 guest 线程 = 1 宿主线程模型不变；Java 线程与 native `pthread_create`
  线程在同一 JavaVM attach 语义下对等。
- 每个 Java 子线程拥有独立 A32 CPU、guest stack、Bionic TLS/thread-info、
  process thread id 与 JNI local-frame；同线程 JNI 重入继承 suspended SP/TLS，
  因而 native frame 存活期间允许 monitor/join/sleep 停泊。
- 守护线程：session teardown 复用"先中断后 join"存量顺序，不引入强杀。

## 4. monitor 与 wait/notify

- `monitor-enter/exit` 指令、`synchronized` 方法标志与 JNI MonitorEnter/Exit
  使用 session 唯一 monitor table（owner/recursion/竞争阻塞/detach 释放语义已完备）；synchronized
  方法在异常展开弹帧时自动释放（02 §8）。
- **扩展 wait-set**（推翻存量"不实现 Object.wait/notify"的边界，见 06 冲突表；
  状态机对照 AOSP `vm/Sync.cpp` 的 `waitMonitor/notifyMonitor`，薄锁/胖锁
  优化不参考）。每 object identity 增加独立于锁竞争队列的等待集，状态迁移：

| 事件 | 前置校验 | 迁移 |
| --- | --- | --- |
| `wait(t)` | 调用者是 owner，否则 IllegalMonitorStateException | 保存 recursion 深度 R → 全量释放锁 → 进 wait-set → 按统一 Clock 挂起（t=0 无限期） |
| `notify` | 调用者是 owner | wait-set 取一个（顺序不承诺）移入锁竞争队列 |
| `notifyAll` | 调用者是 owner | wait-set 全量移入锁竞争队列 |
| 超时到期 | — | 自动移入锁竞争队列 |
| 线程中断 | — | 移入锁竞争队列并置中断标记 |
| `Shutdown` | teardown 专用 | wait-set 全量唤醒并以 `shut_down` 失败 |

  被唤醒线程**必须先重新竞争并取得锁、恢复 recursion = R**，然后才返回或
  抛 InterruptedException（中断标记时；先重获锁再抛是 JLS 要求的顺序）。
  `InterruptWaiters/Shutdown` 两级中断语义（WU-M8-007）对 wait-set 同样
  适用：interrupt generation 唤醒当前 waiter 但不禁用后续 wait。

## 5. GC（分两期，诚实推进）

**A 期 · 预算 arena（先行）**：只分配不回收；对象数与字节数记账，默认预算
64 MiB（profile 可调，受检范围显式声明）；耗尽抛 OutOfMemoryError（真实
语义）并输出分配画像诊断。对 Gameloft 式薄胶水 title，A 期即可支撑全程——
诚实、机器可判定，不伪造"有 GC"。

**B 期 · 精确非移动 STW 标记清除（Java 厚层 title 前置）**。实施方案已
展开为 [09 · GC-B](09-gc.md)（含现状盘点、停世界机制修订与 WU 分批，
冲突处以 09 为准）。根枚举口径与
标记遍历对照 AOSP `vm/alloc/MarkSweep.cpp`/`Heap.cpp`（并发与分代特性
不参考）：

- **分配器**：arena 页 + size-class 空闲链，非移动；大对象独立整页。触发
  条件：分配水位越过预算比例阈值（受检参数）；`System.gc()` 只记账，
  是否触发由水位决定。
- **根集**：全部线程全部 VM 帧的 tagged ref 槽（02 §7 的 tag 在此兑现
  精确性）、类静态 ref 槽、JNI local/global 引用表、intern 表、Thread
  对象、`<clinit>` 进行中的类对象。native 持有的引用必然经 JNI 引用表，
  天然在根集内，无需扫 guest 内存。
- **停世界握手**：全局 safepoint 标志（原子）；解释线程在方法入口、回边、
  分配点轮询；进入 native 调用 / monitor 阻塞 / wait 挂起的线程在进入时
  置"safe region"标记（期间其 VM 帧不可变），返回时清标记并轮询——
  这类线程无需真正停下即视作已达安全点。机制复用存量 slice/interrupt
  基础设施。
- **标记**：根 → 灰色工作栈；对象引用槽按链接期类布局精确遍历，数组按
  元素类别，宿主背衬实例只贡献其 class 引用（宿主状态内不藏 VM 引用，
  这是 intrinsic 实现的硬约束——需要持有 VM 对象的 intrinsic 一律经
  global ref 表）。
- **清扫**：mark bitmap 重建空闲链。宿主背衬实例被清扫时调用该 intrinsic
  声明的宿主状态析构 handler（释放 voice/surface 等宿主资源）——这是
  对象模型的资源管理义务，**不是** finalizer 语义（`finalize()` 仍不
  执行，01 §3 非目标不变）。
- **弱/软/虚引用**：B 期起步一律按强引用处理并记账（偏保守，不提前
  回收）。
- **诊断**：每次 GC 输出结构化日志（存活/回收字节、对象数、暂停 tick），
  进 `dexvm.stats`。

## 6. 预算、切片与确定性

- 每条解释指令计 1 dexvm tick（payload 类指令按数据字数累计）；每次生命
  周期入口调用的 tick 预算与 `runtime.maximum_ticks_per_call` 同口径，由
  profile 声明、默认值受检。
- 长调用按存量 2000 万 tick 切片语义发布 slice observer（窗口消息泵、外部
  exit、MCP manual-step 全部照常工作）；解释器出向 native 调用的 A32 tick
  计入既有预算，两侧账目独立、汇总可查。
- 时间源只有统一 Clock（`System.currentTimeMillis/nanoTime`、`Thread.sleep`、
  `Object.wait` 超时、GC 计时全部经它），固定步长模式下解释执行完全确定，
  Scenario 三轮 gate 的可重复性与现状等价。

## 7. Title Profile v2

> 本节是 DVM 迁移时的历史契约。v2 exact identity 现仅作为 legacy applicability
> adapter；新配置使用 optional v3，详见
> [ABI 与 Title Profile 迁移](../apk-startup/06-profile-and-abi.md)。

schema v2（v1 冻结不动，二者由 `schema = 1|2` 区分，校验器同仓支持）：

| 区块 | v1 → v2 |
| --- | --- |
| identity | 不变（精确匹配是安全边界，永久保留） |
| runtime | `lifecycle` 新增 `dex_activity`；该模式下 `native_call` **不允许出现**；预算字段保留并新增 dexvm tick/堆预算 |
| data / audio | 不变（挂载、manifest、资源模式仍是每 title 事实） |
| java | `[[java.class]]` 在 `dex_activity` 模式下**不允许出现**（应用类查 DEX，平台类查 intrinsic 目录） |
| quirks | 不变，并允许 dexvm 域 quirk（如个别 title 的堆预算、帧深度覆盖），仍强制"关闭即失败"测试 |

行数上限随内容收缩重估（目标 < 100 行）；`tools/validate_title_profiles.py`
同步升级并保留 v1 全部门禁。v2 形态示例（省略号处为 v1 同款语法）：

```toml
schema = 2
[identity]
package = "com.example.game"
version_code = [10]
so_sha256 = ["…"]
abi = "armeabi-v7a"
[runtime]
api_level = 19
lifecycle = "dex_activity"
maximum_ticks_per_call = 1000000000
[runtime.dexvm]
heap_budget_bytes = 67108864     # 受检范围显式声明
max_frames = 512
ticks_per_call = 200000000
[data]
working_directory = "/sdcard/example"
mounts = [ { guest = "/sdcard/example", source = "external", required = true } ]
[quirks]
# 仍强制注册表交叉引用与"关闭即失败"测试
```

没有 `native_call`，没有 `[[java.class]]`，没有 surface 尺寸手写
（来自 Manifest 与 intrinsic 默认，quirk 可覆盖）。

## 8. 可观测性与 Agent 接口

- 新增查询面（与 `gpu.stats`/`hle.unimplemented` 同级、closed-schema）：
  `dexvm.stats`（帧数/指令数/分配水位/GC 次数）、`dexvm.stack`（指定线程
  Java 栈回溯）、`dexvm.classes`（已链接/已初始化类清单）、
  `dexvm.unimplemented`（指令与 intrinsic 缺口聚合）。
- 结构化日志新增 `runtime.dexvm.*` 域：类初始化、线程起止、GC、未捕获
  异常（含完整 Java 栈）；禁止裸输出，沿用限流与环形缓冲。
- MCP/Scenario 无需新 action：dexvm 生命周期对 runner 暴露的仍是
  step/click/lifecycle/shutdown 同一契约。
