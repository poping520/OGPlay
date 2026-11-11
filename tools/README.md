# 开发工具

## Windows 发布包

在 Windows PowerShell 中执行（需 CMake、Node/npm、Visual Studio C++ 工具及已准备的 WebView2 SDK）：

```powershell
./tools/package_windows.ps1
```

使用 `windows-msvc` 构建 Release 和 Windows WebUI，再通过 CMake 的 Runtime 组件导出。
产物默认位于 `out/releases/`：免安装 ZIP、SHA-256 校验文件和解压目录。
EXE、依赖 DLL 与 data 位于包根目录；附带匹配构建工具链的 x64 VC++ Release DLL、许可证和逐文件哈希清单。
打包前检查所有原生模块的 x64 架构及导入依赖。WebView2 使用系统 Evergreen Runtime；初始化失败时提供官方安装入口。
`-SkipBuild` 仅用于复用刚完成且已验证的 Release 构建，`-CMake` 可指定 CMake 路径，
`-OutputDirectory` 可指定新的输出目录；已有同名产物时拒绝覆盖。脚本只生成本地包，不创建标签或发布 GitHub Release。

## 应用图标

`assets/icons/ogplay.png` 保留最初原图，供 Windows 图标使用；`assets/icons/ogplay-macos.png` 是 macOS 图标源图，使用已调整好的 1024×1024 图案、留白及透明度。
Windows 生成 ICO 时裁掉原图完全透明的外围，保持图案比例居中放入最小正方形，再生成各尺寸，减少任务栏留白；不修改源图。
macOS 直接从该 PNG 生成 `ogplay.icns`，不再裁剪、缩放主体或覆盖源图。图标产物随仓库保存。
Android 使用自适应图标：由原图按最远可见像素缩放进 66 dp 安全圆，生成各密度的前景层
`android/app/src/main/res/mipmap-*/ic_launcher_foreground.png`；背景色在 `values/ic_launcher_background.xml`，形状由系统裁剪。
替换源图后运行（需要 Pillow）：

```text
python3 tools/build_app_icons.py
```

Windows EXE 内嵌 `assets/icons/ogplay.ico`；macOS `OGPlay.app` 携带
`assets/icons/ogplay.icns`。日常 CMake 构建直接使用生成产物，不依赖 Pillow。

## 启动器 3D 卡带模型

`webui/apps/gui/assets/cartridge.glb`（Draco 14/10/12 位）与 `cartridge-ao.png`（1024 灰度 AO）由 Blender 5.2 程序化生成，产物随仓库保存，
webui 构建时内联进 `cart3d.js`。修改模型后运行，再 `npm run build`：

```text
blender -b --factory-startup -P tools/build_cartridge_model.py -- webui/apps/gui/assets/cartridge.glb
```

## 能力账本

`capabilities.py` 查询和校验 [`capabilities.toml`](../capabilities.toml)，字段与 test 入口格式见账本文件头：

```text
python3 tools/capabilities.py list dexvm --status partial   # 按 id 前缀/状态列出
python3 tools/capabilities.py show runtime.vfs              # 查看条目（也接受 id 前缀）
python3 tools/capabilities.py find WebView                  # 搜索 id/note/test
python3 tools/capabilities.py check                         # 格式、note 长度与 test 入口校验
python3 tools/capabilities.py verify runtime.ui_inflater --build-dir build/dev
```

`verify` 按条目的 test 入口运行 doctest 过滤、ctest、tools 命令或 vitest；`--dry-run` 只打印命令，
多配置生成器用 `--config`。`check` 由 ctest `tools.capabilities_current` 执行。

## dalvik/tests 一致性探针

`conformance/dalvik_tests.py` 把 `.local/aosp/dalvik/tests` 复制到工作目录，用 javac
（1.7，API 19 android.jar）与 dx `--debug`（与 etc/default-build 一致；d8 1.5 会删除 javac
断言代码）编译，经 `ogplay-cli dexvm run-dex --enable-assertions` 在两个解释器后端运行，
与 expected.txt 逐字节比较并分类（pass/fail:*/excluded:*）。dx.jar 取自已下载的
build-tools 29.0.2 包（与 d8.jar 同目录，`OGPLAY_DX_JAR` 可覆盖）。排除表固定并写明理由。

```text
python3 tools/conformance/dalvik_tests.py --self-test
python3 tools/conformance/dalvik_tests.py [--only <test>] [--backend switch] [--jobs 4] [--timeout 60]
python3 tools/conformance/dalvik_tests.py --check-baseline # pass 退步退出 1；支持 --only 子集
python3 tools/conformance/dalvik_tests.py --update-baseline # 无回归才更新仓库基线，子集合并保留其余项
```

