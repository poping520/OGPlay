# GUI-8 · 游戏库移除闭环

状态：实现与定向自动验证完成；用户要求真实全链路验收留到下次。

- 详情接回“移除库中实例”，默认二次确认，显示实例 ID 和删除/保留范围。
- `library.remove` 严格校验安装 ID、confirmed=true 和未知字段；入库中、运行中拒绝。
  损坏实例退出后仍可移除；库内 APK/设置/日志被删除，沙盒、外部数据包、原始 APK 保留。
- 全局“移除前二次确认”实际生效；失败保留错误提示，成功刷新库并清除选择。
- 修正 Dashboard 契约过时描述及机型预设列表误导文案；GUI-7 仍仅数据阶段。

验证：Windows Release `ogplay-gui` / `ogplay_tests` 构建成功；GUI RPC/settings 7 项 / 4460 断言、
库模型/视图模型 24 项 / 208 断言通过；前端 TypeScript/Vite 与 30 项测试通过；
`frontend.gui_webui_manifest` / `frontend.gui_options` 通过。生成制品已同步到 Release 数据目录。

本轮未启动真实 APK、未进行原生点击/拖放验收。下次在独立测试库完成：
导入 → 保存实例设置 → 启动真实 APK → 手动/自动打开 Dashboard → 退出回收 → 移除库条目，
核对沙盒保留。预设选择/运行时应用、存档管理等仍属未接入功能，不在本次完成范围。
