# 模块：frontend

## 职责

提供 CLI 与未来 Qt GUI；二者只编排公共内核 API，不实现兼容行为。

## 公共 API

CLI 支持版本、能力账本、结构化 Agent 请求，以及 M4 最小 API 19 NativeActivity APK
的交互窗口运行；`run-apk --supersample <1..4>` 可显式选择内部渲染倍率，省略时为 1×；
通用 import/profile 与 GUI 留在 M6。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile。
- 用户可见输出可以写 stdout/stderr，但生产库不得裸输出。
- `run-apk` 要求显式 API 19 Bionic 目录；APK 必须恰有一个未压缩 armeabi-v7a native 库，
  不猜测多库入口。
- `--supersample` 只接受完整十进制整数 1..4，必须在 APK I/O 和窗口创建前拒绝缺值、
  零、越界或尾随字符；默认值保持 1×。
- pointer 事件按最近 guest 帧与当前窗口的等比内容区映射；黑边按下/移动不注入，黑边
  释放仍夹紧转发以闭合已开始的手势。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI 参数契约和真实 APK smoke 由 CTest/有界集成命令驱动；GUI 行为测试在 M6 增加。
