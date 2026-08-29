# APS-2 · APK native inventory 与 process ABI

## 目标（一句话）

把 APK `lib/<abi>/*.so` 组织为独立 native inventory，并由 runtime-supported ABI 与 APK
实际 ABI 的交集确定一个 process ABI，彻底移除“由 Profile root `.so` 间接选 ABI”。

## 依赖

- APS-1（共享 APK startup facts，可并行到接口稳定后）
- `docs/design/apk-startup/06-profile-and-abi.md`

## 设计锚点

- 06 §1–2
- 08 §4

## 变更

- 引入/整理 `ApkNativeLibraryInventory`；
- 记录 ABI、APK entry、soname/logical name；
- 新增 process ABI resolver；
- 明确 multiple ABI 的确定性优先级；
- 调研纯 Java APK 在现有 guest process 的可行模式，并把真实限制写入结果；
- 不改 profile schema。

## 验收（机器可判定）

- armeabi-only / armeabi-v7a-only / dual-ABI fixture 全绿；
- 无 ABI 交集明确失败；
- selected ABI 后无法解析其它 ABI 同名库；
- resolver 不读取 `so_sha256`；
- full CTest 无回归。

## 结果（机器可判定，已达成）

- `ApkNativeLibraryInventory` 以 ABI、APK entry、entry basename soname 与可选
  `lib<logical>.so` logical name 组织 owned image；同 ABI lookup identity 重复会 typed
  failure。ELF 内部 `DT_SONAME` 尚不在 inventory 阶段解析，其与 entry basename 的
  alias/冲突语义由 APS-4 在动态装载边界裁决。
- `ResolveApkProcessAbi(runtime_supported, inventory)` 严格采用 runtime 支持列表顺序；
  项目默认列表固定为 `armeabi-v7a`、`armeabi`。resolver 只查询 ABI presence，不读取
  `so_sha256`、Profile 或 root library。
- `ApkSelectedNativeLibraries` 固定一个 process ABI，后续 soname/logical lookup 不再接受
  ABI 参数，因此无法误取其它 ABI 的同名 library。
- 纯 Java APK 调研结论：当前 `AndroidGuestCallSession` 构造检查要求非空
  `root_module`/`modules`，`BionicModuleSet` 也拒绝空 module set，故 APS-2 对空 inventory
  返回 `native_abi_required`；不得伪造默认 ABI，rootless/Java-capable mode 归 APS-3。
- Windows MSVC：`cmake --preset windows-msvc`、`cmake --build --preset windows-msvc`
  通过；APS-2 定向 8/8，完整 `ctest --preset windows-msvc` 788/788。
