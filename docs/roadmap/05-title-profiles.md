# 05 · 去硬编码：Title Profile 与 Quirk 体系

本篇回答需求 3.2：**如何把"针对个别游戏的硬编码"变成"规范化的通用机制 + 数据"。**

---

## 1. DEMO 的硬编码现状（实测）

当前代码里共有 **4 条**游戏专属路径：**Asphalt 6**、**Dungeon Hunter**、**Asphalt 5**、
**Tales From Deep Space**。

### 1.1 游戏识别靠导出符号 if-else

```cpp
const bool dungeon_hunter = has_defined_export(
    "Java_com_gameloft_android_GAND_GloftDUNQ_DungeonHunter_DemoRenderer_nativeRender");
const bool asphalt6 = has_defined_export(
    "Java_com_gameloft_android_GAND_GloftAPHP_GameRenderer_nativeRender");
// 另有 Asphalt5Renderer_nativeGetJNIEnv、Java_fCoreJava_fAndroidHarness_main 两条探测
```

之后整个 `run_main.cpp`（3727 行）被这些布尔量分叉。

### 1.2 分叉出来的东西（规模）

| 类别 | 数量 | 例子 |
| --- | --- | --- |
| 游戏识别探针 | 4 | 导出符号字符串 + 外置数据特征文件（`InsTime` / `file000000.dat` / `intro.mp4`） |
| **独立帧循环 + 生命周期** | **4 条，约 1700 行** | A6 / DH / A5 / Tales 各一套 while 循环，线程策略、音频泵、触摸映射全不同 |
| `if (asphalt6)` 类分支 | ~20 | 遍布 `run_main.cpp` 与 `a32_cpu.cpp` |
| 二进制补丁地址 | ~25 | `0x107ab994`（材质分支）、`kLoadBias+0x00a7ab64`（locale）、Tales 的 FMV/World/Lua 全局偏移等 |
| JNI 符号字符串 | 60+ | 四套 `Java_com_gameloft_..._*` / `Java_fCoreJava_*` 手写绑定 |
| 挂载点 | 6 | `/sdcard/gameloft/games/GloftAPHP`、`/data/data/com.gameloft...GloftDUNQ...` 等 |
| 每游戏环境开关 | 15 | `TALESHLE_FIX_A6_THREADMAP`、`TALESHLE_PATCH_A6_LOCALE`、`TALESHLE_PATCH_A6_MATERIAL_BRANCH`、`TALESHLE_SKIP_A6_GAME_INIT`、`TALESHLE_A6_LEGACY_ALIAS`、`TALESHLE_SWF_VARIANT`(DH) … |
| VFS 文件/SWF 替换 | ~8 | DH 的零字节 `DQMenus*` / `DQHUD*` SWF 替换 |
| 硬编码分辨率 | 1 | DH 强制 `1024×600`（Galaxy Tab P1000） |
| 硬编码资源名 | ~10 | `res/raw/raw_000.ogg`（A6 封面音乐）、`intro.mp4`、Tales 的 `agslogo.mp4` 等 |

### 1.3 危害

- 加一个新游戏 = 再复制一条分叉，代码量线性膨胀（已经发生四次）
- 每条分叉都可能影响其他游戏（低地址读兼容最初就是全局的，后来才收窄到 A6）
- AI 无法在有限上下文里安全修改一个 3727 行、被四个布尔量交织分叉的文件

### 1.4 已经开始漏的地方

`run_main.cpp` 的 `SetStaticObjectCallback` **没有按游戏分支**，无论跑哪个游戏都固定返回
Asphalt 6 的包名 `com.gameloft.android.GAND.GloftAPHP`、A6 的存档目录和标识符
`"taleshle-a6"`。

这是硬编码模式失控的典型征兆：**分支加到一定密度后，必然会有该分而没分的地方**，
而且不会报错，只会让其他游戏拿到错误的身份信息。正式版的 profile 化正是为了从结构上
消除这类问题——身份信息只有一个来源。

---

## 2. 目标形态

```
    通用机制（代码）        ×        每游戏数据（Title Profile）
    ─────────────────                ─────────────────────────
    · 挂载点解析器          ←         mounts = [...]
    · 空指针兼容策略        ←         quirks = ["null_call_page"]
    · JNI 绑定表加载器      ←         [[java.class]] ...
    · 统一帧循环            ←         lifecycle = "gl_surface_view"
    · 输入映射引擎          ←         [[input.binding]] ...
```

