# P4 · 原版模拟器应用的窗口、反射与宿主边界

基线：`main` / `021b9b090`。本轮不提交。运行平台为 macOS arm64，API19 原版 ROM
framework；不修改 ROM Java 类、不增加非 native intrinsic，也不增加游戏分支或 Profile。
本地 `framework-res.apk` 和测试 ROM 不入库，不修改 `.gitignore`。

## 范围与当前结论

验收应用为 My Boy! 1.8.0，`com.fastemulator.gba`，version code 90048，armeabi。
APK SHA-256 为 `8d56fc34e0e35e0ef3f9e195bc9c353f584e5e876db6b52848b616ae82605f81`。
原版 `libgba.so` 身份见场景文件，不修改应用和模拟核心。

- M1 完成：ActionBar 标题、图标、overflow/Refresh，目录进入、返回、文件列表滚动及
  根目录 Back 退出均实测。800×360 的滚动补验正常退出，见 `browser-scroll-result.json`。
- M2 完成：GLES/ANGLE 合成、虚拟按键、SDL 键盘事件、hello 固定画面、声音测试均实测。
  `arm.gba` 的验收口径为“与同 APK 真机结果一致”：自动核心在 OGPlay 和获授权安装的
  NX595J（API25）上都显示 Failed test 225，属于应用自带模拟核心的缺陷，不是 OGPlay 的差异。
  解释器核心在 OGPlay 显示 224，真机未对照。
- M3 完成：设置持久化、应用对话框/取消、IMMS 文本与退格、即时存档/读档、SRAM 落盘和重启读取、
  应用请求的横竖屏切换均有实测证据。

## 通用能力修复与首错

| 到达时的现象 | API19 对照及原因 | 修复与同路径证据 | 受影响范围 |
| --- | --- | --- | --- |
| LayoutInflater 构造 `ActionBarView$HomeView` 抛 IllegalAccessException | DexVM 在成员检查之外检查声明类可访问性；Dalvik `dvmCheckMethodAccess` / `dvmCheckFieldAccess` 只检查成员标志 | `reflection.cpp` 按实际虚调用目标检查成员，移除方法的额外 protected receiver 限制；字段保留 Dalvik 特有的 receiver 检查；`Class.newInstance` 单独保留类访问检查。`00-baseline.log` → `01-reflection.log`：原错消失，启动进入浏览器 | 所有跨包访问非 public 声明类的 public 成员，以及 protected 反射方法调用；不是专用布局修复 |
| EmulatorService 建通知时拒绝 Activity PendingIntent | 本地 AMS 已有 broadcast/service 令牌，但拒绝 API19 类型 2 的单 Activity 令牌 | 复用既有身份、Intent 快照、flags 与 GC 保活机制。`03-explore.log` → `04-explore.log` / `04-rom.png`：原错消失，进入 240p 画面 | 所有使用单 Activity 通知入口的应用；send、多 Activity task stack 等仍不支持 |
| 启动说明对话框被底下 Activity 遮住，列表直到 Refresh 才可见 | `onResume` 先 show Dialog，ActivityThread 随后 add 基础窗口；API19 WMS 把同 app token 的 BASE_APPLICATION 放在附加窗口下面，宿主却仅按创建次序排层 | WindowSystem 独立维护根窗口次序，保持子窗口相对层级。`03-intro.png` → `10-intro.png`、`10-browser.png`：说明框和确认后列表正常 | 任何在基础 decor 附着前打开对话框的 Activity；其他 app token 次序保留 |
| 设置返回、存档返回后 GLES 画面变粉紫/黑色 | 隐藏 relayout 立即把旧 Surface 置无效，ROM SurfaceView 因 `isValid=false` 跳过 surfaceDestroyed；应用未清除旧 EGL 纹理。API19 `RELAYOUT_DEFER_SURFACE_DESTROY` 保留旧 Surface 直到 performDeferredDestroy | 本地 WMS 保存延迟销毁 Surface 引用及有效性，按完成调用退役；替换和 remove 释放旧引用。`13-resumed.png` → `15-resume.png`、`15-after-save.png`、`15-after-load-stable.png`：同路径画面恢复 | 使用 SurfaceView/GLSurfaceView 的暂停恢复及 Activity 往返；ROM fixture 验证 destroyed 回调时 Surface 仍有效且保留 EGL context |
| 切到竖屏可显示，但随后打开设置报 recycled Android boundary frame layout does not match | P3 允许合成帧交换宽高，FrameService 回收却仍强制启动尺寸；Android 配置变化后的帧必须能正常退回宿主 | 回收按帧自身有界尺寸和 RGBA 长度校验，字节存储由下一生产者调整，允许在途旧方向帧归还。`final-manual-real-time.log` → `rotation-final.json`：横→竖→设置→横→设置，closed / exit 0 | 所有配置旋转后的空白/软件帧回收；畸形帧仍明确失败 |

另修正 `setServiceForeground` 的展示边界：它此前是被应用捕获的 UOE，不是 uncaught 首错。
本地服务核对真实 token/ComponentName，按 Android 对非零 id 的空通知抛 IllegalArgumentException；
有效请求仅以 `RecordUiDegraded` 警告忽略通知显示，不伪造系统前台服务或通知集合。
验证见 `16-pending-tests.log`、`rotation-final-real-time.log`。

