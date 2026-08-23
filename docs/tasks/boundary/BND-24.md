# BND-24 · PVZ OpenGL 运行闭环与最终验收

## 目标

在不引入游戏专属生产分支的前提下，让目标 APK 越过 OpenGL 阻断并完成全量回归。

## 验收

- [x] target ELF import audit 证明指定 EGL/GLES2 imports 均有 provider + handler；
- [x] bounded、关闭 survey 的 `run-apk` 越过 OpenGL import/dispatch fault；
- [x] 若未到可见帧，新的第一 fault 已证明不属于本轮指定 OpenGL 闭集；
- [x] focused、architecture、preflight、late dlopen、shared GL、fault equivalence 全过；
- [x] `cmake --preset dev`、build、全量 CTest 通过；
- [x] `MODULE.md`、`capabilities.toml`、`CURRENT.md` 同步；
- [x] 验收后确认没有持续运行的 `ogplay` 进程。

## 验收记录

- selected `armeabi/libpvz.so` SHA-256 为
  `e8863cdf19ff57a1a08bccf11ffd15c8dfc20c003c0778cfd8ba9d35b860b329`；
  dynsym 实际导入 `eglGetProcAddress` 和 `142/142` GLES2 core。EGL named
  export 与 GLES2 compile-time `index_sequence` 在 seal 时直接绑定全部导入的
  `{fn,module*}` slot；catalog/late-import 与 GLES2 concrete-handler gate 通过。
- `run-apk` 使用 API19 Bionic、exact local Profile、ephemeral sandbox、
  `--exit-after-frames 3`，关闭 survey，并有 45 s wall deadline。运行 9.3 s 内完成
  `libgnustl_shared.so` / `libnimble.so` / `libpvz.so` 加载与 JNI_OnLoad，无
  OpenGL import/dispatch fault；新的第一失败是 DexVM
  `Landroid/view/Window;->setSoftInputMode(I)V` 方法缺口，不属于本 WU。
- focused 59/59；`cmake --preset dev`、`cmake --build --preset dev` 成功；
  full CTest 933/933（architecture 5/5）。结束后 `pgrep` 确认无 `ogplay` 残留。
