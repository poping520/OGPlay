# BND-4 · Legacy 收口

## 目标

删除迁移期中央 route/function-id/参数表/dispatch，固化 concrete Virtual SO module 与
显式 shared service 架构。

## 交付与验收

- [x] 所有现有 boundary exports 在 seal 时绑定 concrete module hot entry。
- [x] EGL/GLES module 共用唯一 `GuestGlContext` graphics service。
- [x] 删除 `HleRoute`、fallback global id、平行参数表和 import-driven builder。
- [x] `AndroidBoundaryHle` 对外只保留 session facade API。
- [x] focused boundary/JNI/namespace tests 与 benchmark 报告。

验证遵循本次会话限制，不跑全量 exact/scenario/title gate。macOS arm64 Debug 的 1M 次
dense decode 为 9728 us，cold symbol Resolve 基线为 108946 us；只记录测量，不设倍率门禁。
