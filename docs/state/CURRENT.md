# 当前状态

更新：2026-08-06 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；Title Profile v1 schema、C++ 强类型加载和三重身份匹配已闭合，
  当前仍不提交具体游戏 profile。
- Windows/MSVC、Linux/x64 与 macOS/arm64 均在同一主仓库 commit `f1b59bb` 上以 ANGLE
  开启和 warnings-as-errors 通过严格全量 CTest 302/302。记录见
  [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0200] C++ 受限 TOML 加载器把 identity/runtime 转为强类型只读模型，严格检查
  UTF-8、200 行、文件名/package、API/ABI、生命周期、表面范围与未知字段。
- [WU-0201] data/audio 声明式加载覆盖 APK/OBB/external 挂载、资源清单、工作目录与
  封面音乐，guest/资源路径均拒绝逃逸和非规范形式。
- [WU-0202] Java 类/方法绑定、quirk 参数和输入模板进入纯数据模型；重复绑定、禁用参数、
  缺失参数表和非法 id 明确失败，不执行脚本。
- [WU-0203] profile 目录稳定排序加载，只以 package + versionCode + `.so` SHA-256
  三项精确匹配；非法和交叠身份失败，无匹配保留通用路径。四个 WU 在 Windows/MSVC
  warnings-as-errors 构建及全量 CTest 311/311 中通过。
- [WU-0204] `data/quirks.toml` 进入 CI 门禁：五个说明字段、规范 owner、真实
  `tests/*.cpp:<case-name>` 引用及所有 profile enabled 项均受检。
- [WU-0205] C++ `QuirkRegistry` 加载 UTF-8 注册表与多行 reason；含 quirk 的 Profile
  目录必须显式注入注册表，未注册或未注入均明确失败；Windows/MSVC warnings-as-errors
  构建与全量 CTest 318/318 通过，当前仍未添加真实游戏 quirk。
- [WU-0199] 冻结 Title Profile v1 JSON Schema 与纯标准库 TOML 校验器；身份三重指纹、
  API/ABI/生命周期、声明式 data/audio/java/quirks/input、文件名一致性及 200 行上限
  均有机器正反例，仓库 profile 目录进入 CTest；Windows/MSVC warnings-as-errors 构建与
  全量 CTest 304/304 通过，尚未提交具体游戏 profile。
- [WU-0195] Windows/MSVC 出口预演：真实 Bionic 与两个 APK，严格全量 CTest 297/297。
- [WU-0196] macOS/arm64 Metal 与 SwiftShader 出口；严格全量 CTest 297/297，关键测试
  连续 5 次稳定。
- [WU-0197] 原生 ANGLE 测试统一为 Windows D3D11、Linux Vulkan、macOS Metal 硬件映射。
- [WU-0198] Linux/x64 严格出口通过：修复 GCC missing-field-initializers /
  range-loop-construct，并以清单 ICD + Vulkan loader 闭合 SwiftShader 黄金帧；审查后
  增加作用域 driver 环境、可重定位 ICD、真实 renderer 与 software→hardware 隔离回归，
  修复后严格全量 CTest 302/302；编写 `M4-ACCEPTANCE.md`。
- macOS/arm64 审查修复复验：POST_BUILD 暂存 `libEGL`/`libGLESv2` dylib 后严格全量
  CTest 302/302；Metal/SwiftShader/隔离与两个真实 APK 各连续 5 次稳定。
- Windows/MSVC 审查修复复验：同 commit `f1b59bb` 严格全量 CTest 302/302；D3D11 黄金帧、
  SwiftShader 明确失败、backend 隔离与两个真实 APK 各连续 5 次稳定；冻结 M4 验收基线。

## 下一步（按优先级）

1. 实现三种通用生命周期描述与单一帧步骤序列，不复制每游戏循环。
2. 把 profile 的 VFS/Java/input 声明接到现有通用机制，仍不提交具体游戏 profile。
3. M4 范围外项目（窗口 surface、未绑定 GLES2、通用多库入口等）不得伪造成功，继续以
   能力账本为准。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
