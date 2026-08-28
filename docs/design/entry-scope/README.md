# 方案：Profile 驱动的启动作用域裁剪

> **启动架构已被取代：** 本文保留 entry/preset quirk 的历史设计与验收事实，但其中
> “Profile 决定启动资格、入口或 root native module”的规则已由
> [APK Startup 设计](../apk-startup/README.md)取代。当前默认入口来自 Manifest，Profile
> 仅能作为经评审的 optional compatibility override。

把"补不尽的 Java 层"变成"只执行游戏引擎必需的 Java,商业外壳按非目标裁剪"——
用**通用机制 + 每游戏数据(Title Profile)**表达,不在 `src/` 写游戏分支,不伪造成功。

面向两类读者:**决策者**(是否立项、是否需要 ADR)与**实施阶段的 AI**(作为该能力
开发的长期上下文根节点)。

## 效力声明

- 本方案不改变项目元规范:`AGENTS.md` 的全部约束(WU 有界、机器可判定测试、能力
  记账、明确失败、结构化日志、`src/` 零游戏名分支、差异只入
  `data/profiles/`)对本方案完全适用。
- 本方案是"结论级"配置的设计,不是诊断工具。它与 gap survey(诊断,默认关闭,
  永不产出兼容性结论)是两回事:survey 回答"游戏碰了什么",本方案回答"这款游戏
  的运行作用域是什么",后者要经 Scenario gate 三轮验证才成立。
- 建议按 sandbox(ADR-0020)先例,评审通过后以一条 ADR 固化"启动作用域裁剪"这一
  架构决定。评审前不启动实施。

---

## 1. 根因:为什么 Java 补不尽

老 Gameloft 标题(Asphalt 6 等)的真正游戏在 **native 引擎**(`libasphalt6.so`),
Java 层只是外壳,可清晰二分:

| 类别 | 例子 | 与游戏的关系 | 章程判定 |
| --- | --- | --- | --- |
| **引擎驱动** | `GLGame`、`GameGLSurfaceView`、`GameRenderer`、JNI 入口 | 就是"游戏本身"的启动/呈现/输入桥 | 必须实现 |
| **商业外壳** | `GameInstaller`(下载/进度 UI)、`GloftDRM`、C2DM 推送、IAB/boku 支付、`Tracking` 分析、telephony 监听 | 与玩到游戏无关 | `AGENTS.md` **非目标**(禁止 Play 服务、现代支付/社交/反作弊) |

补不尽的本质:每款游戏的商业外壳都不同且庞大,而它们大多**根本不该被实现**。
逐个补平台方法只是在给非目标逻辑续命。

### 1.1 Asphalt 6 实测启动链(反汇编事实)

```
GLGame(manifest launcher).onCreate
  └─ 读 GameInstaller.sbStarted:Z
       ├─ 为真 → 继续进入游戏引擎路径(GLSurfaceView/renderer/native)
       └─ 为假 → startActivity(.installer.GameInstaller) + finish()
GameInstaller.onCreate  ← 当前卡在这条支路
  ├─ 进度条 UI + m_portalCode="gl_shop"(商店门户)
  ├─ WiFi / TelephonyManager / ConnectivityManager(为联网下载数据)
  ├─ C2DMAndroidUtils.InstallerInit / InstallerOnCreate(推送)
  ├─ GloftDRM.<init>(许可/资源校验)   ← 现阻塞点
  └─ Tracking.init(分析)
```

`GameInstaller` 整条是"首启下载 + DRM + 推送 + 分析"外壳。游戏数据在本地已
provisioned(external `files/` 已有 980 个 `file000000.dat…`),这条下载外壳对
"用已装好的数据把游戏跑起来"是冗余的。

---

## 2. 目标与非目标

**目标**

- 提供**通用**的、profile 驱动的机制,使运行只执行游戏引擎必需的 Java,跳过商业
  外壳。机制不含任何游戏名/包名;每游戏差异只进 `data/profiles/`。
- 让"跳过"是**诚实**的:只在其前置事实真实成立时才跳(如"数据已装好"),否则明确
  失败。

**非目标**

- 不实现 store/DRM-as-store/payment/push/social(章程非目标)。
- 不在 `src/` 加游戏分支;不伪造成功、不静默返回零。
- 不做 Java 静态分析自动判定"哪些可跳"——可跳集合由人工判定并写进 profile,附
  理由,经评审。

---

## 3. 关键判断:哪些能跳,哪些不能

