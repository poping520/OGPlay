# 当前状态

- [DVM-150](../tasks/dexvm/DVM-150.md) 已替换 addShutdownHook 临时绕过：Runtime 的注册、
  移除和 exit/halt 协议执行原版 BootDex，System.exit 进入同一流程；退出码按 VM 保存，
  worker 不自 join，Application/Activity 显式退出后进程进入 stopped。真实 LogManager
  Handler 清理与纯 Java timed hook 已定向验证。宿主 Stop 仍是取消路径；Runtime 其他
  native 与 Java finalization 尚未支持，未扩张声明。shutdown 定向及加载回归 12 用例、355 断言通过。

- [DVM-149](../tasks/dexvm/DVM-149.md) 的 Apache HTTP 阻塞已前移：固定 API 19 ext.jar
  选入 329 类 Terms/Restlet 闭包（含 Commons Logging 反射实现），加两个 core 层级依赖后
  本地 BootDex 为 1407 类且全链接。双后端验证 BasicHttpParams、Restlet HTTP client 构造
  与日志写出；真实 APK 已输出 `Starting the Apache HTTP client`，当前首错为 Restlet
  `Reference.normalize()` 缺 `StringBuilder.substring(II)`。TLS 配置仍明确不支持，完整在线
  请求和 Dialog presentation 未验收；本地 bootdex.jar 不提交。

- [DVM-148](../tasks/dexvm/DVM-148.md) 已完成：真实 PvZ 非 survey Terms 页面使用横屏资源、
  多行文本、表格权重、ScrollView 与 `npTc` NinePatch 正常布局合成；Button 从 View 继承
  同一 Drawable 背景实例。真实输入 `13` 经 Editable/TextWatcher 由 guest 更新 accept
  alpha，前后截图及日志已保存。addShutdownHook 绕过已由 DVM-150 替换；
  IoUtils/ClassLoader 非 UI 临时绕过仍待后续独立完善。真机截图对照回归又补齐 XML widget 默认
  尺寸、framework textAppearance、空文本行高、大小写字形和按词换行。

- [WU-PERF-07](../tasks/optimization/WU-PERF-07.md) 已让 JNI 同步重入的 Dynarmic executor
  按 guest thread 与重入深度持久复用；每层仍隔离寄存器/栈，DexVM thread 退出时回收。
  pvz-amaz 8.1.0 的稳定期采样中 JIT 构造热点由 603/847 个样本降至 6 个；实跑
  42.743 秒呈现 17660 帧，平均约 413 FPS，已消除原约 2.6 FPS 卡顿。

- guest `NewStringUTF` 已与 API 19 Dalvik 对齐：null C 指针返回 null `jstring`，非空坏输入
  仍明确失败并按层级保留 JNI slot、guest thread、LR/SP、r0-r3 与原始 cause。该差异曾使
  pvz-amaz 8.1.0 的 `LoaderThread.runNative` 在 `lr=0x63125854` 错误终止；修复后实跑已
  进入 `eadpLogEventRouter`。同一路径的 null Java String `GetStringUTFLength/Chars` 与
  忽略 `jstr`、仅按 UTF pointer 释放的 `ReleaseStringUTFChars` 也已按 Dalvik 行为对齐；
  pvz-amaz 已越过 EASP 事件、完成全部 LoadTask，人工停止时呈现 48 帧并干净退出。

- [DVM-147](../tasks/dexvm/DVM-147.md) 已补齐 API 19 AndroidOpenSSL `SHA1PRNG` 与 AES
  KeyGenerator：统一 CSPRNG 首次播种后由 guest ARM OpenSSL 产出随机字节。pvz-amaz 8.1.0
  非 survey 实跑越过原 `NoSuchAlgorithmException`。随后补齐 `Log.d(tag,msg,throwable)`；
  后台异常已能正常记录且不再中止进程。session 的 Activity 均为顶层对象，`isChild()`
  已按该事实返回 false。首帧握手的 host progress 等待现有单次 2ms、总计约 128ms 的
  真正 wall-time 上限，常驻 runnable 服务线程不再阻断 Surface 回调；pvz-amaz 实跑已收到
  `surfaceCreated/surfaceChanged`、进入 production mode，并成功呈现 1 帧后干净退出。

更新：2026-09-12。

- BootDex-first 本地 Binder 已接通：IInterface/IBinder/Binder/Parcel、ResultReceiver 与内部
  IResultReceiver 及协议异常共 20 个 class_def 来自固定 API 19 JAR，BootDex 现为 1076 类。Binder 普通
  transact/onTransact 与 Parcel/Bundle/Intent/ResultReceiver 协议执行原版 Java；integration
  仅保留身份/线程策略和唯一字节 backing/Binder 引用 native。外部服务 bind 返回缺席，失败绑定保留连接登记供
  unbind 清理，重复/未登记 unbind 明确失败；跨进程 Binder、驱动、BinderProxy 后端、FD 与系统服务仍不支持。

## 当前能力

