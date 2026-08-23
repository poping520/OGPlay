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
- [x] `AndroidModule`、`EglModule`、`Gles1Module`、`Gles2Module`、`LogModule`
  均为 concrete `final` implementation；named export metadata 直接关联 method，GLES
  直接复用 generated catalog，新增 SO 不修改中央 dispatch（中央 dispatch 已不存在）。
- [x] 删除 `InvokeModule`、module function enum、`FastBinding` 与 Virtual SO hot path 的
  `std::function`；architecture gate 持续检查 direct hot router。
- [x] A32 frame 提供 `GuestPtr<T>`、`GuestCString` 与 typed scalar decode；普通 pointer
  export 已使用强类型 guest address，variadic/custom ABI 保留 wrapper。
- [x] end-to-end benchmark 覆盖 0 参数、4 word、6 word 含 guest stack、representative
  GLES、JNI fixed slot 与 forced slow baseline，只记录数据、不设时间阈值。
- [x] 各 named export 的实现体归属 concrete module；`AndroidBoundaryHle::Impl` 不再提供
  `InvokeAndroid/InvokeEgl/InvokeGles1/InvokeGles2`，module 不再向 session facade 转发。
- [x] Android looper/input mutable state 归属 `AndroidModule`；EGL/GLES1/GLES2 module
  通过构造注入同一 session graphics services，继续共享唯一 `GuestGlContext` 和 ANGLE
  runtime。
- [x] architecture gate 同时拒绝 module-to-Impl forwarding 与 libc shared active PC。
- [x] module 不再持有整个 `AndroidBoundaryHle::Impl&`：transport、Android memory service
  与 graphics context 分别通过 `BoundaryCallServices`、`AndroidBoundaryServices`、
  `GraphicsBoundaryContext` 构造注入；共享 graphics/input state 未复制。
- [x] architecture gate 扫描全部 boundary module implementation source，并以明确 legacy
  API/token 和 direct-router 结构不变量阻止中央 forwarding、shared active PC、
  `FastBinding` 与 hot-path `std::function` 回归。

闭环测量（macOS arm64 Debug）：2,000 次 0 参数 16,325 us、4-word 16,155 us、
6-word stack 16,184 us、500 次 GLES 4,470 us、2,000 次 JNI fixed slot 15,028 us、
2,000 次 forced slow 14,230 us。数字包含 benchmark harness，只用于回归记录，不作
绝对时间或倍率门禁。
