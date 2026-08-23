# BND-4 · Legacy 收口

## 目标

删除迁移期中央 route/function-id/参数表/dispatch，固化 concrete Virtual SO module 与
显式 shared service 架构。

## 交付与验收

- [ ] 所有现有 boundary exports 绑定 module hot entry。
- [ ] EGL/GLES module 共用唯一 graphics context service。
- [ ] 删除 `HleRoute`、fallback global id、平行参数表和 import-driven builder。
- [ ] `AndroidBoundaryHle` 仅保留 facade 职责。
- [ ] focused boundary/JNI/namespace tests 与 benchmark 报告。
