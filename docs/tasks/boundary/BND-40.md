# BND-40 · API19 ARM32 linker 元数据视图

状态：能力交付完成。依赖 [BND-25](BND-25.md)，范围见 [ADR-0076](../../adr/runtime.md#adr-0076)。

## 范围与验收

- 普通 dlopen 句柄是有效 guest soinfo 地址；registry 拥有引用与退役，特殊伪句柄独立处理。
- 表从现有 ELF namespace/sealed exports 生成，闭合 base、符号/字符串与 SysV hash 查询。
  libc 等真实 ELF 的完整导出优先，并保留已截获后的实际地址；Virtual SO 指向现有 guest thunk。
- 通用 ARMv7 guest 载荷验证 ELF 与 boundary 表遍历、dlsym 等价及实际调用，重复打开/关闭、
  缺失符号/库与失效句柄受检。最后复现原 APK/OBB 启动，记录首个 dlopen 与下一首错。
- 不补 BootDex、不修改 mmap、不替换 APK、不放宽 JNI 校验，不改变模块/构造器/JNI 唯一归属。

## 验证结果

- Release `ogplay/ogplay_tests` 受影响目标构建通过；5 个定向用例、148 条断言通过。
- 固定 ARMv7 小载荷直接计算 ELF hash、读取 soinfo 的符号/字符串/bucket/chain/load_bias，
  查询真实 ELF 的可调用导出并返回 42；查询全部 liblog 导出，覆盖 hash 碰撞链，实际调用
  `__android_log_write`。真实 libc 同时保留未截获的数据导出与截获后的 strlen thunk。
- 与 dlsym 地址一致；重复打开共用记录与引用，最后关闭后 backing 不可读，重开不复用地址；
  未知符号/库、失效句柄、非法投影与 arena 耗尽明确失败，元数据稳态只读。
  原 RTLD_DEFAULT、GL alias、sealed fallback 与消费式 dlerror 回归通过。
- 私有布局只支持 ADR 中受审字段；其余槽保留为 0，不宣称完整 linker 行为。
  未进行 Windows 构建或游戏兼容验收。

同 APK/OBB、无 Profile、关闭 survey、ephemeral sandbox 的原启动路径（120 帧上限）
实测首个 dlopen 为 `liblog.so`，返回 `0x79000000`；随后真实 libc/libdl/libm 等视图可查询，
Mono（sequence=2）与 Unity（sequence=3）的 JNI 加载正常完成。原 `0xb5` 和段扩展故障未再出现。
下一致命错误为宿主 `JniExceptionError: JNI exception thread is not attached`，进程退出 134；
此前还有 NDK 符号缺失日志，未据此推断首错原因，本轮不修复下一独立缺口。

证据：`.local/bnd40-build.log`、`.local/bnd40-tests.log`、`.local/bnd40-dt-startup.log`；
动态句柄、成功加载与下一首错的机器断言见 `.local/bnd40-startup-check.log`。
