# 模块：OGPlay Probe

## 职责

以普通 Android APK 执行可判定的能力探针。界面供人查看、筛查和重跑，JSON 与 `OGProbe` 日志供宿主自动汇总。

## 公共边界

- `org.ogplay.probe`，manifest 声明 minSdk 9、targetSdk 19，验收目标为 API 19、`armeabi-v7a`。
- Java 7 和 framework 控件，英文和简体中文资源随设备配置切换。原生探针使用 NDK，静态链接 libc++。
- 探针按设备、服务、运行时、存储、媒体、图形、UI 和原生分类。断言依据 API 19 的实现语义、独立来源的一致性和 runner 下发的配置。范围待确认时使用 `info`。
- 网络探针仅连接 127.0.0.1 并查询 localhost。隔离自检由 autorun 显式启用。
- SDK/NDK 经参数或环境变量定位。APK、签名密钥和生成物集中在 `out/`。
- 主 APK 清零 type-spec 的 res1，以便当前 OGPlay 加载主探针。两个固定资源包以 STORED 方式内嵌，包含同一 marker。`runtime.resources.table_control` 加载清零样本，`runtime.resources.aapt2_typespec` 加载新版 aapt2 原始样本，检测宿主是否接受 API 19 读取时忽略的 res1。构建验证两份样本的 res1 和 ZIP 存储方式。静态 libc++ 的 abort 消息在旧 libc 上写入 stderr。

## 不变量

- 探针 ID、scope、调用描述符和状态词是报告接口。JSON 报告为 `ogplay.probe.report/1`，键序固定，结果按 ID 排序。
- 使用具名组合判据的探针在 `observed.failed_checks` 中按判据顺序记录失败项，通过时为数组 `[]`。
- 外部存储的 `OGPlayProbe` 目录保存 autorun、报告和续跑数据。每次启动以相同 session 和安装实例恢复，已持久化的探针结果可重放。
- 写入运行检查点后才执行探针。结果先同步并原子替换，再推进检查点。worker 超时放弃该线程。主线程超时先保存结果，再退出进程以便续跑。
- 单条日志最长 3500 UTF-8 字节。详细异常和大对象优先保留在磁盘报告。
- runner 的 ledger 关联是按描述符、服务名或异常证据的候选匹配。采样结果与各次启动的日志和 run-report 一同归档。

## 验证

构建、配置矩阵、隔离自检和单调基线入口见 [样例 README](README.md) 与 [Probe 操作手册](../../docs/playbook/PROBE.md)。