| 逻辑 | 能否跳过 | 依据 |
| --- | --- | --- |
| C2DM 推送、IAB/boku 支付、Tracking 分析、telephony 监听 | **能** | 章程非目标,引擎不消费其产物 |
| GameInstaller 下载/进度流程 | **能(前提:数据已 provisioned)** | 下载是 store 行为;数据本地已在则冗余 |
| **GloftDRM** | **待判定** | 见 §7:若只 gate license→能跳;若解出 asset 密钥喂 native→不能跳,须真实实现 |
| GLGame 生命周期 / GLSurfaceView / renderer / native JNI | **不能** | 这就是"游戏本身" |

**判定纪律**:一条外壳能否跳,取决于"引擎路径是否消费其产物"。消费真实数据的
逻辑(DRM 解出的密钥、安装器解出的资源)不得跳过——跳过=伪造成功。

---

## 4. 三种机制(从粗到细)

推荐以 **4.1 为主**,4.2 辅助,4.3 严格受限。

### 4.1 入口点选择:直接启动引擎 Activity(主用)

Profile 声明"启动作用域"的入口 Activity,覆盖 manifest launcher,并预置使
launcher 不再跳转到外壳的门控事实。对 A6:直接以 `GLGame` 为入口,并令
`GameInstaller.sbStarted` 表达为"已安装",使 GLGame 不再 `startActivity(GameInstaller)`。

- **诚实前提**:数据已 provisioned(统一 VFS 能解析游戏引擎要读的路径)。前提不成立
  时明确失败,不进游戏。
- DVM-16 已把 manifest launcher 作为事实读出;本机制在其上加一层 profile 覆盖。

### 4.2 静态字段预设(profile 声明)

Profile 声明一组"类初始化后写入的静态字段",表达一个**真实成立的事实**。

- 用途:`sbStarted=true`(数据已装好)、其它"已完成首启"标志。
- 诚实性红线:只能用来表达真实事实,**不得**用来跳过产出真实数据的逻辑。schema
  只接受基元/字符串值;引用类型拒绝(进人工决策)。

### 4.3 方法级中性化(profile 声明,严格受限)

Profile 声明一组非目标方法 → 中性行为(no-op / 返回基元常量)。

- 仅限 **void 或基元返回、且引擎不消费其副作用**的非目标方法(推送/分析)。
- 每条必须带 `reason`(off-target 依据),运行时日志按条标注(类似 survey 的显式
  标注),使其永远可见、可审计。
- 返回引用/产出真实数据的方法**禁止**走此路径。

### 中性化 vs 诚实失败的取舍

能被游戏自身 `try/catch` 接住的非目标失败,**优先诚实失败**(抛游戏能 catch 的
Java 异常,如 `IOException`),让游戏自己跳过——这最诚实、零配置。只有当外壳
catch 不住、又确属非目标时(如 DRM 崩在 onCreate),才用 4.1/4.2/4.3 显式裁剪。
两条路径都要记账。

---

## 5. Profile v2 schema 扩展(草案)

```toml
[runtime.entry]
# 覆盖 manifest launcher;缺省时沿用 manifest 事实。
launch_activity = "com.gameloft.android.GAND.GloftAPHP.GLGame"

# 类初始化后写入的静态字段(只接受基元/字符串;引用类型拒绝)。
[[runtime.presets]]
class  = "com.gameloft.android.GAND.GloftAPHP.installer.GameInstaller"
field  = "sbStarted"
type   = "Z"
value  = true
reason = "数据已 provisioned;GameInstaller 为 store 下载外壳(AGENTS.md 非目标)"

# 非目标方法中性化(只接受 void / 基元返回)。
[[runtime.neutralize]]
class  = "com.gameloft.android.GAND.GloftAPHP.PushNotification.C2DMAndroidUtils"
member = "InstallerInit(Landroid/app/Activity;)V"
reason = "C2DM 推送为 AGENTS.md 非目标"
```

**schema 校验(硬约束)**:`presets` 拒绝引用类型值;`neutralize` 拒绝引用返回;
每条 `presets`/`neutralize` 必须有非空 `reason`;`launch_activity` 必须是 DEX 内
存在的 Activity。任何违反在 profile 加载期明确失败。

---

## 6. 运行时集成点

