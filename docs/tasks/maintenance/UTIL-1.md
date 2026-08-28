# UTIL-1 · 基础编码与字节工具收敛

目标：把跨 core、loader、session、frontend、runtime 与 agent 的重复文本编码、
字节读取和格式化循环收敛到有明确策略参数的共享基础工具，同时保持调用域原有失败边界。

依赖：`core` 无上层依赖契约、现有 loader/JNI guest/Android Base64 行为测试。

验收：

- [x] `core` 提供 overflow-safe range、little-endian、power-of-two align、canonical
  UTF-8、策略化 UTF-16→UTF-8、Base64 与 hex 原语。
- [x] frontend/session 的 UTF-8 与 ASCII whitespace、loader 的字段读取和 binary XML
  字符串、agent/Android Base64、DexVM/数据库/liblog hex 使用共享实现。
- [x] `jni_guest` 以私有 `jni_guest_memory.h` 统一 8/16/32/64 位读取和受限 C 字符串读取，
  不把 guest ABI 语义下沉进 core。
- [x] Android Base64 的 URL-safe、padding、wrap、CRLF flags 和三种 UTF-16 孤儿代理策略
  （reject、U+FFFD、`?`）保持显式。
- [x] 扁平 launcher TOML 与结构化 Title Profile TOML 保持两个域解析器，只共享 UTF-8、
  whitespace 和 Unicode code-point 编码；不伪造两者语法等价。
- [x] `windows-msvc` 下 `ogplay` 与 `ogplay_tests` 编译通过；按用户要求不运行测试，
  运行时与单元测试留待后续。

非目标：不新增 MD5；不改 SHA-256、CRC32 或 deflate；不改变 Android framework、
Profile 或 title 能力；不把 `framework_package.cpp` 的 UTF-16 容器复制误当成 UTF-8 转码。
