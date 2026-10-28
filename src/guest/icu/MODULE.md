# 模块：BootDex guest ICU JNI

本目录实现 `api19.json` 审计清单中 required_backend 的 ICU（含 Locale 显示名、ISO3 与可用区域枚举）、NativeDecimalFormat、
NativeNormalizer、Pattern/Matcher、字符集名称枚举与 TimeZoneNames native。源码只调用 ICU 51 C ABI；禁止包含 ICU C++ API、链接 NDK
libc++，或跨越 API 19 STLport C++ ABI。

- 不在宿主下载、编译或链接 ICU，也不嵌入 ICU 数据，因为区域语义与数据统一由固定 API19 guest ICU 提供。

`JNI_OnLoad` 由 `src/guest/crypto/crypto_jni.c` 唯一定义并调用本模块注册入口。ICU data
从 guest VFS 固定路径 `/system/usr/icu/icudt51l.dat` 读取，校验精确大小后交给
`udata_setCommonData_51`；随后禁用文件回退并以 `u_init_51` 立即验证。卸载先关闭 formatter/regex peer、
调用 `u_cleanup_51`，再释放 common data。

NativeDecimalFormat 与 Pattern/Matcher 使用单调逻辑 token，registry 的发布/查找/移除由
guest mutex 保护；同一 Matcher 由原版 Java monitor 串行，DecimalFormat 保持 API19 的非线程安全
约定。close、clone、非法 token 与库析构均明确处理；GC/teardown 通过 Java address 字段关闭 peer。
整数、double 与十进制 digit-list 格式通过逐字段 C ABI 与 grouping symbol 扫描恢复完整
FieldPositionIterator；parse 按 ICU 结果返回 Long/Double 并保持 ParsePosition。区域数据使用 ICU 的区域回退，不限语言白名单。LocaleData 的日期字段、
相对日与货币来自 ICU resource；Unicode 大小写由 `u_strToLower/Upper` 承担。只注册审计清单内
required backend；另注册 Character 的 toLowerCaseImpl/toUpperCaseImpl，按 libcore 用 ICU51
u_tolower/u_toupper 做单 code point 简单映射，供 String.regionMatches 的逐 char 折叠使用。
其余 BootDex native 继续由 intrinsic catalog 的显式未实现声明拒绝。
Pattern/Matcher 普通协议来自 API19 BootDex，regex_jni.c 只调用 uregex C ABI；matcher 持有
独立 UTF-16 native 副本，覆盖 input/region、UTF-16 group offsets、bounds、find/matches 与
关闭，不长期保存 Java 字符指针。Normalizer 使用 unorm；字符集名称读取 ICU provider 表，
公开 Charset.availableCharsets 只发布运行时确实支持的六种标准 codec。
构建 manifest 固定所有 guest C 源文件与 `icu51_capi.h` 的哈希，ABI 变化不能绕过复现校验。
