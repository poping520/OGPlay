# APS-8 · Optional Profile v3 与 legacy adapter

## 目标（一句话）

把 Title Profile 从启动 gate 降级为 optional compatibility override：保留 v1/v2 读取，
新增不要求 `so_sha256` 的 v3，并确保 no profile/no hash 都不阻断 generic startup。

## 依赖

- APS-7
- `docs/design/apk-startup/06-profile-and-abi.md`

## 设计锚点

- 06 §3–8
- 08 §7

## 变更

- profile selection API 从 match-or-fail 改为 optional selection；
- v1/v2 exact identity 作为 legacy applicability adapter；
- 新增 schema v3；
- v3 `so_sha256` 可选；
- v3 不存在 root library 语义，ABI 不可覆盖 process resolver；
- 现有 data/surface/dexvm budget/quirk/entry-scope override 迁入统一 compatibility config；
- validator 增 v3 正负例。

## 验收（机器可判定）

- no profile → generic startup 继续；
- old v2 exact match → override 可应用；
- old v2 hash mismatch → profile 不应用但 generic startup 不失败；
- v3 无 `so_sha256` 可通过 validator；
- v3 尝试声明 root `.so`/强制 ABI 被 schema 拒绝；
- profile fixtures/full CTest 无回归。

## 结果

- `SelectApkCompatibilityProfile` 把 no profile、纯 Java与旧 hash mismatch 统一为成功的
  generic selection；v1/v2 命中时只提供 compatibility override，旧 exact API 留作兼容。
- C++ loader 可读取 v1/v2/v3：v1 历史 lifecycle/native-call/Java 声明只做数据迁移并
  归一到 generic DexVM Activity，不恢复 replay；v3 的 version/hash guard 可省略。
- v3 JSON Schema 与 Python validator 同步禁止 `abi`、`lifecycle`、root library，并覆盖
  无 hash 正例和强制 ABI/root library 负例。
- CLI 与 GUI 导入均改用 optional selection；process ABI 仍只来自 APK inventory resolver。
