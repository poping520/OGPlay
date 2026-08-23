# BND-9 · OpenSL ES public data ABI

## 目标

把 Android 4.4.4 AOSP Wilhelm 的全部 `SL_IID_*` public data export 发布到
`libOpenSLES.so` synthetic ELF，并建立与 callable thunk 相互独立的只读 guest ABI。

## 交付与验收

- [x] `BoundaryCatalog` 区分 public function、public data 与 private callable；data export
  保留显式 guest 地址/大小且不消耗 dense slot。
- [x] 按 AOSP `sl_iid.c` 发布全部 51 个 `SL_IID_*` pointer global。
- [x] 按 AOSP `OpenSLES_IID.c` 写入对应 16-byte UUID record，并将 static ABI arena 封为只读。
- [x] synthetic ELF 将 IID 声明为 `STT_OBJECT`，保留 4-byte pointer symbol size。
- [x] preflight/late load 可解析 data export，尚未进入本 WU 的三个 public function 继续
  unresolved，不伪造 handler。
- [x] focused catalog、Bionic link、ABI byte layout 与 architecture gate 通过。

## 验证

```text
ctest --preset dev --output-on-failure -R
"OpenSL|Boundary catalog|Bionic routing|Android boundary decodes|Android boundary descriptors|architecture.boundary"
```

8/8 通过。会话最终全量 CTest 按总任务要求在 OpenSL 函数与音频后端完成后执行。
