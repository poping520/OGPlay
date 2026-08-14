# 子模块：runtime/framework

## 职责

提供所有游戏共用的声明式 Java 框架 HLE，目前覆盖 Activity 生命周期、Asset/InputStream、
SharedPreferences、Locale、当前包 PackageInfo 和窗口屏幕策略状态。

## 依赖

单向依赖 `runtime/jni`；Asset 子域可额外依赖 `runtime/vfs`。禁止依赖 Bionic、syscall、
execution、integration 或游戏 profile。

## 不变量

- 所有引用必须经过 JNI HLE 受检解析，跨线程字段只能保存 Global reference。
- 直接资源 HLE 只接受调用方提供的三个唯一 implementation id；路径按 legacy Java
  前缀/trim 规则规范化后只能读取 `/apk/assets/` 下来源为 APK 的受检文件，结果必须进入
  统一 primitive array store。
- `FrameworkScreenPolicyState` 线程安全保存最近一次允许宿主屏幕休眠/保持唤醒请求与请求
  计数；状态未请求时必须保持未知，不能伪造默认窗口策略。
- legacy phone-language index 从同一受检、确定性 Locale 配置派生：法/德/意/西/日/英/葡
  为 `0..6`，支持 ISO-639 两/三字母代码，其他合法语言明确回退英语索引 `5`。
- 服务未安装或能力未实现时明确失败，不模拟完整 Android services。
- `src/` 不得出现游戏、厂商或包名特判。

## 测试

对应 `tests/runtime/framework_*_tests.cpp` 与累计无界面 JNI 契约。

## SharedPreferences 落盘

`preferences_xml.h` 是 framework HLE 与 DexVM handler 共享的唯一 prefs 读写
实现（ADR-0020）：guest 路径 `/data/data/<pkg>/shared_prefs/<name>.xml`，
Android 同构 XML（标量走属性、`<string>` 走元素正文），经 VFS 普通文件通道，
`commit()` 是落盘点。个别游戏绕过 API 直接读该文件，因此文件视角与 API 视角
必须指向同一份事实。受检子集为 boolean/int/long/float/string；string set、
未知元素/属性/实体与 DTD 一律明确失败并保留原文件，不静默丢条目。
加载只把 `-ENOENT` 解释为首次运行；EIO/EACCES 等 VFS 错误转换为明确
`PreferencesXmlError`。float 以 locale-free 最短往返表示渲染和受检解析，保持
IEEE-754 值不变；解析显式使用 classic locale 与完整消费检查，避免依赖仅在较新
macOS libc++ 提供的浮点 `std::from_chars` 运行时符号。
