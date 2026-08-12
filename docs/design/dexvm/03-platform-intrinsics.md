# 03 · 平台内建类（intrinsic）

## 1. 边界定义

以类名前缀划界，规则固定且机器可判定：

- `android.*`、`java.*`、`javax.*`、`dalvik.*`、`org.apache.http.*`（这代游戏
  常引用）→ **只能是 intrinsic**。应用 DEX 中若携带同名类（个别游戏打包了
  支持库），以 intrinsic 优先并记账告警——不解释平台命名空间的字节码。
- 其余全部类名 → 应用类，由解释器执行。`src/` 禁止游戏名/厂商名/包名的
  规则不受影响：intrinsic 只有平台类名，游戏类名只存在于 DEX 数据里。

intrinsic 是**代码定义的不可变目录**（对齐 `input.template_catalog` 的既有
模式），不是 profile 声明。profile 的 `[[java.class]]` 机制在 dexvm 生命周期
下不再参与装配。

## 2. intrinsic 类的构成

每个 intrinsic 类在目录中声明：

| 要素 | 说明 |
| --- | --- |
| 类名 / super / 接口 | 参与统一类层级；游戏类可继承（`extends Activity`）或实现（`implements Renderer`） |
| 构造器 | 绑定宿主状态创建（产出宿主背衬实例，见 02 §6） |
| 方法表 | 每方法 = 签名 + handler；handler 复用现有 impl handler 目录（`audio.track.write` 等存量直接挂接） |
| 可 override 虚方法集 | 显式声明哪些方法允许游戏类 override（`onCreate`、`onDrawFrame`、`run`…），宿主经 vtable 派发进解释器 |
| 字段语义 | intrinsic 字段一律经 getter/setter handler，不暴露原始槽位；常量字段（如 `Build.VERSION.SDK_INT`）在链接期物化 |

`PlatformClassProvider` 是 dexvm 侧的显式接口；`runtime/integration` 在 session
装配时注入实现（framework HLE 存量 + 新增），dexvm 不反向依赖 framework。
方法描述子的最小形态：

```cpp
struct IntrinsicMethod final {
    std::string_view name;
    std::string_view descriptor;
    bool is_static{};
    bool overridable{};          // 允许应用类 override（vtable 派发锚点）
    PlatformHandlerId handler;   // 复用现有 impl handler 目录
};
```

**语义出处纪律**：java.* intrinsic 中有 native 半边的（`System.arraycopy`
的重叠/类型检查顺序、`Object.hashCode/clone`、`Class.*`、`Runtime.*`、
`Thread.*`），实现前对照 vendor 基线的 `vm/native/java_lang_*.cpp`
（[07 §2](07-aosp-reference.md)）；纯 Java 半边（集合、StringBuilder 等，
源码在 libcore，不 vendor）以 JLS/类库文档 + 夹具断言为准。宿主背衬实例
若需持有 VM 对象引用，一律经 JNI global ref 表——GC 根集不扫宿主状态
（04 §5 硬约束）。

## 3. java.* 最小集（分批）

原则：**按真实命中扩展，不预先照搬 JDK 面**。首批以三款存量 Gameloft title
与一款 libGDX 样本的静态引用测量（阶段 0 工具产出）核准；此外，存量 profile
的通用 impl handler 目录（analytics/license/device/audio/locale 等约 60 个 id）
是"胶水叶子方法实际触达哪些平台 API"的现成实证语料——每个人工 handler 都是
对一段真实字节码行为的人工摘要，方法级接管（04 §1）的 intrinsic 最小集
优先以它反推校准。预计如下：

| 批次 | 类 | 后端 |
| --- | --- | --- |
| P1 · 语言核心 | Object、Class、String、StringBuilder/StringBuffer、Throwable + 核心异常层级（RuntimeException/NPE/AIOOBE/CCE/ArithmeticException/OOM/StackOverflow…） | 对象模型原生：String 即 VM 字符串；异常即 VM Throwable |
| P1 · 系统 | System（arraycopy、currentTimeMillis/nanoTime → 统一 Clock、getProperty 受限白名单）、Math/StrictMath、Runtime（loadLibrary → 现有 ELF 装载；gc → no-op 记账） | C++ handler |
| P2 · 集合 | ArrayList、HashMap、HashSet、Hashtable、Vector、Iterator 协议、Arrays 常用静态方法 | C++ 容器背衬；迭代器失效语义从简并记账 |
| P2 · 基础值 | Integer/Long/Float/Double/Boolean/Character 装箱 + parse/toString、Enum 基础语义 | C++ handler |
| P3 · 线程 | Thread、Runnable、Object.wait/notify（见 04 §4）、synchronized 语义 | hal 真线程 + monitor table |
| P3 · IO | InputStream/OutputStream 抽象、ByteArray 流、DataInputStream、File/RandomAccessFile → VFS、zip（复用 loader 的 ZIP/Inflate） | VFS/loader 存量 |
| 按需 | Random（确定性种子策略）、UUID（复用现有确定性 UUID）、Locale（复用确定性 Locale）、正则（明确失败起步） | 存量优先 |

