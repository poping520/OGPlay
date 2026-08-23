# BND-24 · PVZ OpenGL 运行闭环与最终验收

## 目标

在不引入游戏专属生产分支的前提下，让目标 APK 越过 OpenGL 阻断并完成全量回归。

## 验收

- [ ] target ELF import audit 证明指定 EGL/GLES2 imports 均有 provider + handler；
- [ ] bounded、关闭 survey 的 `run-apk` 越过 OpenGL import/dispatch fault；
- [ ] 若未到可见帧，新的第一 fault 已证明不属于本轮指定 OpenGL 闭集；
- [ ] focused、architecture、preflight、late dlopen、shared GL、fault equivalence 全过；
- [ ] `cmake --preset dev`、build、全量 CTest 通过；
- [ ] `MODULE.md`、`capabilities.toml`、`CURRENT.md` 同步；
- [ ] 验收后确认没有持续运行的 `ogplay` 进程。
