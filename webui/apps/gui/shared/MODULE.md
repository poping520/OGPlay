# 模块：启动器共享逻辑

各功能共用、无界面的部分：
- `rpc.ts`：经宿主 webview 绑定调用 closed-schema JSON-RPC，校验响应结构，失败显式抛错。
- `i18n.ts` 注册 GUI 的九份消息目录（`messages/<标签>.ts`，zh-CN 为源目录）并 re-export ui-kit 的
  language/setLanguage/useLanguage/t/tj/has；gameName 留在 GUI。代码只写语义 ID；宿主消息以 `UiText`
  `{text, id?, params?}`（RPC 错误为 message_id/params/next_step_id）到达，`uiText` 有 ID 时查目录，否则原样显示诊断文本。
  设置 system 只使用宿主 system_language 解析，草稿预览、恢复与保存共用该规则。
  `i18n.test.ts` 校验各目录与 zh-CN 同 ID、ICU 可解析且参数一致、日文与中文外不含汉字；源码只引用存在的 ID、
  每个 ID 都有使用者（`host.*` 由 C++ 引用，`setting.*` 字段文案由宿主 schema 派生），TS 与宿主 C++
  （ui_text.cpp 原生文案表除外）不含中文界面文字。cart3d 独立解码器错误仍排除。
- `prefs.ts`：最近一次解析后的实际语言（不存 system）与界面风格存 localStorage，仅供闪屏前预设，以宿主设置为准；存储不可用时按默认值。首次启动未记住语言时按 `navigator.language`（`initialLanguage`）。
