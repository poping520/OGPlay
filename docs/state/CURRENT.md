# 当前状态

更新：2026-09-15。

## 最近进展

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
  消除 `AudioTrack.write` 回压自锁；Angry Birds 已推进至 frame 1072，下一独立首错为
  `nativeUpdate` 当前线程 JNI 引用失效。
- BND-34..39 已闭合本轮 EGL/GLES 核心审计、Java EGL/GLES 桥接、GLSurfaceView、GLU、
  `dl_unwind_find_exidx` 与相关行为缺口；核心名称覆盖不等同 CTS/Khronos 完整认证。

## 当前边界

- **VM/Java**：DexVM 使用受审 API 19 BootDex；普通 Java 状态归字段/数组，JNI 使用真实 VM
  类型关系。文件 IO 通过 Libcore Posix 进入唯一 VFS；完整 mmap/lock、系统 CA 和 Java 长尾
  仍明确失败。
- **Android**：只覆盖当前 APK 直接需要的 Context、Activity、资源、文件、设置及有限服务；
  不运行 Binder system_server、Play 服务、跨包解析、支付或完整 Android 系统。
- **网络**：Apache HTTP Java 类可链接不代表在线可用；socket 仍受 NetworkRuntime policy/
  transport 控制；默认 SSL 状态已闭合，但真实 TLS/PKIX 未实现。`URLEncodedUtils` 尚未纳入。
- **图形/UI**：ANGLE GLES 与 SDL3 窗口输入已接通；完整 framework 排版、Dialog/Web 展示、
  传感器和系统 UI 不在当前范围。

## 验证快照

- Windows Release 仅构建受影响目标；BootDex 当前为 1531 类。
- 既有 BootDex 全链接测试：8602 assertions；能力清单解析通过。
- 图形 catalog/定向回归通过不代表 CTS/Khronos 或全部厂商扩展认证。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
