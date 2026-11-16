# OGPlay 兼容性测试

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
| a | 720 × 1280 | 320 | 2 | 1000 |
| b | 480 × 800 | 240 | 4 | 1500 |

表中宽高为宿主传入尺寸；Probe 首页声明竖屏，显示探针按短边 × 长边校验旋转后的尺寸。

交互模式直接用 `ogplay-cli run-apk samples/probe/out/ogplay-probe.apk` 启动（Windows 使用上文的 CLI 路径）。guest 外部存储 `OGPlayProbe/` 不存在 `autorun.json` 时，首页显示“尚未运行”，点击“开始测试”才执行；完整结果会在下次打开时显示上次汇总、分类与完成时间，可点“重新测试”。中断的手动运行不会自动续跑，首页显示中断探针 ID，点击“继续”将活动项记为 crash 后沿用检查点续跑，或点“重新开始”清空本批进度从头执行。失败项和单项重跑需已有结果，运行中禁用启动类按钮。有 `autorun.json` 时保持 runner 的 session、filter、exit、selftest 与自动续跑行为。文件报告仍位于该目录。

“设备信息”无需跑过测试，进入或点击底部“刷新”时在后台独立只读采集；加载后以可滚动分区卡片展示，单个分区失败只显示该区的异常。长按行可复制值，GL 扩展和底部“查看原始数据”默认收起。截图与输入验证使用 [MCP 操作手册](MCP-TESTING.md)，指定新的 `--sandbox-dir`，不传 autorun；需要 `session_state` / `shutdown` 时加 `--diag`。

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