Dalvik 对照使用固定本地 AOSP：`dalvik/vm/oo/AccessCheck.cpp`、`vm/interp/Stack.cpp`、
`vm/native/java_lang_Class.cpp`、`vm/native/java_lang_reflect_Field.cpp`；窗口对照为
`WindowManagerService.addAppWindowToListLocked` 和 SurfaceView 的 deferred-destroy 路径。
NX595J 对照仅证明同一应用在 API25 的行为，不冒充 API19 真机验证。

## 验收入口与场景

所有本轮产物在 `.local/analysis/p4-myboy/`，正式场景为
[`my_boy.basic.scenario.toml`](../../../tests/scenarios/my_boy.basic.scenario.toml)。
使用原 APK、无 Profile、无 survey、独立新沙盒、1280×720 / 320 dpi，外部目录挂为 `/sdcard`。

```sh
python3.12 tools/run_scenario.py \
  --scenario tests/scenarios/my_boy.basic.scenario.toml \
  --profiles data/profiles --ogplay-cli build/release/ogplay-cli \
  --fixture "my_boy.apk=$HOME/Library/Application Support/OGPlay/library/com.fastemulator.gba/game.apk" \
  --fixture "my_boy.roms=$PWD/.local/roms/gba" \
  --surface-width 1280 --surface-height 720 \
  --evidence-dir .local/analysis/p4-myboy/gate-1
```

每轮使用新证据目录，分别为 `gate-1`、`gate-2`、`gate-3`。场景 63 个 checkpoint，
13 个稳定画面 SHA-256 golden；其余 checkpoint 的 frame/no-fault 是进展证据，截图由人工判读。
路径包括初次说明框、浏览器/overflow/Refresh、子目录、hello、即时保存/加载、退出确认、
240p 菜单方向操作、声音测试菜单及播放、设置和 Stretch 修改。
键盘、SDL 文本、重启持久化及方向切换采用补充实测；现有 runner 没有这些 action，未扩充 runner。
场景通过不等于 ARM 测试通过，也不等于所有 M1～M3 条件已自动断言。

关键截图：`gate-1/{browser,overflow,rom_directory,hello,loaded_hello,test_suite,suite_menu,`
`sound_frequency_menu,preferences,stretch_enabled}/frame.png`。
`gate-before17-*`、`gate-before-goldens-*` 为收敛期间的旧轮次，不作为最终三轮结果。

最终构建与上述 13 个 golden 的场景在独占运行下连续三轮通过，均为 `passed`、
`survey=false`、63 checkpoint、clean shutdown，汇总见 `gate-summary.json`。
并发执行 20 款回归时曾有一轮 `sound_frequency_menu` 黑色/局部画面，golden 未满足并在 6 秒
预算处失败（没有 guest fault），保留于 `gate-concurrent-goldens-2`。未查明该并发不稳定的根因，
不能把最终独占三轮通过解释为并发压力问题已修复。

## 输入、音频和存储补充证据

- 虚拟方向与 A/B 经原版 ViewRootImpl/SurfaceView 触摸路径操作 240p 菜单。
  `09-keyboard-{start,right,page2}.png` 记录键盘方向事件推进菜单。
  本地 `sdl_input.dylib` 只在诊断进程中注入真实 SDL key/text event，经现有宿主输入边界进入
  KeyEvent/IMMS；没有生产专用接口或游戏映射，不声称物理键盘人工操作。
  最终构建另用既有 input.toml 机制将 J/K 映射通用 Android BUTTON_A/B（本地
  `gamepad-input.toml`，无游戏身份或坐标），方向键选 Sound test、K 返回、J 再进入，见
  `keyboard-final-{b-return,a-enter}.png`；`keyboard-final.json` 正常关闭，无 fault。
- Cheat name 对话框聚焦后，SDL 文本输入 `P4-input`，Backspace 后为 `P4-inpu`，见
  `12-text-focused.png`、`12-text-erased.png`。缺少合法金手指代码时应用自己的错误框正常显示，
  最后 Discard，未伪造或保存无效代码。
- Stretch 修改保存在 `09-sandbox/.../internal/shared_prefs/com.fastemulator.gba_preferences.xml`，
  重启后 `final-manual-prefs-persisted.png` 仍选中。CPU interp 是排查 ARM 差异时人工选择的设置，
  正式三轮均从新沙盒使用默认自动核心。
- `rotation-final-{portrait,settings-after-portrait,landscape,settings-after-landscape}.png` 验证应用
  screenOrientation 偏好触发 P3 的配置派发路径；`rotation-final.json` 无 fault，正常关闭。
  单纯宿主窗口 resize 不改变 guest 方向，未将它作为旋转证据。
