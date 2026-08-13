# SBX-10 · 生命周期落盘与 prefs 错误语义

## 目标（一句话）

把 `FlushAll` 接入 pause/clean shutdown，并保证 prefs 只把真正的 ENOENT 当首次运行。

## 依赖

- SBX-3、SBX-6、SBX-7。

## 验收

- guest `onPause` 返回后刷新全部脏节点；clean teardown 在线程停止后、VFS 析构前刷新。
- 落盘失败向前端传播；EIO/EACCES 等 prefs 读取失败不得返回空表。

## 交付（完成）

- `DexActivityLifecycleBindings::flush_persistent_state` 由 `run-apk` 绑定到
  `VirtualFileSystem::FlushAll`；Suspend 在 onPause 后调用，Stop 在 Java 线程停止后
  调用并保留失败。
- `LoadPreferences` 仅吞 ENOENT；Stat/打开/读取失败转换为明确
  `PreferencesXmlError`，并清理已打开 descriptor。

## 验证

`tests/runtime/preferences_xml_tests.cpp` 用懒 backing 注入 EIO，锁定损坏配置不会被
误判为首次运行；`tests/session/profile_vfs_tests.cpp` 锁定 lifecycle flush adapter
会把仍打开 descriptor 的脏节点写入 store，生产 `run-apk` 绑定同一 adapter。
