# 当前状态

更新：2026-09-16。

## 最近进展

- [DVM-173](../tasks/dexvm/DVM-173.md) TLS-01/02 已验收：OGPlayJSSE 发布 PKIX
  TrustManagerFactory、guest libcrypto 路径验证、只读 AndroidCAStore 与测试 CA 包；
  `engineInit` 经 `SecureRandom`/`NativeTls.seed` 播种 guest libssl，`SSLSocket` 走
  `NativeTls.connectRaw` 原始通道，loopback TLS 1.2 握手、HTTPS GET `200 tls-ok`、
  layered `autoClose=false` 与 RSA mTLS 双解释器通过。默认网络仍关闭。TLS-03 测试 CA
  包不是公开信任列表，payload `libssl.so` 仍为已停止维护的 OpenSSL 1.0.1，不发布公开
  互联网 HTTPS。BootDex 1636 类。

- KeyStore [DVM-172](../tasks/dexvm/DVM-172.md) 约定范围内验收完成：API19 原版公开 API 与
  自有 Java Provider/SPI/BKS codec 进入 1572 类 BootDex；标准及历史 3DES PBE、AES RAW、
  RSA/EC PKCS#8、native token 生命周期、v0/v1/v2、资源上限与失败清理完成。Android 4.4.4
  mixed store 双向互操作通过，正式发布独立 `OGPlayKeyStore`，默认类型为 BKS；CallbackHandler、
  同 store 双线程、磁盘跨 session 重载及应用沙盒同路径读写隔离通过，KS-01..09 闭合。
- [DVM-171](../tasks/dexvm/DVM-171.md)：固定 API 19 原版 `NativeCrypto` 及精确依赖进入
  BootDex，删除 C++ 手工类壳；原版 217 个 native 中 26 个沿用既有后端、1 个为初始化适配、
  190 个明确失败。资源登记归 VM，私有验签移出原版 ABI；初始化失败传播和 payload 来源
  固定均已受检。BootDex 现为 1542 类。
- API 19 bundled OpenSSL payload 现包含同一 AOSP 4.4.4_r2.0.1 `aosp_arm-user` 构建的
  `libcrypto.so` 与 `libssl.so`；TLS/PKIX 的上层能力边界保持不变。
- [KeyStore 通用能力规划](../design/dexvm/13-keystore.md)按 ADR-0064 实施，约定范围已验收；TLS/PKIX、系统 CA、
  AndroidKeyStore、DSA 与签名生成仍明确不在本次范围。
- Angry Birds 无 Profile 兼容链已依次越过 `dl_unwind_find_exidx`、location 薄层、
  `Context.getFileStreamPath`、旧 Dalvik JNI Object-call/void、AudioTrack notification getter、
  Flurry boot-age、Settings moved-key 和 Context calling/self 权限检查。
- [DVM-161](../tasks/dexvm/DVM-161.md)：Settings System/Secure/Global 公开语义迁入 AOSP
  BootDex，C++ 仅保留私有 NameValueCache 有界存储。
- [DVM-162](../tasks/dexvm/DVM-162.md)：Context calling/self 与 enforce 权限家族共用
  Manifest grant；无 Binder IPC 时 calling-only 保持 denied。
- API 19 ext.jar 的 `org.apache.http.entity` 全部 12 类已并入 DVM-149 BootDex 范围；当前
  BootDex entity 包完整入集。
- [DVM-164](../tasks/dexvm/DVM-164.md)：URLConnection/HTTP/HTTPS 请求状态和默认 SSL
  状态迁入 AOSP BootDex，C++ 只保留外部 I/O 边界；NetworkPolicy 默认 disabled。Angry Birds
  已越过原 getter 缺口。当前 BootDex 1531 类，全链接 8602 assertions 通过。
- AudioTrack position callback 回填 PCM 后不在唯一 mixer 消费线程继续同步补发过期通知，
  消除 `AudioTrack.write` 回压自锁。
- [DVM-165](../tasks/dexvm/DVM-165.md)：按 KitKat 为 targetSdk 1..13 补齐旧 JNI direct-reference
  app-bug 兼容，严格模式不变且违规用法输出明确 warn；Angry Birds 无 Profile 已越过原
  `nativeUpdate` JNI 引用失效，运行 1258 帧后由游戏正常返回 false。
- [DVM-166](../tasks/dexvm/DVM-166.md)：补齐 API 19 KeyguardManager 有界 facade；当前桌面状态
  固定为未锁屏，三项查询统一经可替换 provider，为后续宿主行为接入保留单一边界。
