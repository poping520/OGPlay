# SBX-11 · Java 文件与 prefs 数据完整性

## 目标（一句话）

修正 DexVM 空目录与包装输出流语义，并让 float prefs 精确往返。

## 依赖

- SBX-5、SBX-6。

## 验收

- `File.list()` 对空目录返回长度 0 数组，仅对不存在/非目录返回 null。
- `DataOutputStream.close()` 后再次 close 底层 `FileOutputStream` 不得截断文件。
- 任意受支持 float 渲染后保持同一 IEEE-754 值。

## 交付（完成）

- File.list 在列举前保留目录存在性事实；空列表仍创建对象数组。
- DataOutputStream 构造时接管同一 output record，旧 handle 退休，双 close 幂等。
- float 使用 `to_chars`/`from_chars` 的 locale-free 最短往返表示。
- FileOutputStream/FileWriter/DataOutputStream 仍是会话内整文件缓冲，在 flush/close
  经 VFS descriptor 整体发布；文档不再声称流对象长期持有 descriptor。

## 验证

`tests/dexvm/file_vfs_tests.cpp` 增加空目录和双 close 毁档回归；
`tests/runtime/preferences_xml_tests.cpp` 逐 bit 核对 float 往返。
