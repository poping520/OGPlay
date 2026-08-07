# 子模块：runtime/framework

## 职责

提供所有游戏共用的声明式 Java 框架 HLE，目前覆盖 Activity 生命周期、Asset/InputStream、
SharedPreferences、Locale 和当前包 PackageInfo。

## 依赖

单向依赖 `runtime/jni`；Asset 子域可额外依赖 `runtime/vfs`。禁止依赖 Bionic、syscall、
execution、integration 或游戏 profile。

## 不变量

- 所有引用必须经过 JNI HLE 受检解析，跨线程字段只能保存 Global reference。
- 直接资源 HLE 只接受调用方提供的三个唯一 implementation id；路径按 legacy Java
  前缀/trim 规则规范化后只能读取 `/apk/assets/` 下来源为 APK 的受检文件，结果必须进入
  统一 primitive array store。
- 服务未安装或能力未实现时明确失败，不模拟完整 Android services。
- `src/` 不得出现游戏、厂商或包名特判。

## 测试

对应 `tests/runtime/framework_*_tests.cpp` 与累计无界面 JNI 契约。
