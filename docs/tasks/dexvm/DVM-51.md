# DVM-51 · intrinsic 开发效率底座

## 目标（一句话）

让新增 intrinsic 能从固定 API-19 class shape 直接得到当前 builder 骨架，并通过
类型化调用参数与链接期预绑定字段访问实现 handler，不再重复手写 descriptor 查找、
裸 slot 编解码和已漂移的生成样板。

## 依赖与源码真相

- DVM-32、DVM-34、DVM-49
- `.local/asop/libcore/luni/src/main/java/java/lang/`
- 固定 tag：`android-4.4.4_r2.0.1`

`data/dexvm/api19-java-lang-surface.json` 只记录上述 Luni 目录 95 个顶层类型在源码中
声明的 public/protected 字段、构造器和方法；泛型按 JVM descriptor 擦除，不人为加入
编译器生成的 bridge 方法或嵌套类型。`Thread` 位于 libdvm 的另一棵源码树，不混入该
Luni 清单。

## 交付

1. `IntrinsicCall` 统一提供 receiver、int/long/float/double/reference 参数读取、
   Java-visible 非空参数检查，以及五类字段的 instance/static、默认 receiver/显式
   object 读写；field kind、对象可赋值性、slot tag/宽度全部受检。
2. `BoundInstanceField` / `BoundStaticField` 在声明时返回不可伪造 handle；链接器消费
   catalog 时把声明 token 绑定到该 VM 的 `VmFieldId`。handler 只捕获 token，避免把
   某个 linker 的 field id 放进进程级静态状态，也不再逐调用按字符串解析字段。
3. `java.lang.Thread` 作为代表性迁移：七个 Java-visible field 和 handler 参数全部
   改走上述接口；线程生命周期语义保持 DVM-48/DVM-50 不变。
4. `dexvm_api19_surface.py` 从 pinned Java 源码生成/校验确定性 JSON；
   `dexvm_stub_gen.py --surface ... --class ...` 直接输出当前
   `IntrinsicClassBuilder::{Class,Interface}`、预绑定字段和显式未实现方法骨架。
   原 gap/survey 模式同步修复为当前 `Class`/`Interface`/`VirtualMethod` API。

## 边界

- 不增加任何 guest API 行为或 title/profile 分支，`capabilities.toml` 无状态变化。
- class-shape 抽取器不是 Java 编译器，不枚举 private 成员、嵌套类型和 synthetic bridge；
  清单的边界就是 pinned 源码的 public/protected 顶层声明。
- 本 WU 按用户明确授权忽略通常的 10 文件与 800 行限制。

## 验收

- 合成类同时覆盖 int/long/float/double/reference instance field、static field、默认
  receiver、显式 object、类型化参数与非空参数路径。
- stub generator 自测锁定当前 builder API、字段 handle 和显式未实现方法输出。
- API-19 surface 自测锁定泛型擦除、数组/varargs、继承接口、字段与可见性过滤；本地
  pinned 源码可用 `--check data/dexvm/api19-java-lang-surface.json` 做逐字节漂移检查。
- Windows/x64 `windows-msvc` 配置、构建与 838 项 CTest 全量通过。

状态：完成。
