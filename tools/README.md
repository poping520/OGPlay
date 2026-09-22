# 开发工具

## API 19 BootDex

`bootdex/build_bootdex.py` 根据 [`bootdex/api19.json`](bootdex/api19.json) 从 pinned AOSP jar
精确选取 class_def，以 smali/baksmali 3.0.10 重组 DEX，并生成固定格式的 JAR。

```text
python3 tools/bootdex/build_bootdex.py build
python3 tools/bootdex/build_bootdex.py check
```

输入默认位于 `.local/aosp` 和 `.local/tools`；缺少 smali/baksmali 时生成器会下载固定的
3.0.10 release，所有输入均校验固定 SHA-256。新增类时只修改 recipe 对应源 jar 的有序
列表，运行时仍全量加载生成物中的 class_def。

DVM-107 另选入 framework 的 Pair、SparseArray 家族、ComponentName 与 Parcelable 接口。
其 ArrayUtils 从 `.local/aosp/framework2.jar` 精确选入；该 JAR 与其他 BootDex 输入一样固定
SHA-256。`build`/`check` 均执行两次独立重组并核对 DEX 一致；无需 SDK 或额外 recipe。

`api19.json` 的 `date_family_audit` 保存 DVM-102 日期闭包的依赖分类、native 边界与
ICU 来源；`roots` 冻结原 42 类审计样本，不随依赖迁移扩大；其中 `boot_dex` 是主类清单的受检子集，外部依赖分类不得与主清单冲突。
`audit` 校验固定 `core.jar`、全部引用分类与 native 签名，完整派生成员表写入
`.local/dvm102-date-family-audit.json`。`--report` 可指定报告路径；`--emit-expectations`
只打印观测指标供审阅，不修改冻结值。清单结构校验与审计自测统一由 builder 执行：

```text
python3 tools/bootdex/build_bootdex.py audit
python3 tools/bootdex/build_bootdex.py --self-test
```

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
把 `src/guest/crypto` 与 `src/guest/icu` 编译为唯一 `libogplay_jni.so`。构建器校验
libcrypto、ICU 两库和 icudt51l.dat 输入哈希，执行两次独立构建，并检查 ELF32 ARM/DYN、
SONAME 与精确 DT_NEEDED；两个 C 源和 ICU C ABI 头的哈希写入 manifest。工具链或输入不匹配
即失败。宿主构建不下载、编译或链接 ICU。

## 机型预设数据校验

`python tools/validate_devices.py --devices data/devices` 校验 GUI-7 schema 1（Python 3.11+ 标准库）。
未知/缺失字段、类型、范围、身份和资源预算错误均失败；不读取或修改宿主/guest 配置。
格式与支持边界见 [data/devices/README.md](../data/devices/README.md)。
定向测试：`python tests/tools/test_validate_devices.py`；CTest：`tools.device_presets_current` / `tools.device_presets_validator`。
