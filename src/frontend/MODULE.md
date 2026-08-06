# 模块：frontend

## 职责

提供 CLI 与未来 Qt GUI；二者只编排公共内核 API，不实现兼容行为。

## 公共 API

CLI 支持版本、能力账本、结构化 Agent 请求，以及由精确 Title Profile 驱动的 APK
预检与 M4 NativeActivity 交互窗口运行；`run-apk --supersample <1..4>` 可显式选择内部
渲染倍率，省略时为 1×；GUI 留在 M6。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile。
- 用户可见输出可以写 stdout/stderr，但生产库不得裸输出。
- `run-apk` 要求显式 Bionic 目录；APK 入口只能由 Manifest + native hash + ABI 精确 Profile
  决定，支持压缩的 `armeabi`/`armeabi-v7a`，依赖闭包不得由 CLI 手写。
- `--profiles-dir` 可覆盖默认仓库目录；`--preflight` 在身份、ELF 闭包、API、生命周期与
  surface 受检后，复用生产 Android boundary 完成映射/重定位并报告模块、relocation 与
  native-call 计数，不创建窗口或执行 guest。
- `--supersample` 只接受完整十进制整数 1..4，必须在 APK I/O 和窗口创建前拒绝缺值、
  零、越界或尾随字符；默认值保持 1×。
- pointer 事件按最近 guest 帧与当前窗口的等比内容区映射；黑边按下/移动不注入，黑边
  释放仍夹紧转发以闭合已开始的手势。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI 参数契约和真实 APK smoke 由 CTest/有界集成命令驱动；GUI 行为测试在 M6 增加。
