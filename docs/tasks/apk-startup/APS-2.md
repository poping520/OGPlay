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
