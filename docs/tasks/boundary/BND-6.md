# BND-6 · Boundary 目录拓扑

## 目标

在不改变 ABI、Virtual ELF、SVC transport、`{fn,self}` hot path 或任何 Android API
行为的前提下，把现有 standalone boundary 实现迁入 `core/services/modules/facade`，并让
每个 Virtual SO 的 export metadata 跟随其 module。

## 交付与验收

- [x] 通用 catalog/symbol/A32 call 文件归入 `core/`。
- [x] `GuestGlContext` 归入 `services/`，GLES1/GLES2 继续引用同一实例。
- [x] GLES1、GLES2、log standalone implementation 归入各自 `modules/<so>/`。
- [x] Android/EGL/log export metadata 分别归入 module 目录，不保留中央业务 metadata 表。
- [x] facade 实现归入 `facade/`；public facade header 保持原 include/ABI。
- [x] boundary tests 归入 `tests/runtime/boundary/{modules,integration}`。
- [x] CMake、include、architecture gate 和 `MODULE.md` 与新拓扑一致。
- [x] focused boundary/catalog/preflight/late-dlopen/shared-GL/fault tests 通过。
