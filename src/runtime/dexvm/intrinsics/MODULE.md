# 模块：runtime/dexvm/intrinsics

每个 Java 类由一个同名源文件通过 `Declare_*()` 同址声明形状与 handler。
`catalog.cpp` 仅负责显式聚合；`shared.h` 只保存跨类复用的内部 helper。
禁止字符串 core handler id、全局静态自注册、android.* 声明和行为顺手修改。

`java.lang.System` 的 `getProperty`/`setProperty` 共用会话内属性表；默认只发布
API 19 guest 可确定的 `/`、`:`、`\n` 三个 separator 属性，不读取宿主系统属性。
未知 key 返回 null，null/空 key 与 null value 按 Java 异常语义失败。
