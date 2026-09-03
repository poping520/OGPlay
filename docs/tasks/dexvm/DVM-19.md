# DVM-19 · Asphalt 5 pilot：v2 profile + exact 主界面 gate

## 目标（一句话）

pilot title 删除 profile 全部 `native_call` 与 `[[java.class]]`，以 schema v2
`dex_activity` 经同一 exact Scenario 三轮通过并进入主界面（设计 01 §5
成功标准 1）。

## 结果（机器可判定，已达成）

- `data/profiles/com.gameloft.android.GAND.GloftAsphalt5.asphalt5.profile.toml`
  由 201 行 v1（16 条 native_call + 33 条 java 方法映射）替换为 25 行 v2
  （identity + surface + dexvm 预算 + quirk）——胶水声明清零。
- `asphalt5.title_flow` Scenario 三轮 + 迁移后生产目录复验全部 passed：
  固定 468 帧/468000 tick，主界面 PNG SHA-256
  `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`
  与 v1 路线逐位一致，无 guest fault，clean shutdown（证据
  `.local/dexvm/gate-01..03、gate-post-migration`，本地不入库）。
- 途中真实发生（对照 03 §7 的预言）：`<clinit>` 内 `System.loadLibrary`、
  onCreate 全副作用链（nativeCheckwifi/nativeCheckSilent 出向）、
  onSurfaceCreated 的 nativeGetJNIEnv/GLResLoader.init/GLMediaPlayer.init
  （nativeInit/nativeGetTotalSounds 出向 + SoundPool(20,3,0) 构造）、
  native 侧 GetStaticMethodID/CallStatic* 命中真实 DEX 方法表并第三路由
  解释执行 getResourceFull/loadSound 等真实方法体。
- full CTest 558/558 无回归；v1 装配路径原样保留服务其余存量 title。

## 追加（gate 后人工试玩复盘）

- 实测换语言（简体中文）与进入赛道（QUICK RACE）曾在 64 MiB 堆预算下命中
  GC-A 记账上限：`GLResLoader.getResourceFull` 的瞬态字节数组不回收，标题
  阶段累计约 66 MiB → OutOfMemoryError 置 pending → native 下一次
  `GetObjectClass` 被严格门禁挡下。修复：
  1. 预算提到 512 MiB（受检上限 1 GiB 内；实测换语言 + 进赛道峰值 <300 MiB），
     GC-B（阶段 4）落地前该预算即诚实上界；
  2. 入向桥对解释方法遗留的 pending Java 异常输出结构化 warn
     （类/消息/解释器栈）——本次定位即靠它；
  3. `AssetManager.open` 缺失条目由宿主 C++ 异常改为游戏真实可捕获的
     `java.io.IOException`。
- 人工复验：换语言/主菜单/进入赛道（SAINT TROPEZ 加载并可进游戏）均正常；
  标准 title_flow gate 与 full CTest 558/558 在 512 MiB 预算下保持通过。

## 追加（2026-09-03 InputStream 回归修复）

- core 按 API 19 恢复抽象 `InputStream` 后，Android APK 资源桥仍实例化该抽象基类，导致
  `GLResLoader.getResourceFull` 的 bulk read 无法取得 `gamecfg.bar` 内容；游戏忽略 read count，
  随后 native `CPackage` 连续触发 `GetLibSize/FSeekLibData` 断言，并以压缩文件指针 `1`
  调用 `LZMAFile::OpenAttached`，在 `0x4d9` 写入崩溃。
- 资源桥改为具体 `ByteArrayInputStream` 后，Release 原 APK 无持久化 3 帧烟测正常启动/退出；
  title-flow 跑至 468 帧、无 guest fault 且 clean shutdown。最终 Main Menu 经人工检查，并在
  两个 fresh sandbox 中稳定得到 SHA-256 `cb892db9…`；场景 golden 已由旧 `f91150b4…`
  更新为该修复后的确定性画面，通过态复验的 6 个 checkpoint 全部 passed。
