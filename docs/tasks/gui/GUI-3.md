# GUI-3 · 导入向导与宿主对话框

状态：Windows 单体 APK 导入主流程完成；跨窗口拖放手势未实测。

依赖：[GUI-2](GUI-2.md)。复用 AnalyzeApkImport、BuildLibraryImport 与 LibraryStore。

- HAL 使用 IFileOpenDialog 独立 STA 线程，文件/目录选择通过 future 与 RPC 轮询返回；
  选择器取消返回空，原生消息循环不阻塞 WebView RPC 回调。
- 文件路径先复制为独立快照；拖入 APK 以 192 KiB 分块传到快照，校验总量和顺序，
  APK 上限 1 GiB。后台分析和入库使用同一快照，每服务最多一个活动导入任务。
- 摘要显示图标、名称、包名、版本、API/ABI 与 Profile；无 Profile 不推测数据包需求。
  所需数据包可选目录或明确跳过。同包须确认新实例，从不覆盖旧游戏或存档。
- 后台原子发布返回实际 installation id；前端刷新并选中新卡片。重复提交拒绝，
  失败可查询。入库期间库枚举返回忙，避免清理正在写入的临时目录。
- 正常关闭等待后台操作结束后清理本服务快照；崩溃遗留快照不在下次启动时按名称删除。
  选择器与后台操作期间向导保持，确认前可以取消；开始提交后不能取消。

支持边界：当前模型仅支持单体 APK。XAPK/APKM/APKS 拆包、目录自动寻找 APK、Linux 和
macOS 宿主不在本次范围；UI 明确提示不支持分包。设置、删除及 Dashboard 尚未接入。

验证（2026-09-22）：

- Windows Release `ogplay-gui`、`ogplay_tests` 构建通过。
- `npm run build`：类型检查、11 个前端用例及静态制品生成通过。
- GUI/control_service 定向回归 53 用例通过；包含有效二进制 Manifest APK 的上传、分析、
  入库与重新解析，以及损坏 APK 失败、分块长度/顺序、取消、重复提交、无效数据目录。
- 4 项 `frontend.gui_*` CTest 通过：制品哈希、参数、空库与 CJK 非空库真实 WebView 冒烟。
- Windows 原生交互：选择测试 APK、摘要确认、入库、自动选中、同包二次确认禁用/启用、
  数据包目录回填及第二实例入库均通过。源与库内 APK 的 SHA-256 相同，旧实例保留，
  第二实例为 `org.example.game-2`，目录与 UTC 导入时间已落盘。测试窗口已关闭。

证据：`.local/gui-v2/v2-03-build.log`、`v2-03-tests.log`、`library-03/`。
未执行全量测试或真实游戏兼容验收；跨窗口拖放手势未实测，分块传输有前后端自动测试。
