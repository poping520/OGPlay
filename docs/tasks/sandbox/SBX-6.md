# SBX-6 · SharedPreferences XML 持久化

## 目标（一句话）

把 SharedPreferences 落成 Android 同构的 `shared_prefs/*.xml`，经 VFS 读写，
使其随沙盒跨会话保留，且文件视角与 API 视角是同一份事实。

## 依赖

- SBX-3（覆盖层）。

## 验收

- prefs XML 往返；与文件视角一致。
- 支持子集受检，未知元素/属性明确失败并记账，不静默丢条目。
- 跨会话读回。

## 交付（完成）

- `runtime/framework/preferences_xml.{h,cpp}`：`RenderPreferencesXml` /
  `ParsePreferencesXml` / `LoadPreferences` / `StorePreferences` /
  `PreferencesGuestPath`，framework 与 DexVM 共享同一实现。
- 格式与平台一致：标量走属性、`<string>` 走元素正文；渲染有序，因此无编辑
  的重写产生同一份字节。
- 受检子集 boolean/int/long/float/string。`float` 没有对应的 getter，但平台
  会写它，解析必须能读回真实文件而不是失败。string set、未知元素/属性、
  未知实体、DTD、畸形值一律明确失败，原文件保持不动。
- DexVM handler：`getSharedPreferences` 首次按名装载 XML，`commit()` 写回
  （落盘点）；装载失败抛 guest 可见的 `IllegalStateException` 而不是当作
  首次运行清空。

## 验证

`tests/runtime/preferences_xml_tests.cpp` 5 个用例（往返、转义、读平台原样
写出的文件、拒绝不可表达的输入、经 VFS 装载/落盘）；
`tests/dexvm/file_vfs_tests.cpp` 增加跨会话用例：会话 1 经
`getSharedPreferences`→`edit`→`putInt/putString`→`commit` 写入并直接读
XML 文件核对，会话 2 经 getter 读回。macOS/arm64 CTest 647/647。

## 未闭环

端到端"进游戏产生存档 → 退出 → 重启读到存档"仍无法演示：当前最深入的
title（Dungeon Hunter）只到标题画面，本地 200 帧运行没有触发任何
`commit()`，沙盒里只有 `meta.toml`。机制由机器测试覆盖，用户级闭环要等
title 深度推进。
