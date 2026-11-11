# P3 · ROM 进程、窗口与生命周期

P3 将生产会话切换到原版 Android 4.4 的 ActivityThread、主 Looper、PhoneWindow、
WindowManagerGlobal 和 ViewRootImpl。会话通过进程内 AMS 提交生命周期请求。WMS
管理单个逻辑显示、窗口布局与层级、焦点和手势捕获。ROM ViewRootImpl 经 Skia 软件
Surface 提交像素，GLSurfaceView、NativeActivity 和视频通过同一 WMS 合成入口发布帧。
全屏不透明 EGL 层满足覆盖条件时由 FrameService 借用像素。IME 文本和 composing 走本地
IMMS/IInputContext，物理按键仍由 ViewRootImpl 分发。

删除 JavaTraversal、UI 投影、宿主 modal、旧 Activity 调度和对应 BootDex 替换。
WebView 保留离线加载失败回调，作为普通 View 参与 ROM 布局。服务边界为当前进程内的
Binder/AIDL。系统 UI、多显示、跨进程 Binder、TextureView 和硬件 UI 加速超出范围。

## 运行期配置与帧同步

SDL SetOrientation 只设置方向提示。逻辑显示由前台 Activity 的固定横竖屏请求和 Activity
切换触发宽高交换。密度和任意逻辑尺寸来自启动选项。AMS 经 IApplicationThread
发布全局配置，按各 Activity 的配置差异与 getRealConfigChanged 判定 relaunch 或
onConfigurationChanged。ROM ActivityThread 管理保存状态和重建，ROM ResourcesManager
更新 Resources 和 DisplayMetrics。WMS 先取消当前触摸捕获，再更新显示尺寸并通知窗口
relayout。窗口增删和 EGL backing 退役沿 Activity 切换路径处理，前端以已呈现的新尺寸映射触摸。

会话推进下一次 Clock 前等待已绑定 EGL10 窗口的 Java 生产者完成 swap、进入可观测阻塞或解绑退出。
执行锁观察、Java 等待状态及 native futex waiter 用于识别阻塞。主线程阻塞时允许 GL 完成
生命周期握手。实时前端在等待时继续处理宿主事件，手动步进等待主队列空闲和当步绘制。
等待超过 30 秒时记录生产者线程与已等待时长，每次停滞只记录一次，放行 Clock。
超时生产者在下一次 swap 或可观测阻塞后恢复双向同步。原生线程保留单向 EGL 节流。
契约见 [DexVM Android](../../../src/runtime/integration/dexvm_android/MODULE.md)
与 [session](../../../src/session/MODULE.md)。

## 验证入口

- Release 的 `ogplay_tests` 用 `--source-file` 选择
  `*rom_window_integration_tests.cpp`、`*rom_activity_service_tests.cpp`、
  `*rom_window_system_tests.cpp`、`*rom_window_services_tests.cpp`、
  `*native_activity_process_tests.cpp` 和 `*android_native_activity_io_tests.cpp`。
  APK 夹具见 [tests/fixtures/ui](../../../tests/fixtures/ui/README.md)。
- 第三轮也复跑 egl_facade、scheduler、choreographer_frame_source、property_animation、
  lifecycle 和 android_value 相关文件。四个新增用例验证 relaunch 的状态和首帧、处理配置
  时原实例的 relayout、连续 swap 与 Clock 的对应关系、WHEN_DIRTY 空闲时推进。
- 本地验收证据位于 `.local/analysis/p3-window/round3/`。既有 UI 和测试替代证据位于
  `.local/analysis/p3-window/round2/`。
- 第三轮补充在 `build/release` 构建 CLI 和测试。格式化后仅复跑
  rom_window_integration、rom_window_services、rom_window_system、rom_activity_service、
  native_activity_process、egl_facade 和 session/lifecycle。新增用例覆盖 Activity 和 Dialog
  的 finish 与会话退出，以及实时和手动模式下 EGL 长帧放行及 swap 后恢复同步。

## 最终验收

