# 模块：启动器共享逻辑

各功能共用、无界面的部分：
- `rpc.ts`：经宿主 webview 绑定调用 closed-schema JSON-RPC，校验响应结构，失败显式抛错。
- `i18n.ts` 注册 GUI 自己的英文表并 re-export ui-kit 的 language/setLanguage/useLanguage/t/tj；
  gameName 留在 GUI。中文原文作键，未知文字显示原文；宿主状态详情、错误 message/params/next_step
  也经 t() 显示。设置 system 只使用宿主 system_language 解析，草稿预览、恢复与保存共用该规则。
  `i18n.test.ts` 与 controls 共用 ui-kit 测试扫描器，覆盖 GUI 源码、宿主设置与 GUI 消息，
  并禁止原生外壳/游戏关闭路径内写中文字符串；占位符一致。cart3d 独立解码器错误仍排除。
- `prefs.ts`：最近一次解析后的实际语言（不存 system）与界面风格存 localStorage，仅供闪屏前预设，以宿主设置为准；存储不可用时按默认值。首次启动未记住语言时按 `navigator.language`（`initialLanguage`）。
