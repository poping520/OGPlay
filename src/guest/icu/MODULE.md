# 模块：BootDex guest ICU JNI

本目录实现 `api19.json` 审计清单中 required_backend 的 ICU、NativeDecimalFormat 与
TimeZoneNames native。源码只调用 ICU 51 C ABI；禁止包含 ICU C++ API、链接 NDK
libc++，或跨越 API 19 STLport C++ ABI。

`JNI_OnLoad` 由 `src/guest/crypto/crypto_jni.c` 唯一定义并调用本模块注册入口。ICU data
从 guest VFS 固定路径 `/system/usr/icu/icudt51l.dat` 读取，校验精确大小后交给
`udata_setCommonData_51`；随后禁用文件回退并以 `u_init_51` 立即验证。卸载先关闭 formatter、
调用 `u_cleanup_51`，再释放 common data。

NativeDecimalFormat 使用单调逻辑 token、VM 串行执行保护的 registry 和 ICU C formatter；close、clone、
非法 token 与库析构均明确处理。整数格式通过逐字段 C ABI 与 grouping symbol 扫描恢复完整
FieldPositionIterator；parse 按 ICU 结果返回 Long/Double 并保持 ParsePosition。LocaleData 的日期字段、
相对日与货币来自 ICU resource；Unicode 大小写由 `u_strToLower/Upper` 承担。只注册审计清单内
required backend；其余 BootDex native 继续由 intrinsic catalog 的显式未实现声明拒绝。
构建 manifest 同时固定两个 C 源文件与 `icu51_capi.h` 的哈希，头文件 ABI 变化不能绕过复现校验。