| 项目 | 结论与证据 |
| --- | --- |
| 1. scope | 通过。409 项恢复 ROM，15 项移除，147 项按范围排除。最终 `round3/scope.tsv`。 |
| 2. 测试 | 补充修复格式化后 7 个指定文件 53 例全过，见 `round3/fix/tests-after-format.log`。第三轮原验收为 19 个文件 158 例全过，其余沿用阶段 B，既有 DVM-84 失败见 `round3/test-comparison-final.txt`、`test-failures-final.tsv` 和 `round2/test-coverage-map.tsv`。 |
| 3. dump-surface | 本轮无新增差异。与 P2 有 645 个类的差异。原版类恢复、替换和覆盖移除、本地服务/native 绑定及 ROM 依赖归类沿用第一轮和阶段 B。严格的范围条件仍保留阶段 B 已说明的范围外差异。见 `round3/surface-p2-diff.tsv`、`round1/final-surface-diff.txt`、`round2/stage-b/surface-diff.txt` 和 `surface-outside-scope.tsv`。 |
| 4. 游戏 | 第三轮 20 款的 900 帧达成数为 P2 / 阶段 B / 第三轮 7 / 12 / 12。补充修复只复跑 Wild Blood、Dead Trigger 和 Asphalt 5，各呈现 900 帧。Wild Blood 的退出焦点异常消失，两个退出缺口见下文。原矩阵见 `round3/games-comparison.tsv` 和 `games-first-errors.md`，补充证据见 `round3/fix/game-results.jsonl`。 |
| 5. UI | 沿用阶段 B 的 `round2/ui-compare/notes.md`。本轮补拍 relaunch 和原实例 relayout 的 320×400→400×320，结合像素断言核对新帧边界。阶段 B 的三组历史画面缺口仍在。见 `round3/ui-rotation/`。 |
| 6. 性能 | Angry Birds 1280×720 各两次交替，每次呈现 4000 帧。活跃帧中位数 0.780→0.683 ms（−12.5%），低于 10% 回退门槛和 WU-PERF-17 的 1.300 ms，四次全屏 EGL 借用均命中。帧指标沿用阶段 B 活跃计时，实时等待位于空闲分支。常驻状态变更后三次交替启动的峰值 RSS 中位数 526.03→515.52 MiB，单帧热启动 1.74→1.79 秒。P2 的历史中位数为 512.23 MiB、1.74 秒。见 `round3/frames-summary.json`、`startup-summary.json` 和 `performance-summary.txt`。 |
| 7. 校验与构建 | 补充修复使用 `build.sh` 重建窗口夹具，格式化后 Release CLI 和测试构建通过。见 `round3/fix/fixture-build.log`、`build-after-format.log` 和 `capabilities-check.log`。第三轮原校验与 macOS x64 构建见 `round3/*-check.log`、`x64-build-final-active-producer.log` 和 `x64-file.txt`。 |
| 8. 文档 | 已更新受影响的模块契约、能力账本、当前状态及本任务单。 |

### 第三轮补充复跑

Wild Blood 安装器启动了第二个 Game Activity。旧会话退出先排队销毁已停止的后台实例，
其 onDestroy 清空共享 Activity 引用。前台实例的主 decor 仍挂载，直到前台 pause 确认后，
AMS 的 Stop 隐藏前台 token，WMS 经 IWindow 将失焦消息排入该 ViewRootImpl 的 Handler。
后台实例的 removeViewImmediate 只移除自己的 decor，前台 ViewRootImpl 的 mAdded 仍为 true，
因此消息交给了前台 decor 树中的 GameGLSurfaceView。补充修复先隐藏结束的 token 再调度
生命周期消息。会话退出先处理整栈的 token，避免后台销毁抢在前台失焦之前。
取证日志 `round3/fix/wild-blood.log` 的窗口 5 属于前台 token 199881，
第一次销毁属于后台 token 7430。失焦回调在两次 onDestroy 之前完成。

| 游戏 | 与 `round3/games` 对比 | 本次退出 |
| --- | --- | --- |
| Wild Blood | 原记录停留安装器。本次越过安装器进入 Game GL 窗口，与 `games-final` 到达点一致。900 帧，运行无首错。 | 原失焦回调空引用消失。第二个 Game 的 onDestroy 调用共享引用上的 CleanUp() 时抛空指针，退出 1。 |
| Dead Trigger | 同达 Unity 渲染，900 帧，运行无首错。 | 同样达到关闭期限，退出 1。快照显示主线程在 NativeActivity.onStop 的 native 回调中等待 futex，EGL pacer 已关闭。 |
| Asphalt 5 | 同一启动配置呈现 900 帧，运行无首错。步数/呈现帧从 `games` 的 34544/900 降到 904/900，与 `games-final` 一致。 | 正常退出 0。 |

三款游戏各运行一次，Wild Blood 使用临时窗口和 AMS 取证日志。探针移除后重新构建并复跑指定测试。

## 剩余缺口与 P4

- My Boy! 的启动已进入 ROM ActivityThread，当前首错是创建 Activity 时 ActionBarView
  布局 inflate 失败。最内层是 LayoutInflater 反射调用 ActionBarView$HomeView 构造器时的
  IllegalAccessException。P4 从该堆栈和 ROM 路径继续。
- Fruit Ninja 的位置订阅仍停在 `requestLocationUpdates`。定位服务的补齐不属于 P3。
  `Character.getDirectionality` 和 `CharBuffer.charAt/length` 保留在现有 libcore 替换类中。
- Wild Blood 的前台实例在后台实例清空共享引用后调用 CleanUp()，退出仍抛空指针。
- Dead Trigger 关闭卡在 NativeActivity.onStop 的 native futex 等待，与 Wild Blood 的焦点顺序不同源。
  快照位于 `round3/fix/dead-trigger-diag/`，等待的唤醒方尚未确认。
- DVM-84 的 PCM owner GC 回收是既有测试失败。阶段 B 缺少 Wild Blood 安装器表单、
  PvZ TermsActivity 和 NFS17 下载布局的完整双侧画面。
- 原生 EGL 生产者的 Clock 同步、真实 context loss、多窗口和跨平台运行仍待验收。