**判定标准：`grep -i "asphalt\|gameloft\|dungeon" src/` 在正式版里必须为空。**
所有游戏名只允许出现在 `data/profiles/` 下的数据文件里。

---

## 3. Title Profile 结构

```toml
# data/profiles/com.gameloft.android.GAND.GloftAPHP.toml
schema = 1

[identity]
package      = "com.gameloft.android.GAND.GloftAPHP"
name         = "Asphalt 6: Adrenaline HD"
version_code = [132]                       # 适用的 versionCode 列表
so_sha256    = ["3f2a…"]                   # 精确指纹，避免误匹配
abi          = "armeabi-v7a"

[runtime]
api_level  = 19                            # 用哪个版本的 Bionic
lifecycle  = "gl_surface_view"             # 通用生命周期模板
surface    = { width = 1280, height = 720 }

[data]
# 数据来源：如何找到、挂到 guest 的哪里
mounts = [
  { guest = "/sdcard/gameloft/games/GloftAPHP", source = "external", required = true },
  { guest = "/data/data/${package}/files",      source = "external", required = true },
]
working_directory = "/sdcard/gameloft/games/GloftAPHP"
# 导入向导据此校验用户给的数据是否完整
manifest = [
  { path = "intro.mp4",     required = false },
  { path = "file000000.dat", required = true },
]

[audio]
# 取代"硬编码 raw_000.ogg 作为封面音乐"
cover_music = { source = "apk", path = "res/raw/raw_000.ogg", loop = true }

[[java.class]]                              # 游戏自有 Java 类的绑定
name = "com/gameloft/android/GAND/GloftAPHP/GLResLoader"
  [[java.class.method]]
  name = "getSoundRaw"; sig = "(I)[B"; impl = "res.sound_raw"

[quirks]
enabled = ["null_call_page", "legacy_low_address_reads"]

  [quirks.legacy_low_address_reads]
  range = ["0x01000000", "0x02000000"]

[input]
profile = "racing_tilt"                     # 引用内置模板，可再覆盖
```

要点：

1. **identity 用指纹而不是"有没有某个导出符号"。** 包名 + versionCode + `.so` 哈希三重匹配，
   匹配不上时降级到"通用 profile + 引擎指纹"，而不是猜。
2. **profile 是纯数据**，不含任何逻辑。所有字段都对应一个已经存在的通用机制。
3. **无 profile 也要能跑。** 通用默认值 + 引擎指纹推断出的模板，让没入库的游戏也有一次机会。

---

## 4. Quirk 体系

Quirk = **绕过游戏自身 bug 或历史包袱的、不适合默认开启的行为**。

### 4.1 什么不是 quirk

这条比"什么是 quirk"更重要：

- **"扩展字符串以空格结尾"不是 quirk**，是我们本来就该做对的事 → 修进通用代码
- **"上报 `GL_OES_rgb8_rgba8`"不是 quirk**，宿主确实支持 → 如实上报
- **"实现某个 libc 函数"不是 quirk** → 是缺失功能

**只有当"正确的通用行为"确实无法满足某个游戏时，才允许开 quirk。**
每次想加 quirk，先问一遍"是不是我们哪里没做对"。

### 4.2 Quirk 的强制要求

每个 quirk 必须在 `data/quirks.toml` 注册，缺任何一项 CI 直接失败：

```toml
[null_call_page]
summary = "映射 0x0–0x2000 的可执行页，让游戏对空对象的虚调用返回而不是崩溃"
reason  = """
部分老引擎在可选组件缺失时不检查空指针就发起虚调用。真机上这会崩溃，
但游戏在真机上从未走到这条路径（因为真机能力更全）。
"""
risk    = "会掩盖真实失败，必须配合 null-call 计数一起使用"
test    = "tests/titles/asphalt6_test.cpp:null_call_page_required"
owner   = "runtime/memory"
```

**`test` 字段是核心：必须有一个测试证明"去掉这个 quirk 就会坏"。**
没有测试的 quirk 会在几年后变成没人敢删的神秘代码。

### 4.3 Quirk 与可观测性绑定

像 `null_call_page` 这种"会掩盖失败"的 quirk，必须同时把被掩盖的事件暴露出来
（即 DEMO 阶段刚补的 `TALESHLE_TRACE_NULL_CALLS`，正式版进 `hle.null_calls()`）。