| 环节 | 改动 | 复用/参照 |
| --- | --- | --- |
| profile 解析 | `loader`/profile 增 `[runtime.entry]`/`presets`/`neutralize` 字段 + 校验 | 现有 v2 解析 |
| 入口覆盖 | `DexActivityLifecycle` 用 `launch_activity` 覆盖 manifest launcher | DVM-16 launcher 事实 |
| 预设写入 | `EnsureClassInitialized` 之后按 `presets` 写 guest 静态槽 | `SetIntrinsicStaticRef` 的落槽路径,目标改为 guest 类字段 |
| 方法中性化 | 链接/解析时对声明方法装中性 handler,**非 survey**、有 profile 依据、日志逐条标注 | survey 的中性值逻辑(`NeutralValueFor`),但走结论级路径 |

所有改动保持"src 零游戏名":类名/字段/方法字面量只出现在 `data/profiles/` 的
TOML 与运行时从 profile 读入的数据里,不进 `src/`。

---

## 7. DRM 专项决策路径

DRM 是唯一不能拍脑袋的一条。`GloftDRM`/`StringEncrypter` 走
`KeyGenerator("AES")` + `SecureRandom("SHA1PRNG").setSeed(seed)` + `generateKey`——
这是安卓上常见的**确定性 KDF**(用 SHA1PRNG 从固定 seed 派生固定密钥,跨设备一致),
不是真随机,所以解出的数据是确定的、可能被真实使用。

**决策步骤**(一条调查 WU):

1. 静态跟踪 `GloftDRM.G()`/`StringEncrypter` 输出的消费点(解出的字符串写到哪个
   字段、是否流向 native/asset 加载)。
2. **分支 A**:只 gate license/store(输出不喂引擎)→ 归入 §4.1/4.2 跳过。
3. **分支 B**:解出 asset 路径/密钥喂 native → **必须真实实现** AES-128 + SHA1PRNG
   KDF,单独 WU;不得中性化(会让引擎拿到错数据)。

在调查出结论前,DRM 保持记账 + 明确失败(现状),不预设跳过。

---

## 8. 验证策略

- **Scenario gate(结论)**:A6 用裁剪 profile 三轮进入主界面,产出 Result v1;
  通过后 profile 才从 `data/profiles-dexvm/` 迁入 `data/profiles/`。
- **"关闭即失败"测试(quirk 纪律)**:去掉某条 `preset`/`neutralize` 后,运行必须
  在原阻塞点失败——证明该裁剪是**必要且被真实使用**,不是无操作的伪装。
- **诚实性测试**:数据未 provisioned 时,`entry`/`presets` 路径必须明确失败、不进
  游戏(证明"跳过"以真实前提为条件)。
- schema 负例测试:引用类型 preset、引用返回 neutralize、缺 reason、不存在的
  launch_activity 均在加载期失败。

---

## 9. WU 分解

| WU | 一句话目标 | 依赖 |
| --- | --- | --- |
| WU-1 | profile v2 `[runtime.entry]`/`presets`/`neutralize` schema + 解析 + 校验(引用拒绝、reason 必填) | — |
| WU-2 | `launch_activity` 覆盖 manifest launcher + 测试 | WU-1 |
| WU-3 | static presets 类初始化后写入 guest 静态槽 + 关闭即失败测试 | WU-1 |
| WU-4 | 方法级 neutralize(受限)+ 日志标注 + 负例测试 | WU-1 |
| WU-5 | A6 GloftDRM 消费点调查 → 决策分支 A/B | — |
| WU-6 | A6 裁剪 profile + Scenario gate 三轮 | WU-2/3/(4) + WU-5 结论 |

---

## 10. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 过度裁剪:跳过了引擎依赖的初始化,导致黑屏/崩 | "关闭即失败"测试 + 逐条加 preset,而非一次跳一大片 |
| 诚实性滑坡:preset 被用来伪造"数据已装好" | schema 强制 `reason` + 评审;presets 只允许表达真实事实;运行时校验前提(数据可解析) |
| 与 sandbox(ADR-0020)交互:provisioned 判定口径 | 统一走 VFS;数据是否就绪由 VFS 解析结果决定,不各自判断 |
| DRM 误判为可跳,实则喂引擎 | WU-5 先出消费点结论;结论前 DRM 保持明确失败 |

---

## 11. 一句话总结

> 游戏 = 引擎驱动的那几个 Java 类 + native;其余(下载/DRM/推送/支付/分析)是章程
> 非目标的商业外壳。用 profile 声明"启动作用域"(入口 Activity + 少量真实事实
> 预设 + 受限中性化),在数据已就绪的诚实前提下只跑引擎路径,把"补不尽的 Java"从
> 结构上关掉——而不是逐个给非目标逻辑续命。
