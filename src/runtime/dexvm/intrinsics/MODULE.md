# 模块：runtime/dexvm/intrinsics

每个 Java 类由一个同名源文件通过 `Declare_*()` 同址声明形状与 handler。
`catalog.cpp` 仅负责显式聚合；`shared.h` 只保存跨类复用的内部 helper。
禁止字符串 core handler id、全局静态自注册、android.* 声明和行为顺手修改。