**规则：任何"吞掉错误"的 quirk，都必须有配套的计数与查询接口。**

---

## 5. 统一生命周期，取代每游戏一个帧循环

DEMO 在 `run_main.cpp` 里为不同游戏写了不同的 while 循环。正式版收敛为**若干模板**：

| 模板 | 适用 | 说明 |
| --- | --- | --- |
| `native_activity` | 用 NativeActivity 的游戏 | ALooper + AInputQueue + ANativeWindow |
| `gl_surface_view` | Java Activity + GLSurfaceView.Renderer | 最常见的老游戏形态 |
| `custom_jni` | 自定义 JNI 入口 | 由 profile 指定入口方法名 |

帧循环本身只有**一份实现**：

```
输入注入 → 生命周期回调 → 渲染回调 → present → 音频泵 → 调度 → 计时
```

游戏差异通过 profile 里的方法名与顺序描述，而不是复制循环体。

---

## 6. 迁移路径

从 DEMO 迁到新架构时，现有 4 个游戏作为**首批验证题**。M5 负责建立通用机制与
exact-title bring-up，M6 把验证过程自动化，完整可玩性回归在 M8 判定。覆盖面其实不错：
两代 Gameloft 引擎、一个 SWF 驱动的 UI、一个用 Lua + FMV 的 Frontier 引擎。

但要先看清进度差异，别把迁移目标定错：

| 游戏 | DEMO 现状 | 迁移目标 |
| --- | --- | --- |
| Asphalt 6 | 可进主界面、可交互 | M8 保持同等或更好，且零游戏特判 |
| Dungeon Hunter | 可进菜单 | 同上 |
| Asphalt 5 | 有独立启动路径 | 同上 |
| **Tales From Deep Space** | **尚未跑通** | **只作为 M8 通用性检验，不阻塞 M5/M6** |

**Tales From Deep Space 已经为它写了一整条专用路径（含约 15 处二进制补丁地址、
FMV 状态机、World/Lua 探针），但至今没跑通。** 这本身就是对硬编码路线最有力的反证：
堆游戏专属代码并不能换来通过率。正确的做法是先把通用运行时能力补齐
（真线程、完整 JNI、syscall 层、真实 Bionic），再回过头把它当作**通用性检验题**，
而不是继续给它加特判。

1. 先把四个游戏当前的所有硬编码**逐条列成清单**（一条不漏，约 140 项，见 §1.2）
2. 每条判定归属：
   - **通用缺陷** → 修进通用代码（如扩展串结尾空格、能力上报、身份信息单一来源）
   - **可数据化** → 进 Title Profile（挂载点、资源名、JNI 绑定、生命周期、分辨率）
   - **真 quirk** → 注册进 quirk 表 + 写证明测试
   - **临时补丁** → 删掉，走正规实现（大部分 `TALESHLE_FIX_A6_*` /
     `TALESHLE_PATCH_A6_*` 属于这类；那些二进制补丁地址多数是在绕过我们自己
     实现不全的 API，补全实现后就不需要了）
3. 迁移完成的判据：
   - `grep -ri "asphalt\|gameloft\|dungeon\|fCoreJava" src/` 为空
   - 四个游戏的题库回归全绿
   - **每个 profile 不超过 200 行 TOML**

**这个迁移本身就是对新架构通用性最好的检验。**
如果一个游戏需要写超过 200 行 profile，说明通用机制还不够，应该回头补机制而不是加字段。

### 6.1 关于二进制补丁

现有约 25 处硬编码补丁地址（材质分支、locale 引导、FMV 全局、World/Lua 探针）
**绝大多数不该迁移**。它们是 DEMO 阶段为了绕过某个未实现的 API 而直接改游戏代码的产物，
属于技术债而非兼容性需求。正式版的原则是：

> **先问"游戏为什么走到这条路径"，补全它真正需要的 API，而不是把它的代码改掉。**

只有确证是游戏自身 bug 的，才允许保留为 quirk，并且必须按 build ID / `.so` 哈希精确限定，
而不是裸写绝对地址。

---

## 7. 题库（Compatibility Database）

- profile 与兼容性状态一起进仓库，社区可 PR
- 每次 CI 跑题库回归，自动更新每个游戏的兼容性等级
- 前端游戏库直接展示（见 [06](06-user-experience.md)）
- 参考 touchHLE 的公开 app 兼容性数据库做法