- **发行与 VM**：Profile 按 API 选择 bundled data。API 19 提供 AOSP guest 库、
  OpenSSL、ICU 51.1、本地验证用 1407 类 BootDex 与 ICU 数据；来源和校验见
  [manifest](../../data/android/19/manifest.json)。普通 Java 状态归字段/数组，JNI 使用
  VM 真实类型关系。
- **Java/密码/ICU**：已覆盖常用集合、并发、IO、序列化、反射、framework 值类、日期与
  格式化；AES、摘要及 RSA/ECDSA 摘要验签走 guest OpenSSL；Date/Number/DecimalFormat
  可走 guest ICU。PKIX、系统 CA、TLS、具名时区历史 DST 与完整 BigInt 不在当前范围。
- **Android 边界**：文件/VFS、资源 XML、Locale、Intent/Context、Activity 身份、窗口焦点、
  稳定 `ANDROID_ID` 和有界服务查询已接通。无跨进程 Binder/system_server、Play 服务、支付或完整
  Android 系统；未实现能力明确失败。
- **图形与 UI**：ANGLE GLES 错误按 guest 首错锁存；View hierarchy、常用布局、文本与
  drawable 已覆盖当前 title 路径。

## 最近进展

- [DVM-144](../tasks/dexvm/DVM-144.md) 验收修复：Parcel 异常回包、StrictMode 策略头、
  接口长度与 bind/unbind 登记已修复；22 个定向用例、12691 断言通过。原规范八组完整
  验收仍有待补门禁，详见工作单；payload generator SHA 的既有阻塞仍在。

- BootDex 一次迁入 19 个小闭包 framework 纯 Java 类；Point、Rect、AndroidException 删除
  重复 intrinsic，ArraySet 为后续 Intent 迁移补齐容器依赖。
- [DVM-137](../tasks/dexvm/DVM-137.md)：Resources Configuration 现在发布同一 VM 的默认
  Locale，并由原版 Java 同步 layout-direction。
- [DVM-143](../tasks/dexvm/DVM-143.md)：`getMethod/getDeclaredMethod` 改为按名称与参数定向
  查找，不再因无关方法签名里的缺失类型失败；PvZ 2.3.12 复跑完成加载任务。
- [DVM-142](../tasks/dexvm/DVM-142.md)：一次迁入 ResolveInfo 所需 PM 值类闭包，删除
  PackageItemInfo/ApplicationInfo intrinsic；PvZ 2.3.12 已越过原反射故障并完成加载任务。
- [BND-29](../tasks/boundary/BND-29.md)：GLES2 非法 capability 前置回送
  `GL_INVALID_ENUM`；真实 ANGLE 定向 2 用例、99 断言通过。
- [DVM-141](../tasks/dexvm/DVM-141.md)：JNI 数组元素出口按真实类型原子幂等注册。
- [DVM-140](../tasks/dexvm/DVM-140.md)：`Intent.putExtras(Bundle)` 委托 BootDex 浅合并。
- [SBX-14](../tasks/sandbox/SBX-14.md)：匿名 `mmap2` 使用 AddressSpace first-fit 账本；
  9 用例、65 断言通过。[SBX-13](../tasks/sandbox/SBX-13.md) 将大文件 IO 改为 64 KiB 分块。
- [BND-28](../tasks/boundary/BND-28.md)：ANGLE GLES error 回送共用 guest 锁存。
- [DVM-139](../tasks/dexvm/DVM-139.md)：JNI assignability、interface MethodID 虚派与静态
  字段查找统一使用 API 19 类型关系。
- Framework 近期完成 [DVM-135](../tasks/dexvm/DVM-135.md)～
  [DVM-138](../tasks/dexvm/DVM-138.md)：主 Looper/View.post、无传感器方向监听、
  Configuration 与 ViewParent。
- CLI 错误独立分块且每次故障只打印一次；未捕获 Java 异常按阶段、exception、message、
  stack trace 显示，MCP `guest_fault` 保持完整。

## 当前阻塞

- PvZ 尚未通过三轮 Scenario gate：2.3.12 无 Profile 路径已越过 PackageManager 反射故障并
  完成加载任务，人工停止于 8568 帧；其他版本/Profile 的 null Locale 与 guest memory fault
  仍需分别归因，不可混为同一结论。
- Tales 首错仍为未实现的 `LocationListener`。
- BootDex Throwable 定向测试仍 terminate；DVM-120 Typeface 定向测试存在既有失败。
- payload 门禁仍有 guest JNI generator SHA 不一致；GUI `process_manager.cpp:131` 仍阻塞
  platform_boundaries 门禁。
- 尚未执行全量 CTest、完整游戏 gate、macOS/Linux、DH 与 Diagnostics 验收。

## 下一步

1. 明确 PvZ Profile 选择差异，分别归因 null Locale 与首帧后的 guest memory fault。
2. 修复既有 GUI/payload 门禁，再补跨平台与剩余验收。

范围仍是老游戏进程直接调用的兼容能力；长期限制见
[KNOWN-ISSUES](KNOWN-ISSUES.md)。索引：[DexVM](../tasks/dexvm/README.md) ·
[APK Startup](../tasks/apk-startup/README.md) · [Layout UI](../tasks/layoutui/README.md) ·
[Playbook](../playbook/README.md)。
