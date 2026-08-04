# M3 JNI 与 Java 框架验收

日期：2026-08-04

## 验收结论

M3 已通过出口验收。源码基线 `05c5c59` 在 Windows/MSVC、Linux/GCC、
macOS/AppleClang 上均以 warnings-as-errors 完成配置与构建，并通过全量 CTest 232/232。
三个平台执行同一累计无界面 JNI 契约并命中同一确定性 trace 断言。

## 已完成开发范围

| 开发项 | 状态 | 机器证据 |
| --- | --- | --- |
| JNIEnv/JavaVM | 完成 | 233 槽 ABI；208 个常用 JNIEnv 与 4 个 JavaVM 行为槽；其余槽可观测 trap |
| 引用与异常 | 完成 | Local/Global/Weak、局部帧、容量、pending exception gate 与清理契约 |
| 签名与调用 | 完成 | 严格描述符、Modified UTF-8、virtual/nonvirtual/static 与三种参数入口 |
| 声明式框架 HLE | 完成 | Activity、Asset/InputStream、SharedPreferences、Locale、当前包 PackageInfo |
| Native 工作线程 | 完成 | 两个真实工作线程 daemon attach、HLE callback、detach 与主线程隔离 |
| DEX L1 | 完成 | 受检只读解析、引擎指纹、Java 厚度与渲染规模报告，不执行字节码 |
| 累计无界面契约 | 完成 | 双向调用、数据、引用、异常、框架服务、工作线程和资源闭环 |

## 三平台出口证据

| 平台与工具链 | 配置与构建 | 全量 CTest | M3 累计契约 |
| --- | --- | --- | --- |
| Windows / MSVC | warnings-as-errors 通过 | 232/232 | 通过 |
| Linux / GCC | warnings-as-errors 增量构建通过 | 232/232 | 通过 |
| macOS / AppleClang | warnings-as-errors 增量构建通过 | 232/232 | 通过 |

累计契约的机器判定证据为测试 154–156：Java/HLE 与 native 双向闭环、确定性累计
trace、缺失或类型错误 native 入口的明确失败。全量集合还覆盖 233 槽 ABI、低频槽可观测
trap、引用/异常、JavaVM attach/detach、框架服务、DEX L1 与两个真实 native 工作线程。

出口预检同时修复了旧版 CMake 对 `IN_LIST` 的策略解析差异，以及 AppleClang 严格转换
检查暴露的 DEX 位拼接隐式收窄；修复后由同一源码基线重跑三平台完整出口。

## Work Unit 范围

- `WU-0104..0122`：JNI ABI、引用/异常、字符串/数组、JavaVM、类/字段/调用与首个累计契约。
- `WU-0123..0128`：DEX L1 报告与常用 JNI 槽扩展。
- `WU-0129..0142`：Activity、资源、偏好、Locale、包信息和 native 工作线程累计闭合。
- `WU-0143`：功能开发状态闭合。
- `WU-0144..0147`：完成 runtime 七个子模块契约、文档门禁与纯机械物理迁移。
- `WU-0148`：修复出口预检发现的 CMake/AppleClang 可移植性问题。
- `WU-0149`：以同一源码基线完成三平台出口验收并闭合状态。

## 明确不属于 M3

- 完整 ART/Dalvik、Binder、system_server、Zygote 或真实 AOSP framework。
- 低频 JNI 反射、direct buffer 等无累计样本证据的槽；它们继续记账并明确 trap。
- ANGLE、EGL/GLES、真实 present、输入与音频播放累计集成：M4。
