# WU-PERF-16 · JIT code cache 提交与 APK 副本内存削减

目标：消除 Dynarmic code cache 被整块提交及 APK 字节多份驻留造成的内存浪费。

背景（2026-11-07，macOS arm64 Release，Angry Birds 2.3.0 运行中采样）：footprint 932 MB，
其中 6 个 guest native 线程各一个 64 MiB code cache 全部驻留（384 MB）；`game.apk`
在堆上 3 份（24.1 MB×3）：会话缓冲、`DexVmAndroidContext::apk_bytes` 深拷贝、
VFS `/data/app/<pkg>-1.apk` 的 `PutFile` 副本。

根因：Dynarmic A32 `EmitPrelude` 末尾 `mem.invalidate_all()` 对整个 code cache 执行
`sys_icache_invalidate`（Linux 为逐行 `dc cvau`/`ic ivau`），逐页触碰使 MAP_JIT 映射全部
提交。独立 64 MiB MAP_JIT 复现：仅写 2 页驻留 32 KB，加整段 invalidate 后驻留 64 MB。
WU-PERF-05 “映射惰性提交”的前提因此不成立。

改动：

- [x] `cmake/DynarmicPatches.cmake`：prelude 只刷新 `[mem.ptr(), end_of_prelude)`；
  后续块沿用 Dynarmic 自身按块刷新。替换未生效时配置失败。
- [x] VFS 新增 `PutReadOnlyFile(path, size, read_at)`：runtime 来源只读节点，字节由调用方
  持有，读取/lease/映射走 `read_at`，不复制（契约见 runtime/vfs MODULE）。
- [x] `AndroidAppProcess` 以弱引用 context 的 `read_at` 发布包 APK；`run_game` 把 APK
  缓冲移入 context，不再保留会话副本。

验证：

- `ogplay_tests` 定向：runtime/vfs_tests 51/51（含新增只读节点用例）、dynarmic_tests 29/29、
  DVM-240 包 APK 经 VFS 读取等 5 例、android_package_metadata 8/8 通过。
- Debug `ogplay-cli run-apk` Angry Birds（临时沙盒）75 秒 297 帧，SIGINT 退出：footprint
  502 MB（峰值 509 MB）；6 个 code cache 合计驻留约 9.6 MB；APK 仅 1 份 24.1 MB。
  基线为 Release 长时运行，构建类型与运行时长不同，数值仅作量级对比。

剩余缺口：

- ICU 13.5 MB 仍有 VFS 副本加 guest 内存副本；bootdex.jar 16.1 MB 与提取的 classes.dex
  8.1 MB 并存；需改请求为宿主文件 backing 或让 DexVM 借用字节。
- 约 1.8M 个小块（MALLOC_TINY/SMALL/MEDIUM 约 240 MB）未归因，需 MallocStackLogging 定位。
- 每线程独立 JIT 重复编译相同代码；Windows/Linux 未实测。
