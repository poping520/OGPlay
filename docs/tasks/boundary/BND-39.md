# BND-39 · ARM exidx linker service

目标：让 guest ARM C++ 异常通过 Android linker 的 `dl_unwind_find_exidx` 找到所属 ELF 的展开表。

## 范围

- 覆盖 API 19 `libdl.so` 中仅返回空的 `dl_unwind_find_exidx` linker stub。
- 按 AOSP 4.4.4 linker 语义，以 PC 匹配 process-owned ELF load range。
- 返回经 load bias 重定位的 `PT_ARM_EXIDX` 地址，并向受检 guest 指针写入 8-byte 表项数。
- 查询覆盖初始与后续动态装载模块；不识别 title，不伪造 APK 资源。

## 验收

- [x] boundary 定向测试验证 PC、返回地址、表项数和 guest 指针写入。
- [x] Windows Release `ogplay`/`ogplay_tests` 构建，4 项 libdl 定向测试、67 assertions
  与 boundary hot-path 架构门禁通过。
- [x] Angry Birds 缺少 bundle index 时异常成功展开并越过原 SIGABRT；首错前移到独立的
  `android.location.LocationListener` 类层级缺口。

状态：已完成。
