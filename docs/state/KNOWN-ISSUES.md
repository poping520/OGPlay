# 已知问题与长期限制

本文件记录跨会话仍然有效、但不一定阻塞当前工作的事项。已解决项由 Git 历史保留，
不在这里累积；`CURRENT.md` 只列当前阻塞。

## 执行与平台

- **解释器覆盖**（状态：active；目标阶段：持续）：`cpu.interpreter` 是确定性参考/诊断后端，未覆盖完整 A32/Thumb-2、VFP/NEON、原子访问和异常模型；主执行后端为 Dynarmic，缺失指令必须明确失败。

<a id="rom-replacements"></a>

- **ROM 替换与覆盖**：catalog 仍有整类替换及非 native 方法覆盖，需分阶段移除，新代码不得增加。
  查询：`ogplay-cli dexvm dump-surface`，对照 ROM class_def、own method flags 和 catalog；方法见 [DexVM](../design/dexvm.md#存量-intrinsic-与支持边界)，不在文档维护清单。
- **动态代理与注解**：Proxy 已用 ROM 校验/缓存，但注解仍走受限合成类，私有 throws 字段仅保留形状；统一实现类身份与语义尚未完成。
  复核入口：`tests/dexvm/reflection_tests.cpp` 的代理/注解主题及 `tools/conformance/dalvik_tests.py` 的 004、044 用例。
- **线程与内存**：GC 全周期、直接写 ABA、直接访存卸载 quiescence 和 MCP shutdown 稳定性尚未完整验收；不宣称完整 ARM reservation 模型。
  复核入口：CPU/GC/线程主题与现有游戏关闭路径；Windows 的稳定 payload 路径尚未实跑。
- **Alarm 与位置服务**：闹钟可登记、替换和取消，到期触发与 PendingIntent.send 仍缺；位置更新订阅仍缺能力。
  复核入口：`tests/dexvm/android_pending_notifications_tests.cpp` 的闹钟登记主题；水果忍者的位置订阅路径，当前启动首错另见 CURRENT。
- **网络与 TLS 发布**：默认离线；公开 CA 来源/更新责任和随附 OpenSSL 1.0.1 补丁维护未闭合，loopback 客户端成功不能作为在线发布依据。
  复核入口：`tests/fixtures/tls/loopback_https.py` 与 guest crypto；SSLEngine、server TLS、AndroidKeyStore、完整 PKIX 不支持。
- **文件与 Looper**：完整 mmap/lock、目录树 rename、WAL/跨进程共享以及 NDK Looper callback/非 pipe fd 不支持。
  复核入口：VFS/syscall/guest_looper 主题；临时沙盒的 Dungeon Hunter `/sdcard` 路径仍待相同启动复核。

## 窗口、图形与媒体

<a id="window-media"></a>

- **退出清理**：Tales 菜单关闭仍读 `0x315f0001`；已知为根线程析构链中的失效读取，未证明是宿主 UAF 或原 APK 缺陷。
  复现：默认持久沙盒、1920×1080，菜单关闭；Dead Trigger 的 NativeActivity.onStop native futex 等待、Wild Blood 双 Activity 共享引用 CleanUp 空指针也未关闭。
- **EGL 与窗口验收**：原生 EGL 生产者仍沿单向 pacer；真实 context loss、多窗口/透明 Activity、GLES1 vertex-array 原路径及跨平台未验收。
  复核入口：原生 EGL 游戏帧推进/关闭、窗口 relaunch 和现有场景；Java EGL 双向同步不覆盖原生自建循环。
- **软件图形**：文字整形/双向重排降级，WebP 编码、Parcel blob、硬件 Canvas 等能力缺失；不得把原样类加载当行为支持。
  复核入口：ROM 软件 Canvas/Paint/Bitmap 路径；Wild Blood 安装器、PvZ TermsActivity、NFS17 下载布局仍缺完整双侧画面对照。
- **SurfaceTexture**：Metal 缺 external，MediaPlayer/NDK/EGL window producer 与后端切换尚未完整接入，context-loss 恢复未完成。
  复核入口：SurfaceTexture 公共队列与 Java/SwiftShader 外部纹理采样路径，范围见图形和 Android 契约。
- **音频交叉验收**：raw-resource 完整加载链、手动 step 背压写入的端到端证据、媒体主时钟/影音同步及长时听测尚不完整。
  手动消费与提前帧扣回已实现；复核入口：AudioTrack/AudioOutputPump、MediaPlayer raw resource 和游戏长播；PCM owner GC 既有失败仍需定向复核。
- **WebView 与语音**：仅有有界离线配置/回调和 ROM View 布局，页面、JavaScript、TextToSpeech 未实现。
  复现入口：游戏内网页或语音功能；不能由 WebView 构造成功推断网页可用。

## 应用差异与验证边界

<a id="title-validation"></a>

- **应用模拟核心差异**：My Boy! 自动 ARM test 225 和 SRAM test 004 与同 APK API25 真机一致；属于当前应用结果，不修改核心或伪造通过画面。
  复现：同一测试 ROM 和 APK；API19 真机、更广 ROM 与跨平台仍未验。
- **文件 URL 与并发画面**：My Boy! 金手指配置重载仍有应用捕获的 IOException；并发压力下偶发局部/黑色半帧，根因未定位。
  复现：金手指导入/重载、并发运行后进入声音菜单；独占 basic 三轮通过不关闭此项。
- **游戏路径**：各游戏当前首错、到达点及交互/退出待验继续以 [CURRENT 游戏进度](CURRENT.md#游戏进度) 为准。
  Tales 的尺寸裁剪/偶发黑屏、Bad Piggies JNI 字符串归属、Sacred Odyssey 黑色 3D 区域等尚未完成正式 title gate。
- **回归与平台**：现有测试并非全绿；受影响主题的已有失败、dalvik 输出差异和场景失败未全部闭合，不能把历史局部通过当当前全量结果。
  复核入口：CURRENT 未闭合问题与一致性工具；多数近期真实游戏验证仅 macOS，Windows/Linux、Android 退出/后台/方向仍有缺口。
- **前端交互**：Linux GUI 未接入；macOS 部分设置/挂载提示/运行分类和 Windows 新 GUI 仍缺 App 内实际点击复核。
  复核入口：导入→设置→启动→Dashboard→移除及 input look/相对鼠标；跨窗口拖放与原生文件选择验收尚不完整。
- **游戏标记**：内置颜色/自定义标记及按标记筛选尚未实现，当前仅有自动运行分类。
  复核入口：GUI 游戏库与 CLI `library list`；不把暂缓方案的数据格式当现行契约。
- **外部停滞取证**：协调器和快照已实现，可杀死真实停滞子进程的跨进程 OS 触发限时落盘仍缺验收。
  复核入口：按排障手册构造受控停滞并从独立进程触发，不能用普通 diag.snapshot 替代。

## 构建与依赖

- **Linux 窗口依赖**（状态：planned；目标阶段：待定）：Linux 无显示环境可使用 SDL Unix-console；可见窗口构建仍需要 X11 或 Wayland 开发依赖。
- **hosted CI**（状态：active；目标阶段：持续）：hosted CI（`.github/workflows/ci.yml`）已在 GitHub Actions 运行；最近 3 次 main 推送（2026-10-11、10-12）均在约 1–2 分钟内失败，原因未诊断。本地与测试机结果不替代 hosted CI 状态。
- **doctest 配置警告**（状态：upstream；目标阶段：持续）：doctest 2.4.11 在 CMake 4.x 配置期产生上游旧 policy deprecation warning，不影响 warnings-as-errors 编译与测试。