- hello 固定画面和保存/加载后的画面使用相同 golden。SRAM 实际写入
  `09-sandbox/com.fastemulator.gba/sdcard/myboy/save/sram.sav`，32768 字节，
  offset 32/64/96 为 1；重启读取命中见 `final-manual-vfs.json` 的 found 项。
  SHA-256 `5731f3a2df4128e6c260d83184c2a1552f3e7e033634e8ad2289c68167a52420`
  与手机拉回的 `native-reference/sram.sav` 完全一致。即时存档 Slot 1 的保存/读取另由场景覆盖。
- 音频未用 runner 断言：SDL disk driver 取得实际 48 kHz / stereo S16 输出，虚拟 A 播放
  1000 Hz 测试音，非静音约 0.400 秒，峰值 8192、RMS 5629、测得 999.95 Hz。
  AudioTrack consumed frames 2103217→2215172，采样窗口内 underrun 3→3，既有 3 次发生在此前。
  证据为 `audio-evidence.json`、`09-before-tone.json`、`09-during-tone.json` 和
  `1000hz-output.wav`。这是有界短音及菜单同步证据，不声明长时间音频稳定性验收。

## 测试、制品与性能

- 最终所有改动 C/C++ 文件均执行 `clang-format -i`，仅构建 `ogplay-cli`、`ogplay_tests`。
  窗口 fixture 使用既有 build.sh 重建，跟踪 APK 同步更新；不移植 CTS/JUnit。
- 定向文件与 P3 窗口组共 22 个测试文件、197 用例、53778 断言通过，见
  `final-test-results.json`。新增测试只覆盖成员访问、Activity PendingIntent、通知降级、
  根窗口次序、延迟 Surface 销毁回调及旋转帧回收；其余复用既有测试。
- `tools/capabilities.py check`、`tools/validate_android_payload.py --root data/android/19`、
  Scenario schema 校验通过。BootDex 配方/制品未变化，不运行 build_bootdex。
- 场景三轮、20 款游戏最终比较及交替帧时间结果见下方收尾记录。

### 帧时间

因修改窗口排序和帧回收，按要求以真实 `021b9b090` 源码构建的独立 CLI 与最终构建，
依次 baseline-1、final-1、baseline-2、final-2 独占运行 Angry Birds 1280×720，
每次 `OGPLAY_FRAME_BENCHMARK=1`、新沙盒、4000 呈现帧，四次 exit 0。
活跃帧中位数分别为 0.661250 / 0.668625 / 0.683375 / 0.682666 ms，
两次测量的中位数为 0.672313→0.675646 ms，增加 0.50%，通过不超过 10% 的门槛。
源码临时恢复构建后已还原工作区并重建最终目标，未创建提交；命令和结果见
`build-baseline.py`、`baseline-021b9b090/revision.txt`、`finish-validation.py`、`frames-summary.json`。

### 20 款有界回归

最终构建沿 P3 第三轮相同命令逐款使用 `--exit-after-frames 900`，80 秒预算后有界关闭。
20 款均执行；13 款实际达到 900，P3 完整轮为 12 款，新增 My Boy!；既有达到 900 的游戏均保留。
保卫萝卜仍在 2 帧 JNI 返回类型错误，小顽皮仍缺 Character 方法，水果忍者仍缺定位订阅，
Sonic Dash 和割绳子仍在既有启动首错。GTA3/GTASA 为首启模态有界等待，分别呈现 19/55 帧，
没有操作弹窗以改变 P3 输入条件，不能称其达到 900 帧。既有退出错误另保存在各 run report，
没有因呈现达到 900 而记为 clean shutdown。

比较以指定 `round3/games` 为先；该目录缺 Angry Birds、Sonic Dash、Monument Valley、
割绳子四份报告，明确改用同轮 `games-final`，并同时核对其完整 20 款基线。
GTA3/GTASA 的指定旧报告自身 unfinished，不能据此作完整退出一致性结论。
按到达点、首错和完整轮的 900 帧集合比较无新增回退，见 `games-comparison.json`、
`games-comparison.md`、`games/runs.jsonl`；不把这次冒烟提升为 20 款游戏兼容验收。

## 剩余边界

1. ARM 测试按同 APK 真机结果验收。[gba-tests 说明](https://github.com/jsmolka/gba-tests)
   规定显示第一个失败编号；自动核心的 [test 225](https://github.com/jsmolka/gba-tests/blob/master/arm/data_processing.asm)
   属于 PC 操作数/寄存器移位语义，是应用模拟核心的已知缺陷。不修改模拟核心、不伪造通过画面。
2. [SRAM test 004](https://github.com/jsmolka/gba-tests/blob/master/save/sram.asm) 是 halfword load
   检查，同一 APK 在手机和 OGPlay 同样失败；前面的字节写入及实际保存/重启读取已单独验证。
3. 金手指配置重载的 file URL 路径仍有被应用捕获的 IOException，未验完整金手指导入/重载。
   Play、广告、支付、蓝牙联机及进程外系统展示不扩展；没有用成功返回冒充范围外服务。
4. 并发压力下声音菜单局部/黑色画面的根因未定位；正式独占三轮通过不能关闭这项记录。
5. API19 真机、Windows/Linux、长时间连续音频与更广 ROM 兼容性未验；20 款仅为有界首错/呈现回归。
