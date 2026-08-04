# 当前状态

更新：2026-08-04 · M3 JNI 基线

## 当前阶段

- M0、M1、M2 均已完成；M3 已开始，当前尚无进行中的 Work Unit。
- `WU-0107` 已完成 JNI 引用表；下一个开发任务编号为 `WU-0108`。
- 本机开发只使用 Windows/MSVC 预设；Linux/macOS 使用持久目录增量验证，并在里程碑
  出口执行三平台总体验收。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0099] M2 出口闭合：Windows/MSVC、Linux/GCC、macOS/AppleClang 均在
  warnings-as-errors 下构建成功并通过 CTest 159/159。
- [WU-0100..0102] 修复三平台严格编译发现的聚合初始化与迭代器类型问题。
- [ADR-0011/WU-0103] Android guest 页固定为 4 KiB；Apple Silicon 16 KiB 宿主页上的
  相邻 guest 页仍可独立映射、保护和释放。
- [ADR-0012/WU-0104] 文档状态改为滚动快照；103 个既有 WU 按里程碑完成一次性迁移，
  完整历史由验收文档、任务单与 Git 保存；文档布局门禁及 MSVC CTest 160/160 通过。
- [WU-0105] 以 Android NDK `jni.h` 冻结 233 槽 JNINativeInterface、精确 primitive
  宽度和 32 位强类型 handle；缺失函数统一记账并 trap，不再允许静默返回零。
- [WU-0106] 保留完整 JNI ABI 目录，但不要求一次性实现全部低频函数；M3 优先闭合
  引用、异常、查找/调用、字符串、数组、RegisterNatives 与 JavaVM 线程接口。
- [WU-0107] 完成 Local/Global/WeakGlobal 引用表、线程与 local frame 隔离、容量上限、
  weak 清除，以及可同时承载 host/未来 DEX VM 对象的身份契约。

## 下一步（按优先级）

1. 创建 `WU-0108`，实现线程独立的 pending exception 状态机与调用门禁。
2. 随后实现签名解析、Modified UTF-8 和常用方法调用变体。
3. M3 出口继续使用三平台 warnings-as-errors 构建与累计契约样本验收。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
