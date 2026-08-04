# M3 JNI 与 Java 框架验收（待出口）

日期：2026-08-04

## 当前结论

M3 的代码与本机开发回归已经闭合，尚未执行 Windows、Linux、macOS 三平台出口测试，
因此本文件不是里程碑通过结论。下一会话补齐出口证据后才能把 M3 标记为完成。

## 已完成开发范围

| 开发项 | 状态 | 机器证据 |
| --- | --- | --- |
| JNIEnv/JavaVM | 完成 | 233 槽 ABI；208 个常用 JNIEnv 与 4 个 JavaVM 行为槽；其余槽可观测 trap |
| 引用与异常 | 完成 | Local/Global/Weak、局部帧、容量、pending exception gate 与清理契约 |
| 签名与调用 | 完成 | 严格描述符、Modified UTF-8、virtual/nonvirtual/static 与三种参数入口 |
| 声明式框架 HLE | 完成 | Activity、Asset/InputStream、SharedPreferences、Locale、当前包 PackageInfo |
| Native 工作线程 | 完成 | 两个真实工作线程 daemon attach、HLE callback、detach 与主线程隔离 |
| DEX L1 | 完成 | 受检只读解析、引擎指纹、Java 厚度与渲染规模报告，不执行字节码 |
| 累计无界面契约 | 开发完成 | 双向调用、数据、引用、异常、框架服务、工作线程和资源闭环 |

## 待执行出口

| 出口条件 | 状态 |
| --- | --- |
| Windows/MSVC warnings-as-errors 构建与全量 CTest | 待执行 |
| Linux/GCC warnings-as-errors 增量构建与全量 CTest | 待执行 |
| macOS/AppleClang warnings-as-errors 增量构建与全量 CTest | 待执行 |
| 三平台累计无界面 JNI 契约结果一致 | 待执行 |

## Work Unit 范围

- `WU-0104..0122`：JNI ABI、引用/异常、字符串/数组、JavaVM、类/字段/调用与首个累计契约。
- `WU-0123..0128`：DEX L1 报告与常用 JNI 槽扩展。
- `WU-0129..0142`：Activity、资源、偏好、Locale、包信息和 native 工作线程累计闭合。
- `WU-0143`：功能开发状态闭合。
- `WU-0144` 起：出口前 runtime 子模块结构整理；三平台出口 WU 在拆分完成后编号。

## 明确不属于 M3

- 完整 ART/Dalvik、Binder、system_server、Zygote 或真实 AOSP framework。
- 低频 JNI 反射、direct buffer 等无累计样本证据的槽；它们继续记账并明确 trap。
- ANGLE、EGL/GLES、真实 present、输入与音频播放累计集成：M4。
