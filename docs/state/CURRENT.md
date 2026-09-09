# 当前状态

更新（2026-09-09）：[DVM-132](../tasks/dexvm/DVM-132.md) 将 API 19
`android.net.Uri` 全部内部类与 `UriCodec` 迁入 BootDex，删除 C++ 简化 parser 与镜像字段；
query、Builder、UTF-8 编解码执行原版 Java。按范围暂不补 StrictMode 文件 URI 暴露及
external-storage canonical 分支。

更新（2026-09-09）：[DVM-131](../tasks/dexvm/DVM-131.md) 为致命 `invoke-*` 错误增加
receiver 与有界类型化参数现场；直接读取原始 DEX prototype/源寄存器，非空对象直接虚派
`toString()` 并有界打印返回内容，不改变正常调用热路径。

此前 [DVM-130](../tasks/dexvm/DVM-130.md) 将 guest 平台 JNI 入口统一到
DexVM/BootDex，删除 Build/SystemProperties、Context/Activity 服务、Settings.Secure、Bundle、
AudioTrack 的重复 HLE 与播放状态。纯 native/HLE 会话不再提供这些类，需装配 VM。

此前 [DVM-129](../tasks/dexvm/DVM-129.md) 修复资源型 application label
错误进入 fixed-font ASCII 限制的问题。`PackageManager.getApplicationLabel` 现按 AOSP
返回 Unicode `CharSequence`，不触发 UI 字形测量；exact PvZ 已越过原错误，新首错为
`Landroid/os/Build;->BRAND:Ljava/lang/String;`。
[DVM-128](../tasks/dexvm/DVM-128.md) 完成 API 19
`Settings.Secure.getString` 与沙盒身份。`ANDROID_ID` 首次由 OS CSPRNG 生成 64 位十六进制值，
持久沙盒跨启动稳定，ephemeral 每次重建；JNI/DexVM 共用配置且不读取宿主设备身份。
exact PvZ 已越过该缺口。
Button/TextView 三参构造与 buttonStyle(Small) 默认样式投影、TextView compound
drawables（measure/raster/文本带内缩）、UI kind 按真实继承链解析、RelativeLayout
TRUE(-1) 规则按 AOSP rule > 0 视为无锚点。exact PvZ 实跑 `CreateDiscoveryStrip 3/4/8`
完整输出并挂入 mFrameLayout 后置 GONE。

## 当前能力

- **发行/guest JNI**：exact Profile API 选择 bundled data。API 19 含 pinned AOSP 五库、
  AOSP OpenSSL `libcrypto.so`、ICU4C 51.1 库及依赖、968 类 BootDex 和 ICU 数据；来源、
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
- **Title**：PvZ 已越过 InitXpromo、PreferenceManager、onResume 焦点、`System.getenv`、
  `Settings.Secure` 与 Unicode application label，上次实测首错为 `Build.BRAND`（迁移后未复跑）；Tales 首错
  LocationListener，均未通过游戏 gate。

## 最近验证

- DVM-132：windows-msvc Release 受影响目标构建；Uri/DVM-97/968 类全链接定向 5 项、
  7427 断言，BootDex build/check、自测与架构三项通过。exact PvZ 首错仍为
  `ContentResolver.query`，BootDex `Uri$StringUri.toString()` 已在参数诊断打印完整 URI。
  payload 门禁仍因既有 guest JNI generator SHA 不一致失败；未跑全量或跨平台验收。

- DVM-131：windows-msvc Release 受影响目标构建；fatal stack/参数双后端定向 2 项、100 断言，
  capabilities/documentation/intrinsic architecture 3 项通过。未跑游戏、全量或跨平台验收。

- BootDex Throwable 定向测试仍 terminate，尚未归因；DVM-120 Typeface 定向测试
  在本 WU 前已失败（stash 验证与本次改动无关）；既有 GUI
  `process_manager.cpp:131` 仍使 platform_boundaries 门禁失败。本轮未运行全量 CTest、
  游戏 gate 或跨平台验收。
  最新架构记录为 [ADR-0055](../adr/dexvm.md#adr-0055)。

## 下一步与边界

1. 补 `ContentResolver.query` 的有界无 provider 路径，继续推进 PvZ。
2. 处理既有 GUI 门禁，补 macOS/Linux、DH 与 Diagnostics 验收。

OGPlay 仅覆盖登记的老游戏进程能力。完整 formatter/大数、宿主资源持久化、Proxy 生成、
privileged executor 工厂/安全上下文和高争用集合仍未交付。长期限制见
[KNOWN-ISSUES](KNOWN-ISSUES.md)。

索引：[DexVM](../tasks/dexvm/README.md) · [APK Startup](../tasks/apk-startup/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Playbook](../playbook/README.md)
