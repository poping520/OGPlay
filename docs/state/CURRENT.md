# 当前状态

更新：2026-08-18 · APK Startup 阶段 A 已完成；APS-1..2 已交付

## 当前阶段

- **APK Startup**：APS-1 已发布 Manifest Application/launcher facts；APS-2 已建立
  APK native inventory、固定 v7a→armeabi 默认优先级和 selected-ABI 隔离视图。纯 Java
  APK 因现有 guest session 要求 native root 而明确失败，rootless shell 归 APS-3。
  frontend 尚未切换。
- **M9 DexVM**：DVM-1..41 已交付；解释执行仍由 `VmExecutionLock` 串行。子线程 native
  调用仍复用 root guest 栈/thread id，JNI/DexVM monitor 表尚未合一；`threads` 与
  `monitors` 保持 `partial`。
- **兼容性基线**：Layout UI 已验收 complete；存档沙盒、GUI、intrinsic 声明迁移与
  MSVC 工程内/工程间并行编译已交付。能力现状以 `capabilities.toml` 为准。

## 验证基线

- Windows/x64 `windows-msvc`：788/788 CTest。
- macOS/arm64 最近记录：766/766 CTest。
- Windows 预设使用原生核数并行工程；OGPlay 自有 MSVC target 启用 `/MP`。

## 下一步

1. 实施 APS-3 rootless Android guest process shell。
2. 按命中批次闭合 DexVM 缺口，随后推进 GC-B 与解释器 v2 threaded 分批。
3. 收口子线程 native guest 栈/thread id 与 JNI/DexVM monitor 统一。
4. Linux M9 严格出口复验。

## 阻塞与边界

- A6 主界面/可游玩 gate 尚未完成；现有启动证据不等同于完整可玩性。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
