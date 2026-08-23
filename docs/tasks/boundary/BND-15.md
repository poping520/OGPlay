# BND-15 · Boundary 目录与 OpenSL ES 最终审计

## 目标

以当前源码、门禁和全量测试逐项证明 boundary ownership 重构与 OpenSL ES Virtual SO 两个
总目标均已闭环，并修正目录搬迁后遗留的证据路径。

## 目录与架构证据

- [x] `src/runtime/boundary/` 根目录只保留 `MODULE.md`，实现分别归入
  `core/services/modules/facade`；各 Virtual SO metadata/handler/state 跟随 module 目录。
- [x] CMake source list 和测试 source list 全部使用新路径；capability ledger 中旧的
  `android_boundary_hle_tests.cpp`、`android_boundary_gles1_fixed_tests.cpp` 路径已同步。
- [x] architecture gate 递归扫描全部 core/service/module/facade 源，并机器禁止
  `InvokeModule/InvokeAndroid/InvokeEgl/InvokeGles*`、`FastBinding`、shared active PC、
  facade forwarding、hot-path `std::function` 与逆向 include 依赖。
- [x] `TryFastCall()` 仍只执行 dense slot → `{fn,self}`，不读取 library/symbol/local-id，
  不调用 `GetState/SetState/HaltExecution`。
- [x] JNI 仍位于 `runtime/jni_guest`，libc override 仍位于 `runtime/bionic`；shared GL、late
  import、boundary/JNI fault equivalence 与 libc override 并发测试均包含于 full gate。

## OpenSL ES 证据

- [x] AOSP Wilhelm public ELF surface、51 IID data、private vtable ABI、PCM object chain、
  mixer、process additive pump 和专用 callback context 均已有直接测试。
- [x] BND-14 新增的真实 A32 callback 读取专用 TPIDRURO，经 SVC #2 re-enqueue 第二个 PCM
  buffer；callback thread 不进入 JNI attached-thread 集合。
- [x] mute 只影响 gain，不再暂停 position、queue consumption 或 callback。
- [x] focused OpenSL/ownership gate 20/20 通过。

## 最终验证

```text
cmake --build --preset dev -j 8
ctest --preset dev --output-on-failure
100% tests passed, 0 tests failed out of 924
```

`runtime.virtual_so_boundary` 与 `runtime.opensles_virtual_so` 均有充分依据保持 `complete`。