固定 `ORACLE_DIVERGENCE` 表记录与 Dalvik 4.4 不符的 expected 判据（含源码位置），归类
`excluded:oracle`；仅表内用例允许 pass → excluded:oracle，基线比较单列显式迁移。

产物默认写 `.local/analysis/wu6/`（summary.md/json、逐用例 stdout/stderr/ledger），不进仓库。

## API 19 启动类路径

`bootdex/build_bootdex.py` 生成 `data/android/19/framework/`：七个 ROM jar（core、conscrypt、
ext、framework、framework2、telephony-common、android.policy）从 `.local/aosp` 原样复制并校验固定 SHA-256；
`src/guest/*/java` 下的 OGPlay Java 用 javac 17 + API 19 android.jar + d8 编成 `ogplay.jar`，
排在启动类路径最前，同名类以它为准。manifest 记录每个 jar 的顺序、来源与哈希。

```text
python3 tools/bootdex/build_bootdex.py build
python3 tools/bootdex/build_bootdex.py check
```

ogplay.jar 只含 manifest 和 DEX；系统资源由 payload 中的 framework-res.apk 提供。

`trim_framework_resources.py` 从 `.local/aosp/framework-res.apk` 生成
`data/android/19/framework/framework-res.apk`：校验 manifest 登记的原版 SHA-256，去掉三张
default_wallpaper.jpg 和 `META-INF/`，其余条目原样保留，resources.arsc 不压缩并按 4 字节对齐。

`build` 两次独立编译 guest Java 并核对 DEX 一致；`check` 重新生成并与现有制品、manifest 比对。
输入默认位于 `.local/aosp` 和 `.local/toolchain`。

`generate_virtual_keymap.py` 从本地 API19 `Virtual.kcm` 与 `KeyEvent.java` 生成虚拟键盘
native 查询表；`--check` 核对生成数据及来源哈希。只接受受审属性，未知行为明确失败。
生成表随 C++ 发布，各宿主平台共用；修改映射后只重建受影响目标。

## DexVM API-19 intrinsic 骨架

`dexvm_api19_surface.py` 从 pinned Android 4.4.4 Java 源码抽取 public/protected
顶层 class shape；仓库固定的 Luni `java.lang` 清单可这样复验：

```text
python tools/dexvm_api19_surface.py \
  --source-root .local/aosp/libcore/luni/src/main/java \
  --package java.lang \
  --check data/dexvm/api19-java-lang-surface.json
```

从清单生成单类当前 builder 骨架（包含预绑定字段 handle，方法保持显式未实现）：

```text
python tools/dexvm_stub_gen.py \
  --surface data/dexvm/api19-java-lang-surface.json \
  --class Ljava/lang/Integer;
```

生成结果是开发起点，不是行为实现；常量值、private 运行时字段、handler 语义及
编译器 synthetic bridge 仍须按该类的 pinned 源码人工审阅。

## M4 本机出口测试

`m4_exit.py` 在当前 Windows、Linux 或 macOS 机器内直接执行严格出口验证，不建立 SSH
连接。脚本预检 API 19 Bionic、两个 APK 的未压缩 ARMv7 native 库、宿主 ANGLE SDK
清单及软件后端声明，然后使用持久 CMake preset 增量配置、构建并运行全量 CTest。该脚本
属于显式的 M4 完整出口，只有用户明确要求全量出口时才运行；普通代码改动使用受影响目标
和单点/定向测试。

```text
python tools/m4_exit.py \
  --bionic-root <包含 api19/lib 的本地目录> \
  --minimal-apk <ogplay-minimal-ndk-armeabi-v7a.apk> \
  --m4-apk <ogplay-m4-exit-armeabi-v7a.apk>
```

Windows 固定使用 `windows-msvc`，Linux/macOS 使用 `dev`；Linux 额外启用 console build。
Linux/macOS 的 ANGLE 清单必须启用 SwiftShader，实际软件黄金帧失败即出口失败。首次运行
完成全量构建，后续运行复用各自 preset 的 build 目录。`--dry-run` 只执行输入与清单预检。

## 远端增量构建

`remote_incremental.py` 使用一个明确指定的远端绝对目录持久保存源码、递归 submodule 和
CMake build 目录。调用者通过参数传入主机、账号、平台及工具路径，通过环境变量提供密码；
仓库中不得保存具体设备信息或凭据。

