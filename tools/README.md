# 开发工具

## 应用图标

`assets/icons/ogplay.png` 保留最初原图，供 Windows 图标使用；`assets/icons/ogplay-macos.png` 是 macOS 图标源图，使用已调整好的 1024×1024 图案、留白及透明度。
macOS 直接从该 PNG 生成 `ogplay.icns`，不再裁剪、缩放主体或覆盖源图。图标产物随仓库保存。
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

## API 19 BootDex

`bootdex/build_bootdex.py` 根据 [`bootdex/api19.json`](bootdex/api19.json) 从 API 19 ROM/AOSP jar
精确选取 class_def，以 smali/baksmali 3.0.10 重组 DEX，并生成固定格式的 JAR。

```text
python3 tools/bootdex/build_bootdex.py build
python3 tools/bootdex/build_bootdex.py check
```

`api19-system-resources.json` 保存受审资源名、类型、消费者及 OGPlay 配置值，不固定 ROM
资源 ID，也不依赖本地 AOSP config.xml。build/check 从最终 DEX 中的实际 Resources 调用
提取 ID，生成 `META-INF/ogplay/system-resources.json` 并与 classes.dex 同包发布。
运行时从当前 BootDex 归档读取映射；更换输入后重新 build/check，无需因 ID 变化重编译宿主。
SQLite journal mode 保留 DELETE；读取 WAL 配置不代表支持 WAL。

提取器支持常量、普通 move 和 invoke/range；分支/标签清除常量事实，无法证明的 ID、
类型变化、重复 ID、遗漏/新增查询明确失败并指出消费者。它不自动适配厂商新增类或 native。

`bootdex/select_shapes.py` 用四个 ROM JAR 的 class_def/原版 smali 筛选纯形状：

```text
python3 tools/bootdex/select_shapes.py --self-test
python3 tools/bootdex/select_shapes.py --surface <dump-surface.json> --scan-dir <preflight目录> --report <本地报告.json>
# 审阅 candidates/conflicts/依赖后，同参数加 --apply 才修改 api19.json；随后 build/check。
```

只选题库引用且依赖已在当前运行时可链接的类；跳过已有 intrinsic、BootDex 及策略归属。
当前保守子集只接受全 abstract 的接口/注解、encoded 基元/String 常量 final 类及空 clinit；
不接受构造器、枚举、计算式初始化或其他行为代码。依赖包括签名/注解中的类型；
未引用的依赖不自动纳入。报告/反汇编缓存留 `.local/`，边界见 [DVM-252](../docs/tasks/dexvm/DVM-252.md)。

输入默认位于 `.local/aosp` 和 `.local/tools`；换 ROM 时应使用同一来源配套的输入 JAR。
JAR 哈希记录于 manifest 供追溯，不作为固定来源准入条件；工具下载仍校验固定 SHA-256。
新增类时修改 recipe 对应源 jar 的有序列表，运行时仍全量加载生成物中的 class_def。
`build`/`check` 均执行两次独立重组并核对 DEX 一致；check 校验 JAR、内嵌映射和 manifest。

`api19.json` 的 `date_family_audit` 保存 DVM-102 日期闭包的依赖分类、native 边界与
ICU 来源；`roots` 冻结原 42 类审计样本，不随依赖迁移扩大；其中 `boot_dex` 是主类清单的受检子集，外部依赖分类不得与主清单冲突。
`audit` 校验固定 `core.jar`、全部引用分类与 native 签名，完整派生成员表写入
`.local/dvm102-date-family-audit.json`。`--report` 可指定报告路径；`--emit-expectations`
只打印观测指标供审阅，不修改冻结值。清单结构校验与审计自测统一由 builder 执行：

```text
python3 tools/bootdex/build_bootdex.py audit
python3 tools/bootdex/build_bootdex.py --self-test
```

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

`ogplay-cli dexvm dump-surface [--bootdex <jar>]` 导出真实 linked catalog/BootDex 表面，
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
