# SBX-5 · DexVM File 族改线 VFS

## 目标（一句话）

废除 `memory_files`，让 DexVM 的 `java.io.File` 族经共享 VFS，并去掉
`File.mkdirs` 的伪成功。

## 依赖

- SBX-3（覆盖层）。

## 验收

- File 流经 VFS 与 native open 同路径互见。
- `File.mkdirs` 真实建目录且失败返回 false；伪成功消除属行为变更，必须有
  "回退旧行为即失败"的测试。
- Dungeon Hunter 复跑无回归。

## 交付（完成）

- `memory_files` 从 `DexVmAndroidContext` 删除；`exists`/`isDirectory`/
  `length`/`delete`/`createNewFile`/`list`、`FileInputStream`/
  `FileOutputStream`/`FileWriter` 全部改走 VFS。
- 输出流当前持有整文件内存缓冲，flush/close 时短期打开 VFS descriptor 整体
  重写；并非流对象长期持有 descriptor。DataOutputStream 包装共享/接管同一记录，
  由 SBX-11 补齐双 close 幂等。
- `File.mkdirs` 逐级 `CreateDirectory`，已存在返回 false，创建失败返回 false。
- 缺 VFS 视为宿主装配缺陷，明确失败而不是退回内存。
- 文件拆分：`dexvm_android_files.cpp` 从 `dexvm_android_io.cpp` 分出
  （后者原本会超 800 行）。

## 验证

`tests/dexvm/file_vfs_tests.cpp` 5 个用例，其中 mkdirs 用例是"关闭即失败"
形态：旧的无条件 true 会让后续 `exists`/`isDirectory` 断言全部失败。
跨会话用例经 Java `FileOutputStream` 写、下个会话经 Java `File` 读回。
macOS/arm64 CTest 641/641；Asphalt 5 exact 逐位持平；Dungeon Hunter exact
本地复跑 174 帧 clean shutdown，无回归。