- [DVM-167](../tasks/dexvm/DVM-167.md)：按 API 19 补齐 ViewGroup width/height addView 重载，
  RelativeLayout 虚派生成自身 BootDex 参数并复用唯一 attach 路径；Angry Birds 下一独立首错
  已推进到 GLSurfaceView `BaseConfigChooser` 的 `No config chosen`。
- [DVM-168](../tasks/dexvm/DVM-168.md)：Java EGL10/EGL14 按 native handle 回填多配置，
  对桌面 RGBA8888 超集配置发布 RGB565 guest 查询投影；Angry Birds 已越过 `No config chosen`
  并进入 `MyRenderer.onSurfaceChanged`，下一独立首错为缺少 `javax.crypto.Mac`。
- [DVM-169](../tasks/dexvm/DVM-169.md)：API 19 原版 `Mac/MacSpi` 与 Conscrypt `OpenSSLMac`
  精确闭包进入 BootDex，AOSP 签名 NativeCrypto 接入 guest libcrypto；Angry Birds 已越过原类缺口，下一独立问题为
  GLThread 调用 `runOnUiThread` 时没有 Looper，并残留 pending exception。
- [DVM-170](../tasks/dexvm/DVM-170.md)：`Activity.runOnUiThread` 按 root/worker context 分流，
  worker 进入主 Looper；guest native token 在 root JNI detach 前统一释放。Angry Birds 已越过
  Handler/pending-exception 连锁故障，下一独立缺口为 `java.security.KeyStore`。
- BND-34..39 已闭合本轮 EGL/GLES 核心审计、Java EGL/GLES 桥接、GLSurfaceView、GLU、
  `dl_unwind_find_exidx` 与相关行为缺口；核心名称覆盖不等同 CTS/Khronos 完整认证。

## 当前边界

- **VM/Java**：DexVM 使用受审 API 19 BootDex；普通 Java 状态归字段/数组，JNI 使用真实 VM
  类型关系；targetSdk 1..13 单独启用 AOSP 旧 JNI direct-reference 兼容并警告。文件 IO 通过
  Libcore Posix 进入唯一 VFS；Mac/MacSpi 与 HmacSHA1 SPI 来自原版 BootDex/Conscrypt，当前
  支持集合经 AOSP 签名 NativeCrypto 接入 guest libcrypto；完整 mmap/lock、系统 CA 和 Java
  长尾仍明确失败。
- **Android**：只覆盖当前 APK 直接需要的 Context、Activity、资源、文件、设置及有限服务；
  Keyguard 当前发布无锁屏事实并预留宿主状态 provider；
  不运行 Binder system_server、Play 服务、跨包解析、支付或完整 Android 系统。
- **网络**：Apache HTTP Java 类可链接不代表在线可用；socket 仍受 NetworkRuntime policy/
  transport 控制。TrustManager/默认 CA 已真实验证；loopback 客户端 TLS/HTTPS 已闭合，
  默认网络关闭，不发布公开互联网 HTTPS。`URLEncodedUtils` 尚未纳入。
- **图形/UI**：ANGLE GLES 与 SDL3 窗口输入已接通；完整 framework 排版、Dialog/Web 展示、
  传感器和系统 UI 不在当前范围。Java EGL config wrapper 保留真实 native handle，并可对宿主
  颜色格式超集投影 guest 显式请求位数。

## 验证快照

- Windows Debug 仅构建受影响目标；BootDex 当前为 1636 类，DEX
  `9e3c6627f72a96905a140d5214ca682c078e77cbccdc5f8bfdb89821ece6c42c`。
  DVM-173 定向 `TLS*` 5 cases / 1363 assertions 双解释器通过。guest JNI
  `9b8fda764c526284189c86f778b6ade5bf4784d4fcda1bf81d329591127fc7d3`。
- JNI 定向回归 54 cases / 1608 assertions；DVM-166/167 双解释器分别通过 58/44 assertions；
  DVM-168 EGL10/EGL14 定向回归 6 cases / 314 assertions；Angry Birds 已进入 renderer 的
  `onSurfaceChanged`；DVM-169 双解释器加密回归 1 case / 1646 assertions；DVM-170 双解释器
  1 case / 16 assertions；真实 APK 已推进至 `java.security.KeyStore` 缺口。
- DVM-172 最终定向回归：KeyStore API、BKS/crypto/native、BootDex 全链接、证书及
  DVM-108/109 共 5 cases / 15722 assertions 通过；包含回调、共享 store 双线程、磁盘重载
  和沙盒隔离。intrinsic layout、BootDex builder self-test、payload current 共 3/3 通过。
- 既有 BootDex 全链接测试：8602 assertions；能力清单解析通过。
- 图形 catalog/定向回归通过不代表 CTS/Khronos 或全部厂商扩展认证。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