首次运行会初始化依赖并完整编译，后续运行只 fetch 当前 `main` bundle、更新发生变化的
submodule，然后由 CMake 增量编译并执行 CTest。远端目录必须专用于本工具；工具不会递归
删除它，也不会同步本地未提交改动。

核心依赖会递归初始化；`angle-prebuilt` 单独浅更新。ANGLE 源码及完整 GN 工作区只用于
维护者升级，普通增量验证不初始化源码 submodule，也不同步其 gclient 依赖图。

```text
python tools/remote_incremental.py --host <host> --user <user> \
  --remote-root <absolute-project-cache> --platform linux
```

macOS 或非标准安装可通过 `--cmake`、`--ctest` 指定可执行文件。密码默认从
`OGPLAY_REMOTE_PASSWORD` 读取；也可使用 SSH agent 或密钥认证。

该远端工具不用于 M4 出口验收；M4 必须使用上面的本机入口在对应平台直接执行。

## ANGLE SDK

ANGLE 源码构建、打包、许可证归档和发布自测全部由独立的
[`OGPlay-angle-prebuilt`](https://github.com/poping520/OGPlay-angle-prebuilt/tree/main/tools)
仓库负责；OGPlay 不再保存这些生产工具。普通项目只消费
`third_party/angle-prebuilt` 浅 submodule。

启用消费时设置 `OGPLAY_ENABLE_ANGLE=ON`。默认 SDK 根目录为
`third_party/angle-prebuilt`，也可用绝对 `OGPLAY_ANGLE_SDK_ROOT` 指向待发布包的根目录；
CMake 会按宿主选择平台/CPU，并逐文件验证清单。普通 Debug 构建仍使用默认
`OGPLAY_ANGLE_SDK_CONFIGURATION=release`。

### API 19 guest JNI

`python tools/bootdex/build_bootdex.py build-guest-jni` 使用 NDK r25c ARMv7 API 19 clang
把 `src/guest/crypto`、`src/guest/icu` 与 `src/guest/zip` 编译为唯一 `libogplay_jni.so`。
构建器校验 libcrypto、libssl、ICU 两库、libz 和 icudt51l.dat 输入哈希，执行两次独立构建，
并检查 ELF32 ARM/DYN、SONAME 与精确 DT_NEEDED；源码和 ICU C ABI 头的哈希写入 manifest。工具链或输入不匹配
即失败。宿主构建不下载、编译或链接 ICU。

## 机型预设数据校验

`python tools/validate_devices.py --devices data/devices` 校验 GUI-7 schema 1（Python 3.11+ 标准库）。
未知/缺失字段、类型、范围、身份和资源预算错误均失败；不读取或修改宿主/guest 配置。
格式与支持边界见 [data/devices/README.md](../data/devices/README.md)。
定向测试：`python tests/tools/test_validate_devices.py`；CTest：`tools.device_presets_current` / `tools.device_presets_validator`。

### 静态体检（DIAG-4 / WU1）

`ogplay-cli dexvm dump-surface [--framework <dir>]` 导出真实 linked catalog/启动类路径表面，
保留链接错误、来源、常量和动态 guest JNI 未核实状态。`dexvm_api19_surface.py --jar ...`
可重复指定 API19 core/framework/framework2/ext ROM JAR，提取全部类层级及 public/protected
成员、encoded 常量和输入哈希，沿用 schema 1 的 classes 列表；源码模式保持可用。
`title_preflight.py --surface <json> --reference <json> --apk <apk> --output <json>`
扫描全部根 DEX（含 multidex）、ARM32 JNI 字符串/候选注册表及 ELF 依赖闭包/版本符号。
`--games-dir <dir> --output-dir <dir>` 写逐 APK、类/成员/引擎频次及简表；`--self-test` 自测。

Java/JNI 缺口分 api19-present-missing、not-in-api19、declared-unimplemented；
ELF 使用独立 native-import-missing，不把未知 C 符号伪称 Java API19 成员，weak 不等于强拒载。
RegisterNatives 指针三元组只证明候选表，owner 保持未知；heuristic 配对不证明实际调用。
动态 native admission 不证明实现，guest-jni-unverified 单列；工具不分析系统调用参数、
动态拼接的名称或游戏路径可达性，不生成声明/桩。部分 DEX/ABI/ELF 不支持明确标 incomplete，
失败/不完整扫描返回非零；完整盘点有缺口也可返回 0，不是兼容性门禁。
旧 dexvm_gap_report 的正则结果仅兼容历史工具，不能替代该权威表面。命令见 NEW-TITLE 步 1。
