# DVM-37 · handler 字符串 id 通道整体删除

## 目标（一句话）

DVM-35/36 迁移完成后，删除 `IntrinsicRegistry` 与字符串 handler id 的全部
残余，intrinsic 分发只剩"声明即绑定"一条通道，代码库中不再存在任何
handler id。

## 依赖

- DVM-34..36 全部完成（`src/**` 中 `Register("` 已为 0）。

## 删除清单（逐项核对）

| 项 | 位置 |
| --- | --- |
| `IntrinsicRegistry` 类与实现（含 `Freeze`、透明哈希） | `interpreter.h` / `interpreter.cpp` |
| `IntrinsicMethodDecl::handler`、`IntrinsicClassDecl::clinit_handler`（字符串） | `class_linker.h` + `RegisterIntrinsics` 中的搬运 |
| `LinkedMethod::{intrinsic_handler, resolved_handler, handler_bound}` | `class_linker.h`（DVM-32 引入的懒绑定缓存一并退场） |
| `LinkedClass::intrinsic_clinit_handler`（字符串） | `class_linker.h` |
| `InvokeIntrinsic`/`EnsureInitialized` 的 id 回退分支与懒绑定逻辑 | `interpreter.cpp` |
| `Interpreter` 构造函数的 `IntrinsicRegistry` 入参、`RegisterCoreBuiltins()` | `interpreter.h` / `interpreter.cpp` / `interpreter_internal.h` |
| `DexVmGuestBridge` 的 `platform_handlers` 入参与调用 | `dexvm_bridge.h` / `dexvm_bridge.cpp` / `run_apk.cpp` |
| `RegisterAndroidBuiltins` 及 `dexvm_android.h` 中对应公共面 | integration |
| gap survey 哨兵 `"survey.unimplemented"` | `class_linker.cpp` 494 行：合成方法改为"无 implementation"，miss 分支的 survey 判定本就走 `GapSurveyEnabled()`，删除前 grep 确认无其他消费者 |

## 测试迁移

以 registry 注册测试 handler 的用例改为 builder 声明（改注册形态，不改
断言语义）：`tests/dexvm/interpreter_tests.cpp`（4 处，含 DVM-32 的冻结/
重复注册/懒绑定用例——由等价的新通道用例替代：builder 重复方法拒绝、
声明未实现的重复 miss 记账）、`gap_survey_tests.cpp`、
`intrinsics_p1_tests.cpp`、`vm_thread_tests.cpp`、`vm_monitor_tests.cpp`、
`videoview_tests.cpp`、`widget_click_tests.cpp`、`file_vfs_tests.cpp` 各
1 处、`tests/session/profile_entry_scope_tests.cpp`（10 处）。

## 边界（不做）

- 不改任何 handler 行为与 miss/survey/记账语义（诊断键已在 DVM-34 切换为
  `<owner>.<name><descriptor>`，本 WU 只删 id 附注）。
- 不做类型安全 DSL、不动性能余项（args-shorty、字符串拷贝）。

## 验收（机器可判定）

1. 全仓 `rg 'IntrinsicRegistry|intrinsic_handler|resolved_handler|handler_bound|survey\.unimplemented'`
   在 `src/`、`include/`、`tests/` 命中为 0（docs 的历史任务书除外）。
2. 全量 CTest 全绿；gap survey 开/关对照、重复 miss 记账、dexasm 夹具
   逐位不变。
3. 收尾：`src/runtime/dexvm/MODULE.md`（公共 API 中 registry 相关条目退场、
   声明即绑定成为唯一通道写入不变量）、`src/runtime/integration/MODULE.md`、
   `CURRENT.md` 滚动更新；`capabilities.toml` 无能力状态变化。
