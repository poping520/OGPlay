# UTIL-2 · 同逻辑多份拷贝收敛（第二批）

目标：合并第一轮克隆普查确认的"归一化后逐字节等价"的重复功能片段，只调整代码结构，
不改变任何校验语义、错误文案、异常类别或失败边界。

依赖：UTIL-1 建立的 `core/byte_order.h` 与 `core/text.h` 共享工具；boundary
services↔modules 既有依赖方向（modules 经 `GraphicsBoundaryContext` 引用 services）。

验收：

- [x] 新增 `src/runtime/boundary/services/gles_transfer_io.h`：guest 小端 32 位字
  读/写、位模式值转换、对齐预检读、精确等尺寸写、NUL 文本写与索引最大值预检的唯一
  实现；`core/byte_order.h` 补齐对称的 `WriteLittleEndian`。
- [x] GLES1（dispatch/draw/query/fixed/completion/remaining）与 GLES2
  （shader_completion/vertex_completion/transfer）及 `graphics_dispatch.cpp` 约 20 份
  转换辅助副本改接共享原语；各 handler 原有尺寸校验语义与错误文案逐字保留。
- [x] 新增 `src/loader/dex_uleb128.h`：`dex.cpp`/`dex_class_data.cpp`/`dex_code.cpp`
  三份 ULEB128 解码器共享同一解码与校验序列，四条错误文案逐调用方原样传递。
- [x] 新增 `src/runtime/syscall/guest_path_reader.h`：`syscall.cpp` 与
  `syscall_file_metadata.cpp` 的 guest C 字符串路径读取共享实现，
  ENAMETOOLONG 行为不变。
- [x] `ValidPackage`/`ValidId` 收敛为 `core::IsValidPackageName`/
  `core::IsValidLowercaseIdentifier`（`core/text.h`），session、input、frontend
  三处调用方共享；`ogplay_input` 补充向下的 `OGPlay::Core` 链接。
- [x] `ArrayKindFor` 收敛进 `interpreter_internal.h`，直线与 threaded 两个解释器
  kernel 共享。
- [x] macOS `dev` 受影响目标 `ogplay_core`/`ogplay_loader`/`ogplay_runtime`/
  `ogplay_input`/`ogplay_session`/`ogplay_frontend`/`ogplay_tests` 编译通过；
  定向测试 loader/session/input 17、boundary GLES 41（10512 assertions）、
  syscall/dexvm/core 10（14637 assertions）、架构检查 6/6 全部通过。

非目标：不做 int/long_binop、StringBuffer/StringBuilder 等参数化克隆合并（另批处理）；
不动 hal 平台平行文件与 `gles2_module.h` 路由表；不改变任何能力声明。
