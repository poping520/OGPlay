# VFS-07 · 有界宿主文件句柄与稳定读取

状态：实现完成，Windows/Linux 已验证；macOS 待原生验证。
决策：[ADR-0110](../../adr/runtime.md#adr-0110)。

## 目标与依赖

宿主目录文件数量不再决定同时打开的文件数量；Windows/macOS/Linux 共用 VFS 的
按需打开、预算、空闲 LRU 和 FD/租约保活，HAL 提供原生定位读取与打开对象身份。
依赖 VFS-01 的节点/打开状态及 VFS-02 的媒体租约。只修改宿主 backing，不扩展 guest API。

## 验收

- 5000 文件挂载不打开宿主句柄；小预算循环读取高水位不超过预算、不全量物化。
- 活跃 FD/dup/只读租约不可淘汰；全占用返回 EMFILE，释放后恢复。
- 回收后的身份复核、同尺寸替换、符号链接、截断、取消及并发定位读取有定向回归。
- Windows Release 受影响目标和 VFS 定向测试；同 APK/外部数据复跑记录下一首错。
- macOS/Linux 原生后端分别编译及运行同套测试；未取得实机证据时保留缺口。

## 实际证据

- Windows `windows-msvc` Release `ogplay` / `ogplay_tests` 构建通过，无关闭严格警告。
  VFS 与新增 Dashboard 字段定向 64 项/25518 断言通过；Windows 无符号链接创建权限，
  该项断言跳过。证据 `.local/vfs-host-handles-{build,dashboard-build,windows-tests}.log`。
- WSL Ubuntu 编译真实 VFS/宿主后端与原 `vfs_tests.cpp`，`ulimit -n 64` 下
  44 项/25311 断言通过，包含符号链接拒绝。复用了同一测试文件，未构建整个 Linux 项目；
  单独编译对既有缺省聚合初始化使用 `-Wno-missing-field-initializers`，其余严格警告保留。
  证据 `.local/vfs-host-handles-linux.sh`、`.local/vfs-host-handles-linux-tests.log`。
- 原 APK/3852 文件外部数据、无 Profile/无 survey、空隔离沙盒在 Windows 越过
  `cannot open host backing file` 并打开 800×480 SDL 窗口。下一首错是
  `Android app process prepare failed: intrinsic hierarchy is not registered: Landroid/hardware/SensorManager;`。
  证据 `.local/wb-windows-vfs-fix-20261009-121725/{args.json,stderr.log}`。
  进程已退出、15971 无 listener；尚未进入游戏，不宣称游戏或退出生命周期验收。
- macOS 后端已实现同一 POSIX 读取语义，本机没有 macOS 原生编译/运行环境。
