# OGPlay 能力探针

用 [Probe 样例](../../samples/probe/README.md) 验证一个原创 APK 在 API 19 的行为。探针按实际观测判定状态，依赖和环境未满足时会留下相应证据。

## 构建与运行

在仓库根目录使用 Python 3.11+ 和 JDK 17。Android SDK 需含 `platforms/android-19/android.jar`、Build Tools、SDK CMake 和 NDK。通过参数或环境变量定位 SDK 和 NDK。

```sh
python3 tools/conformance/probe_tests.py build
python3 tools/conformance/probe_tests.py run --config a --interpreter threaded --out .local/probe/a-threaded
python3 tools/conformance/probe_tests.py run --config b --interpreter threaded --out .local/probe/b-threaded
python3 tools/conformance/probe_tests.py run --config a --interpreter switch --out .local/probe/a-switch \
  --compare-to .local/probe/a-threaded/report.json
python3 tools/conformance/probe_tests.py selftest
```

`run` 和 `selftest` 默认用 `build/dev/ogplay-cli`。Windows 可指定 `--cli build/windows-msvc/Release/ogplay-cli.exe`。`--filter device.cpu graphics.gles` 选择 ID 前缀。每次运行需要独立的空 `--out`，省略时自动生成。默认总时限 300 秒，最多重启 8 次。正常运行有能力失败时仍可生成完整报告。

| 配置 | 宽高 px | dpi | CPU 核 | MHz |
| --- | --- | ---: | ---: | ---: |
| a | 1280 × 720 | 320 | 2 | 1000 |
| b | 800 × 480 | 240 | 4 | 1500 |

交互模式直接用 `ogplay-cli run-apk samples/probe/out/ogplay-probe.apk` 启动。启动后自动跑一轮，结束后停在 UI。全量、失败项和单项重跑沿用画面状态。文件报告位于 guest 外部存储 `OGPlayProbe/`。

## 取证与续跑

- `report.json` 合并 APK 结果、恢复的日志结果、启动报告和 ledger。每项保留结构化实测和预期值、状态、调用表及异常原因和栈。
- `summary.md` 按分类列出缺口及其证据、ledger 候选、未关联的 ledger 和基线变化，并单列信息项及跳过原因。
- 外部挂载用于传入 `external/OGPlayProbe/autorun.json`。OGPlay 将写入持久沙盒的 `sdcard/ogplayprobe/` 覆盖层。runner 复用该安装和挂载，从覆盖层收集 `progress.json`、逐项结果和完整 APK 报告。自动运行子目录的 `guest-report.json` 是原始 APK 视角。
- `run-report-NN.json`、`stdout-NN.log` 和 `stderr-NN.log` 按启动保留。ledger 使用 Dashboard 的结构化账本采样，UI 降级来自 CLI 停止阶段的日志。账本覆盖率取决于进程存活时间和采样上限。
- 自检先触发原生 `abort()`，续跑判为 `crash`。随后故意挂起 worker 并检查 `timeout`，继续验证构建探针、ANDROID_ID 和偏好 token。宿主兜底超时时保留未完成证据。

## 单调基线与报告差分

```sh
python3 tools/conformance/probe_tests.py check --report .local/probe/a-threaded/report.json \
  --also-report .local/probe/b-threaded/report.json --also-report .local/probe/a-switch/report.json
python3 tools/conformance/probe_tests.py diff .local/probe/a-threaded/report.json .local/probe/b-threaded/report.json
```

`check` 检查以往通过项，只有所有指定报告均通过的新增项可以经 `--update-baseline` 纳入下一版基线。生成基线须使用完整、未过滤的正式运行。省略 `--report` 时选择默认输出树内最新的完整正式报告。`diff` 对比每项的状态和实测值，包括配置、计时与每个沙盒生成的身份差异。

退出码 0 表示命令成功，1 表示运行未完成、自检失败、基线回归或差分存在，2 表示参数、输入或构建工具失败。
