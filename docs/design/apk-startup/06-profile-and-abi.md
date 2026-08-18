# 06 · ABI 与 Title Profile 迁移

## 1. ABI 不再属于 Profile 启动决策

process ABI 是 APK/process 事实：

```text
Host/OGPlay supported guest ABIs
            ∩
ABIs actually present in APK native inventory
            ↓
      select one process ABI
```

选定后整个 `AndroidAppProcess` 固定使用该 ABI；后续 `System.load*` 不能切换。

## 2. ABI 选择器

建议独立组件：

```cpp
class ApkProcessAbiResolver {
public:
    Result<GuestAbi> Resolve(
        Span<const GuestAbi> runtime_supported,
        const ApkNativeLibraryInventory& apk);
};
```

选择策略必须确定性，并由测试固定。第一阶段可以只支持 OGPlay 当前成熟的 ARM ABI，
但“APK 同时含 armeabi/armeabi-v7a”时仍要有明确优先级，不借 Profile 决定。

纯 Java APK 若没有 native library，可保留“无 native ABI”的 process mode 或选择默认
Java-capable guest ABI；具体取决于现有 guest process 是否必须有 ARM userspace。
APS-2 必须先把当前限制测清，再冻结行为，不在设计里凭假设伪造。

## 3. Profile 新定位

目标关系：

```text
APK facts ───────────────┐
                         ├─ AndroidAppProcess config
optional TitleProfile ───┘
        only overrides/quirks
```

Profile 可以保留：

- 数据目录/mount 前提；
- surface/预算等经实测需要的兼容参数；
- static preset / non-target neutralization 等 entry-scope quirk；
- GLES/audio/legacy compatibility quirks；
- 可选的版本/哈希 applicability guard。

Profile 不再负责：

- 决定能否启动 APK；
- 选择 root `.so`；
- 指定“应预加载哪个 native library”；
- 指定 process ABI 作为唯一真源；
- 替代 Manifest launcher 的默认事实。

## 4. 兼容现有 schema v1/v2

已有 v1/v2 profile 仍可读取，避免一次性迁移所有 title：

1. 先尝试按旧 exact identity 判断“这个 profile 是否适用”；
2. 命中则把其中仍有意义的字段转成 compatibility override；
3. 不命中/不存在 profile **不再报启动失败**，而是使用空 override 继续；
4. 旧 `so_sha256`/`abi` 只参与旧 profile applicability，不参与通用 process native
   root selection。

这允许 APS-1..7 先落地，而不被 schema 迁移阻塞。

## 5. 新 profile schema：v3

为避免修改已冻结的 v1/v2 语义，新的可选 Profile 使用 `schema = 3`。
建议最小形态：

```toml
schema = 3

[identity]
package = "com.example.game"
version_code = [10]
# 可选：仅当某 quirk 必须绑定特定构建时使用，不再是必填
so_sha256 = ["..."]

[runtime]
api_level = 19

[runtime.dexvm]
heap_budget_bytes = 536870912
max_frames = 512
ticks_per_call = 200000000

[quirks]
enabled = ["..."]
```

v3 约束：

- `identity.package` 必填；
- `version_code` 是否必填由现有 profile 安全策略评审决定，但缺省必须有清晰适用范围；
- `so_sha256` 可选，仅为 applicability guard；
- 不再提供“root library”字段；
- `abi` 若为历史兼容字段，只能作为 assertion/guard，不能覆盖 resolver 选出的 ABI；
- launcher 默认来自 Manifest，只有 entry-scope 明确 quirk 才覆盖；
- `runtime.lifecycle = dex_activity` 不再决定架构路径；APK startup 是默认路径。

## 6. Profile 匹配结果类型

避免继续使用“match 或 fatal”接口。目标可表达为：

```cpp
struct CompatibilityProfileSelection {
    const TitleProfile* profile = nullptr;  // optional
    std::vector<std::string> applicability_notes;
};
```

或 `std::optional<...>`。关键语义是 **NoMatch != LaunchFailure**。

## 7. `so_sha256` 的最终语义

允许保留的场景：

- 某个 quirk 只对一版 native binary 成立；
- exact regression fixture 需要确认测试输入没有被替换；
- 数据补丁的安全 applicability guard。

禁止：

- 用它找到“主 `.so`”；
- 用它决定 `System.loadLibrary` 应返回哪个库；
- APK 无对应 hash profile 时拒绝启动。

## 8. 与 entry-scope 的关系

`launch_activity`、presets、neutralize 等仍可以是 compatibility override，但优先级是：

```text
Manifest / real Java execution
  ↓ only when a reviewed quirk is enabled
Profile override
```

不能把“为了跳商业外壳而做的特殊裁剪”重新升级成所有 APK 的通用启动协议。
