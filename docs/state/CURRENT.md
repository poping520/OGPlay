# 当前状态

更新：2026-08-04 · M3 JNI 基线

## 当前阶段

- M0、M1、M2 均已完成；M3 已开始，当前尚无进行中的 Work Unit。
- `WU-0136` 已闭合偏好服务 Context receiver 的受检解析；下一个开发任务编号为
  `WU-0137`。
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
- [WU-0105..0110] 冻结完整 JNI ABI，完成引用/异常底座、签名解析和 Modified UTF-8。
- [WU-0111..0118] 完成环境、字符串、primitive array、native 注册、JavaVM、类/调用引擎，
  并将首批 169 个 JNIEnv 与 4 个 JavaVM 行为映射到稳定 thunk。
- [WU-0119..0122] 完成 Activity 生命周期、对象数组、字段存储与无图形双向 JNI 契约。
- [WU-0123..0127] 完成 DEX L1 受检解析及类/方法/native/指令/渲染规模、Java 厚度与
  声明式引擎指纹报告；不执行字节码。
- [WU-0128] 将对象数组和全部实例/静态字段访问槽接入公共目录，累计映射 208 个
  JNIEnv 行为槽；反射、direct buffer 等低频槽继续可观测 trap。
- [WU-0129] 建立 Object→Context→ContextWrapper→Activity 声明链；资源服务未安装时
  `Context.getAssets` 通过调用引擎明确报告 missing handler。
- [WU-0130] 增加 HLE 专用引用解析边界，验证线程附着、local 作用域与 pending
  exception gate，只返回强类型对象身份，不泄露宿主指针。
- [WU-0131] 声明式安装 AssetManager/InputStream，经受检 JNI string/byte[] 将 APK
  `assets/` 接入 VFS，闭合 getAssets/open/read/available/close 与失败路径。
- [WU-0132] 在 Context 层声明 getSharedPreferences，Activity 可继承查找；偏好 HLE
  未安装时继续由调用引擎明确报告 missing handler。
- [WU-0133] 实现按名称隔离的 MODE_PRIVATE SharedPreferences 与 Editor，闭合 typed
  默认值、contains、put/remove/clear、commit/apply 及类型错误。
- [WU-0134] 实现 InputStream.read(byte[],offset,length)，目标范围在读取前受检，非法
  范围不推进 VFS descriptor，部分读与 EOF 保持 Java 语义。
- [WU-0135] 实现 InputStream.read() 的 0..255/EOF 语义及 skip(long) 的非负、有界推进，
  两者与 available、byte[] read 共用同一 VFS descriptor 状态。
- [WU-0136] getSharedPreferences 入口与其余偏好方法一样强制经过 HLE 引用解析，伪造、
  跨线程、已失效 Context reference 在读取参数或修改状态前失败。

## 下一步（按优先级）

1. 创建 `WU-0137`，补齐样本常用 Locale/配置查询的声明式 HLE。
2. 继续按样本调用面补框架 HLE，不引入完整 Android services。
3. M3 出口继续使用三平台 warnings-as-errors 构建与累计契约样本验收。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
