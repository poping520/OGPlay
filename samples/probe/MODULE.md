# 模块：OGPlay 兼容性测试

## 职责

以普通 Android APK 执行可判定的能力探针。界面供人查看、筛查和重跑，JSON 与 `OGProbe` 日志供宿主自动汇总。

## 公共边界

- `org.ogplay.probe`，manifest 声明 minSdk 9、targetSdk 19，验收目标为 API 19、`armeabi-v7a`。首页 Activity 声明竖屏；辅助测试 Activity 保留各探针要求的方向行为。
- Java 7 和 framework 控件，英文和简体中文资源随设备配置切换；探针分类名称与分类通过计数固定使用英文。原生探针使用 NDK，静态链接 libc++。
- 探针按设备、服务、运行时、存储、媒体、图形、UI 和原生分类。断言依据 API 19 的实现语义、独立来源的一致性和 runner 下发的配置。范围待确认时使用 `info`。
- 网络探针仅连接 127.0.0.1 并查询 localhost。隔离自检由 autorun 显式启用。
- SDK/NDK 经参数或环境变量定位。APK、签名密钥和生成物集中在 `out/`。
- 主 APK 清零 type-spec 的 res1，以便当前 OGPlay 加载主探针。两个固定资源包以 STORED 方式内嵌，包含同一 marker。`runtime.resources.table_control` 加载清零样本，`runtime.resources.aapt2_typespec` 加载新版 aapt2 原始样本，检测宿主是否接受 API 19 读取时忽略的 res1。构建验证两份样本的 res1 和 ZIP 存储方式。静态 libc++ 的 abort 消息在旧 libc 上写入 stderr。

## 不变量

- 探针 ID、scope、调用描述符和状态词是报告接口。JSON 报告为 `ogplay.probe.report/1`，键序固定，结果按 ID 排序。
- 使用具名组合判据的探针在 `observed.failed_checks` 中按判据顺序记录失败项，通过时为数组 `[]`。
- 外部存储的 `OGPlayProbe` 目录保存 autorun、报告和续跑数据。有 `autorun.json` 时自动执行，按相同 session 和安装实例续跑，保留 filter、exit 和 selftest 语义；没有时只恢复展示，等待用户开始。手动运行中断后提示探针 ID，由用户选择继续（活动项记 crash）或重新开始。完整报告或可恢复的逐项结果供首页展示上次汇总。
- 报告根层可选 `completed_at_ms` 为完成时的 Unix 毫秒时间戳，不改变 environment 或各探针结果；完成时间也保存在批次 plan 中，供只剩逐项结果时恢复；旧数据缺失此键时用报告或完整结果日志的文件修改时间展示，时间源不可用时显示“—”。
- 设备信息页进入和刷新时在后台只读采集 framework、procfs/sysfs 及共享的设备读取方法，不依赖探针 observed，也不写入报告 environment。分区独立捕获失败；GL 复用 EGL pbuffer 读取与 finally 释放。卡片为主，原始 JSON 和扩展列表默认折叠；行值可长按复制，剪贴板不可用时提示。
- 写入运行检查点后才执行探针。结果先同步并原子替换，再推进检查点。worker 超时放弃该线程。主线程超时先保存结果，再退出进程以便续跑。
- 单条日志最长 3500 UTF-8 字节。详细异常和大对象优先保留在磁盘报告。
- runner 的 ledger 关联是按描述符、服务名或异常证据的候选匹配。采样结果与各次启动的日志和 run-report 一同归档。

## 验证

构建、配置矩阵、隔离自检和单调基线入口见 [样例 README](README.md) 与 [Probe 操作手册](../../docs/playbook/PROBE.md)。
