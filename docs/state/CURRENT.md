# 当前状态

更新（2026-09-09）：[BND-29](../tasks/boundary/BND-29.md) GLES2 非法 capability
前置校验回送 GL_INVALID_ENUM；真实 ANGLE 定向 2 用例/99 断言通过。
[DVM-141](../tasks/dexvm/DVM-141.md) 数组元素 JNI 出口按真实类型原子幂等注册已完成。
[DVM-140](../tasks/dexvm/DVM-140.md) Intent.putExtras(Bundle) 委托 Java 浅合并。

更新（2026-09-09）：[SBX-14](../tasks/sandbox/SBX-14.md) 将匿名 mmap2 改为
AddressSpace 账本锁内 first-fit 选址并映射；跳过全部已映射页，复用释放空洞，
失败不消耗地址，brk 状态串行化。9 用例/65 断言通过；PvZ 越过原内存故障到第 1 帧。

[SBX-13](../tasks/sandbox/SBX-13.md)：文件 IO 改为 64 KiB 分块，解除 16 MiB 限制；
40,531,351 字节 read 完整成功。

[BND-28](../tasks/boundary/BND-28.md)：ANGLE GLES error 回送共用 guest 锁存，
glGetError 首错优先、读取清除，宿主契约错误仍终止。

更新（2026-09-09）：[DVM-139](../tasks/dexvm/DVM-139.md) 修复 DexVM→JNI 类发布丢失
interface 图：assignability、interface MethodID 虚派与静态字段查找按 API 19 Dalvik 规则
统一使用真实类型关系，越过 `ITracking.setEnable(Z)V` 错误 incompatibility。

近期 framework 交付：UiTree 父级查询 [DVM-138](../tasks/dexvm/DVM-138.md)、
BootDex Configuration [DVM-137](../tasks/dexvm/DVM-137.md)、无传感器的
OrientationEventListener [DVM-136](../tasks/dexvm/DVM-136.md)、主 Looper 与
detached action 暂存的 View.post/postDelayed [DVM-135](../tasks/dexvm/DVM-135.md)。

Button/TextView 三参构造与 buttonStyle(Small) 默认样式投影、TextView compound
drawables（measure/raster/文本带内缩）、UI kind 按真实继承链解析、RelativeLayout
TRUE(-1) 规则按 AOSP rule > 0 视为无锚点。exact PvZ 实跑 `CreateDiscoveryStrip 3/4/8`
完整输出并挂入 mFrameLayout 后置 GONE。

## 当前能力

- **发行/guest JNI**：exact Profile API 选择 bundled data。API 19 含 pinned AOSP 五库、
  AOSP OpenSSL `libcrypto.so`、ICU4C 51.1 库及依赖、972 类 BootDex 和 ICU 数据；来源、
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
  Activity 组件身份、窗口焦点、沙盒稳定 `ANDROID_ID` 及 flags=0 的 action-only service
  查询已接通。无 Binder/system_server、SettingsProvider、支付或完整 Android 系统；未知/
  潜在 native 或服务匹配不伪造成功。
- **Title**：PvZ exact 首错为 `guest memory is unmapped`；Tales 首错 LocationListener，均未通过
  游戏 gate。

## 最近验证

- BND-28：Windows Release 受影响目标构建；真实 ANGLE texture 定向 1 项、60 断言通过。
  exact PvZ 越过 `glTexParameteri` GLES 1280，新首错为 `guest memory is unmapped`。

- DVM-139：Windows Release 受影响目标构建；JNI interface graph、MethodID 虚派与
  DexVM bridge 三组定向 7 项、80 断言通过。exact PvZ 越过原 JNI incompatibility，
  新首错为 `glTexParameteri` GLES 1280。

- DVM-138：Windows Release 受影响目标构建；双解释器父级查询、既有动态 hierarchy、
  Android catalog 与 BootDex 全类链接 4 项、10544 断言通过。exact PvZ 越过
  `View.getParent()`，新首错为 JNI receiver/dispatch class 不兼容。

- DVM-137：Windows Release 受影响目标构建；BootDex build/check 972 类；双解释器
  Configuration、既有 screenLayout/DisplayMetrics 与全类链接 4 项、7569 断言通过。
  exact PvZ 越过 hidden/orientation 字段，新首错为 `View.getParent()`。

- BootDex Throwable 定向测试仍 terminate，尚未归因；DVM-120 Typeface 定向测试
  在本 WU 前已失败（stash 验证与本次改动无关）；payload 门禁仍因既有 guest JNI
  generator SHA 不一致失败；既有 GUI
  `process_manager.cpp:131` 仍使 platform_boundaries 门禁失败。本轮未运行全量 CTest、
  游戏 gate 或跨平台验收。
  最新架构记录为 [ADR-0055](../adr/dexvm.md#adr-0055)。

## 下一步与边界

1. 归因 PvZ `guest memory is unmapped`，继续推进 title。
2. 处理既有 GUI 门禁，补 macOS/Linux、DH 与 Diagnostics 验收。

OGPlay 仅覆盖登记的老游戏进程能力。完整 formatter/大数、宿主资源持久化、Proxy 生成、
privileged executor 工厂/安全上下文和高争用集合仍未交付。长期限制见
[KNOWN-ISSUES](KNOWN-ISSUES.md)。

索引：[DexVM](../tasks/dexvm/README.md) · [APK Startup](../tasks/apk-startup/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Playbook](../playbook/README.md)