## 4. android.* ：存量即首批

capabilities.toml 中已 complete/partial 的框架能力直接换个挂接方式进目录：

- **生命周期与视图**：Activity、GLSurfaceView + Renderer 契约、View 最小面
  （生命周期反转的宿主锚点，见 04 §2）；现有声明式 Activity 生命周期 HLE
  是其实现基础。
- **资源与存储**：AssetManager/InputStream（存量）、SharedPreferences
  （存量 MODE_PRIVATE）、Context 受限面（getAssets/getFilesDir/
  getSharedPreferences/getSystemService 白名单）。
- **身份与设备**：Build/VERSION、SystemProperties、Settings.Secure、
  Telephony、PackageInfo、确定性 Locale/UUID——`runtime.android_legacy_platform_identity`
  全部复用。
- **媒体**：AudioTrack（7 方法存量）、SoundPool（存量 mixer）、
  MediaPlayer（movie request 存量事实，播放仍明确未实现）。
- **其他高频**：Bundle（真实键值 HLE：`<init>`/put*/get*/containsKey/clear——
  Asphalt 6 分析中"注册了类、方法为空"的临时态在本方案下升级为真实语义）、
  Handler/Looper 最小面（消息投递到宿主循环）、Intent（构造 + extras，
  组件解析明确失败）、PowerManager.WakeLock（对接存量 screen policy）。

## 5. 最小反射面

显式枚举，超出即记账失败：

- `Object.getClass`、`Class.getName/getSimpleName`、`Class.isInstance/isAssignableFrom`
- `Class.forName(name)`：应用类（触发 `<clinit>`）与 intrinsic 类可查；
  找不到抛 ClassNotFoundException（真实语义，游戏常用它做能力探测）
- `Method.invoke`/`Field.get/set`/`newInstance` 反射调用族：**初期明确失败**，
  真实命中后按缺口批次评估

## 6. 记账与失败语义

延续"不让失败静默"的项目主题，粒度到方法：

- 未实现 intrinsic **方法**命中：记账（类名 + 方法 + 签名 + 调用点
  class/method/pc）+ 抛 UnsatisfiedLinkError 或结构化 session 失败（按
  调用形态），绝不返回默认值。
- 未实现 intrinsic **类**命中（FindClass/const-class/forName）：区分两种
  语义——`forName` 抛 ClassNotFoundException 是**真实结果**（游戏自己会
  处理）；链接期 super/接口缺失是**装配失败**（明确报错）。
- 缺口聚合经 Agent 接口与 `hle.unimplemented()` 同级查询，输出可直接生成
  下一个实现批次的 WU 素材——这是 M8"先盘点后实现"方法论在 Java 面的
  延续。

## 7. 对照：exact-title 案例在本方案下的形态

| 现状（profile 路线） | dexvm 路线 |
| --- | --- |
| `C2DMAndroidUtils.nativeInit` 需人工发现并补进 profile startup | `GLGame.onCreate` 被解释执行，调用链真实走到 `nativeInit`，jclass 自动缓存 |
| 17 个推送方法需逐个 `[[java.class]]` 声明 | `GetStaticMethodID` 查真实 DEX 方法表，全部自动命中 |
| `HasPushNotification` 返回 0 靠 impl 映射 | 该方法是游戏自己的 Java 代码，解释执行返回真实结果；只有其触到的平台面（若有）走 intrinsic |
| Bundle "注册类但方法为空"，第一个 GetMethodID 爆错 | Bundle 是真实键值 intrinsic，11 个方法真实语义 |
| InAppBilling 需独立 WU 做对象构造 HLE | `Intent`/`Bundle` intrinsic 构造真实对象，游戏侧逻辑解释执行；仅支付动作本身按非目标明确失败 |
| Dungeon Hunter（WU-M8-011 实证）：`TrackingRegisterFirstRun` 缺 handler 即停；license/billing/online 组 13 个 impl id 需逐个反编译定语义 | 这些方法体被解释执行，`IsDemo`/`GetPlayMode` 的返回值来自真实字节码（通常读 SharedPreferences），无需人工判定；billing/browser 只在触到平台动作面时按非目标明确失败 |
| 启动计数 handler 只能做会话内内存计数（跨会话持久化属人工语义猜测） | 胶水方法体自己经 SharedPreferences intrinsic 读写持久值，语义与原 APK 逐位一致 |
