# 当前状态

更新（2026-09-08 续）：[DVM-125](../tasks/dexvm/DVM-125.md) 闭合 Activity/View
窗口焦点查询与通知。DexActivityLifecycle 维护唯一事实，初始 onResume 为 false、Surface
后下一帧获焦；状态先更新再虚派 Activity 与 attached View，切换/暂停恢复/Stop 去重。
DecorView 与挂载树查询一致，分离 View、旧 Activity 为 false；override 无 super 不影响状态。
原命令越过 `MainActivity.onResume → hasWindowFocus()`，新首错为后台线程调用
`String.format(Locale,String,Object[])` 无法解析。
Button/TextView 三参构造与 buttonStyle(Small) 默认样式投影、TextView compound
drawables（measure/raster/文本带内缩）、UI kind 按真实继承链解析、RelativeLayout
TRUE(-1) 规则按 AOSP rule > 0 视为无锚点。exact PvZ 实跑 `CreateDiscoveryStrip 3/4/8`
完整输出并挂入 mFrameLayout 后置 GONE。
compound 支持四方向/空文本测量与定位、资源事务更新；默认样式读取实际 Context
主题和 Resources，未登记覆盖明确失败。

## 当前能力

- **发行/guest JNI**：exact Profile API 选择 bundled data。API 19 含 pinned AOSP 五库、
  AOSP OpenSSL `libcrypto.so`、ICU4C 51.1 库及依赖、927 类 BootDex 和 ICU 数据；来源、
  hash、NOTICE、manifest 与 payload 校验已同步。crypto/ICU 保持源码模块边界，共用
  JNI_OnLoad 和 `libogplay_jni.so`；ICU 只调用 guest C ABI 与 `icudt51l.dat`，host 不链接 ICU。
  制品见 [manifest](../../data/android/19/manifest.json)；`bootdex.jar` 不提交。
- **密码能力**：Cipher 支持 AES 128/192/256、ECB/CBC NoPadding/PKCS5Padding、CTR；
  Digest 支持 MD5、SHA-1/256/384/512；RSA/ECDSA 摘要验签经 ARM EVP。Java 算法归
  BootDex，native 调 guest OpenSSL；分段、clone、摘要流、证书解析/链签名、GC/teardown
  已覆盖。不含 PKIX、系统 CA、TLS、签名生成、RSA Cipher、HMAC/Mac、SHA-3。
- **BootDex/VM**：集合、atomic/AQS、IO/Reader/Writer、对象序列化、Throwable、反射、
  executor、framework 值类、日期格式与值边界执行 API 19 Java；普通字段/数组为唯一状态，
  JNI 使用 VM 类型关系。BigInt/NativeBN 仅开放 17 个已审计值原语，其余 native 明确失败。
  SoftReference、String.intern、类初始化异常、接口数组协变与 threaded 异常身份已有回归。
- **ICU**：Date/Number/DecimalFormat required_backend 经 guest JNI 调 ICU 51；覆盖 ISO 表、
  LocaleData、货币、数字 parse/字段、复杂大小写和裸 `zh`。具名时区与历史 DST 未纳入审计，
  明确失败；标准六字符集仍走无 ICU 的宿主有界实现。
- **运行边界**：文件/VFS、资源 XML、Locale、URL codec、Intent/Context、平台 enum、
  Activity 组件身份、窗口焦点及 flags=0 的 action-only service 查询已接通。无 Binder/system_server、
  支付或完整 Android 系统；未知/潜在 native 或服务匹配不伪造成功。
- **Title**：PvZ 已越过 InitXpromo、PreferenceManager 与 onResume 焦点查询，首错为
  Nimble tracking 后台线程的 Locale `String.format` overload；Tales 首错
  LocationListener，均未通过游戏 gate。

## 最近验证

- DVM-125：windows-msvc Release 构建；双后端焦点链 96 断言、初始 traversal/切换及
  View 定向回归通过；相关架构门禁 4/5 通过。platform-boundaries 仍仅被既有 GUI
  `process_manager.cpp:131` 阻塞。实跑日志 `.local/review/dvm125/pvz-run.log`。
- DVM-122：NDK r25c ARMv7 API 19 两次构建一致；ELF ABI、SONAME、DT_NEEDED、payload、
  BootDex audit/self-test、Bionic profile、DVM-105/106/108 及大小写/结构定向回归通过。
  Windows Release 未下载、编译或链接 host ICU。
- DVM-121 续：BootDex check 与全类链接（927）、prefs/Context 邻域回归
  25/25（10,957 断言）通过；intrinsic layout、BootDex self-test、payload 三项门禁通过。
  exact PvZ 实跑日志 `.local/review/dvm121-prefs/pvz-run.log`。
- DVM-121：windows-msvc Release 构建；UI 56/56（1078 断言）、布局/样式 5/5
  （501 断言）及 intrinsic 门禁通过；原命令进入 InitXpromo 后的 onAdConfigCreate。
  隐藏树状态有独立回归。
- BootDex Throwable 定向测试仍 terminate，尚未归因；DVM-120 Typeface 定向测试
  在本 WU 前已失败（stash 验证与本次改动无关）；既有 GUI
  `process_manager.cpp:131` 仍使 platform_boundaries 门禁失败。本轮未运行全量 CTest、
  游戏 gate 或跨平台验收。
  最新架构记录为 [ADR-0052](../adr/dexvm.md#adr-0052)。

## 下一步与边界

1. 分析 PvZ 新首错 `String.format(Locale,String,Object[])`，继续推进 PvZ。
2. 处理既有 GUI 门禁，补 macOS/Linux、DH 与 Diagnostics 验收。

OGPlay 仅覆盖登记的老游戏进程能力。完整 formatter/大数、宿主资源持久化、Proxy 生成、
privileged executor 工厂/安全上下文和高争用集合仍未交付。长期限制见
[KNOWN-ISSUES](KNOWN-ISSUES.md)。

索引：[DexVM](../tasks/dexvm/README.md) · [APK Startup](../tasks/apk-startup/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Playbook](../playbook/README.md)
