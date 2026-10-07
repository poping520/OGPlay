# 模块：无 NFC 设备的 API19 Java 客户端

仅支持 API19 类型链接及无适配器发现，不提供 NFC 硬件或通信。
NfcAdapter 保留 AOSP 两个 NDEF 回调接口和两个 getDefaultAdapter 查询方法；
NfcManager 与事件/NDEF 值类型由固定 framework.jar 配方提供。
查询参数检查、manager 的字段和异常转换由 BootDex 执行。

隐藏 getNfcAdapter 工厂经 private nativeGetAdapter 明确抛 UnsupportedOperationException，
NfcManager 原构造器将无设备结果保存为 null；正常查询不创建 NfcAdapter。
构造器及已声明的回调注册入口经 nativeUnsupported 记账并失败，不登记/派发回调。
Context 返回正常初始化的进程内 manager；feature 集合不发布 NFC。

来源：AOSP Android 4.4.4 NfcAdapter.java；保留 Apache-2.0 头。
不引入 Binder、NfcService、标签读写、Beam、扫描或卡模拟。值类的 Java 数据处理
不代表可进行 NFC 通信；其他未纳入 API 不自动补齐。
决策：[ADR-0103](../../../../../../docs/adr/runtime.md#adr-0103)。
