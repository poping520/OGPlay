# 模块：runtime/dexvm/intrinsics

intrinsic 的逻辑单位仍然是 Java class：每个 class 恰好一个 `Declare_*()`，
handler 与对应 `Declare_*` 同址；`catalog.cpp` 只负责显式聚合，
`shared.h` 只保存跨类复用的内部 helper。

文件组织默认仍是一类一个同名源文件。例外是 Android 4.4.4 `java.lang`
Throwable hierarchy 与 primitive wrapper family，分别统一位于
`java_lang_throwables.cpp` 和 `java_lang_primitive_wrappers.cpp`。Java class 仍是
一等逻辑单位，每类保留独立 `Declare_*()`，但这些函数为 family TU-private；
文件只向 `catalog.h` 暴露对应的 `AppendJavaLangThrowables()` 或
`AppendJavaLangPrimitiveWrappers()`，`catalog.cpp` 不感知 family 内具体 class。
后续 API-family 沿用相同规则。family TU 为控制翻译单元数量允许超过通常 800 行。
禁止新增 `misc`/`common`/`all` 等无语义聚合文件、字符串 core handler id、
全局静态自注册，以及 android.* 声明和行为顺手修改。

`java.lang.System` 的 `getProperty`/`setProperty` 与 primitive wrapper property
API 共用每 VM 属性表；默认只发布
API 19 guest 可确定的 `/`、`:`、`\n` 三个 separator 属性，不读取宿主系统属性。
未知 key 返回 null，null/空 key 与 null value 按 Java 异常语义失败。
