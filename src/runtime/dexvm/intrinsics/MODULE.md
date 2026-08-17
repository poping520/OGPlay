# 模块：runtime/dexvm/intrinsics

intrinsic 的逻辑单位仍然是 Java class：每个 class 恰好一个 `Declare_*()`，
handler 与对应 `Declare_*` 同址；`catalog.cpp` 只负责逐类显式聚合，
`shared.h` 只保存跨类复用的内部 helper。

文件组织默认仍是一类一个同名源文件。唯一例外是 `java.lang` Throwable
hierarchy：Android 4.4.4 的完整家族统一位于 `java_lang_throwables.cpp`，但每个
Java class 的 `Declare_*()`、catalog 声明与 catalog 注册仍各自独立。该 API-family
聚合文件为控制翻译单元数量允许超过通常 800 行限制。禁止新增
`misc`/`common`/`all` 等无语义聚合文件、家族级声明接口、字符串 core handler
id、全局静态自注册，以及 android.* 声明和行为顺手修改。

`java.lang.System` 的 `getProperty`/`setProperty` 共用会话内属性表；默认只发布
API 19 guest 可确定的 `/`、`:`、`\n` 三个 separator 属性，不读取宿主系统属性。
未知 key 返回 null，null/空 key 与 null value 按 Java 异常语义失败。
