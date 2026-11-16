# OGPlay 能力探针

普通 API 19 ARMv7 APK。启动后自动执行探针，首页展示进度和分类。点分类可查看单项结果，详情页可重跑单项。所有运行均可导出结构化 JSON。

准备 Python 3.11+、JDK 17、Android SDK 的 API 19 平台、Build Tools、SDK CMake 和 NDK。设置 `ANDROID_SDK_ROOT`、`ANDROID_NDK_ROOT` 后在仓库根目录执行

```sh
python3 tools/conformance/probe_tests.py build
python3 tools/conformance/probe_tests.py run --config a
python3 tools/conformance/probe_tests.py selftest
```

签名 APK 在 `samples/probe/out/ogplay-probe.apk`。runner 会打印汇总 `report.json` 路径。同目录的 `summary.md` 有通过率、首个异常或观测差异、关联 ledger 和启动结局。参数、差分、基线和续跑判读见 [Probe 操作手册](../../docs/playbook/PROBE.md)。
