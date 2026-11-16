# OGPlay 兼容性测试

普通 API 19 ARMv7 APK，首页固定竖屏。手动打开后等待点击“开始测试”，已有完整结果时显示上次汇总与完成时间，可重新测试；中断时可选择继续或重新开始。有 `OGPlayProbe/autorun.json` 时仍自动执行并支持 runner 续跑。首页展示进度和分类（分类名称与通过计数固定英文），点分类查看单项结果，详情页可重跑单项。设备信息独立后台采集，无需先跑测试；分区卡片可滚动，长按行复制值，底部可刷新或展开原始 JSON。所有运行均可导出结构化 JSON。

准备 Python 3.11+、JDK 17、Android SDK 的 API 19 平台、Build Tools、SDK CMake 和支持 API 19 的 NDK（如 r23）。设置 `ANDROID_SDK_ROOT`、`ANDROID_NDK_ROOT` 后在仓库根目录执行

```sh
python3 tools/conformance/probe_tests.py build
python3 tools/conformance/probe_tests.py run --config a
python3 tools/conformance/probe_tests.py selftest
```

签名 APK 在 `samples/probe/out/ogplay-probe.apk`。runner 会打印汇总 `report.json` 路径。同目录的 `summary.md` 有通过率、首个异常或观测差异、关联 ledger 和启动结局。参数、差分、基线和续跑判读见 [Probe 操作手册](../../docs/playbook/PROBE.md)。
