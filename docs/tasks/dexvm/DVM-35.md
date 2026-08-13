# DVM-35 · java.* intrinsic 按类分文件迁移（声明即绑定）

## 目标（一句话）

把 dexvm 内全部 java.*/javax.* intrinsic（`core_catalog.cpp` 声明 +
`interpreter_builtins.cpp`/`intrinsics_java.cpp`/`intrinsics_string.cpp`
约 140 个 handler）迁移为 `src/runtime/dexvm/intrinsics/` 下每个 Java 类
一个文件、声明与实现同址的形态，迁移完成后 dexvm 层不再有任何 handler id。

## 依赖

- DVM-34（builder 与双通道）。

## 方案

### 1. 目录与命名

```
src/runtime/dexvm/intrinsics/
  MODULE.md                     # 简短契约：目录规则、命名、禁止事项
  catalog.h / catalog.cpp       # 逐类函数清单，聚合出 CoreIntrinsicCatalog()
  java_lang_Object.cpp          # 一类一文件：Declare_java_lang_Object()
  java_lang_String.cpp
  java_lang_StringBuilder.cpp
  java_io_InputStream.cpp
  ...
```

- 文件名 = 类描述符去掉 `L;`、`/` 换 `_`（内部类 `$` 换 `_S_`，如有）。
- 每文件恰好一个 `IntrinsicClassDecl Declare_<同名>()`，用
  `IntrinsicClassBuilder` 同时给出形状与 handler；文件内可有匿名命名空间的
  私有 helper。跨类共享的 helper（`Value`/`Make`/UTF-8 转换等）收进
  `intrinsics/shared.h`（内部头，不进公共 include）。
- `catalog.cpp` 持逐类函数指针表并实现 `CoreIntrinsicCatalog()`；
  `Interpreter::RegisterCoreBuiltins()` 在本 WU 结束时变为空操作（保留符号到
  DVM-37 删除），core handler 不再注册进 registry。

### 2. 迁移规则（机械搬运，行为零变化）

- 逐类搬运：把 `core_catalog.cpp` 中该类的声明与散落各处的对应
  `Register("core.xxx", ...)` lambda 合入同一个 `Declare_*()`；lambda 体
  原样搬运，不做任何行为修改（顺手改动一律禁止，含格式化以外的"小优化"）。
- 分批提交建议按包：java/lang → java/io + java/util → 其余（java/net、
  javax.*、接口与异常层级）。每批全量 CTest 全绿再进下一批。
- `core_catalog.cpp`、`interpreter_builtins.cpp`、`intrinsics_java.cpp`、
  `intrinsics_string.cpp` 随最后一批清空删除；期间保留未迁移部分，
  `CoreIntrinsicCatalog()` = 新目录聚合 + 存量残余，两者类集合不得重叠
  （builder/linker 的重复类注册校验兜底）。
- 本专项为机械迁移型批次，新建文件数（约 100 个类文件）超出常规 WU 的
  10 文件预算属预期偏离，依据：单文件独立、无跨文件逻辑改动、全量回归兜底
  （先例：GUI-12 触及 19 文件）。

## 边界（不做）

- 不动 android.*（DVM-36）；不删 registry/id 通道与 `LinkedMethod` 字符串
  字段（DVM-37）。
- 不改任何 handler 行为；String intrinsic 的整串拷贝等性能项不在本 WU。

## 验收（机器可判定）

1. 全量 CTest 全绿；`tests/dexvm/intrinsics_p1_tests.cpp` 与 dexasm 夹具
   逐位不变。
2. 迁移完成判定：`src/runtime/dexvm/**` 中 `Register("` 出现次数为 0；
   `CoreIntrinsicCatalog()` 产出的 decl 全部 `implementation`/
   `clinit_implementation` 非空或显式有意留空。
3. 新增最小结构测试：`CoreIntrinsicCatalog()` 无重复类描述符；抽样断言
   若干代表类（Object/String/StringBuilder/Integer）的方法集合与迁移前
   一致（数量 + 名字 + descriptor）。
4. 收尾：`src/runtime/dexvm/MODULE.md` 文件分工更新（三个 handler 文件退场、
   `intrinsics/` 接管）、新目录 `MODULE.md`、`CURRENT.md` 滚动更新。
