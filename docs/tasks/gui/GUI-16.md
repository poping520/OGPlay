# GUI-16 · 清空游戏数据

状态：完成，2026-11-01；OGPlay.app 内点击待验。依赖：GUI-8（移除与运行中保护）、沙盒 SandboxStore（ADR-0071）。

目标：让玩家把一款游戏恢复到刚导入时的状态（重开存档、清掉游戏下载/生成的文件），游戏本身与数据包不动。

## 范围

- runtime：`SandboxStore::Clear()` 原地清空 internal/external/obb/sdcard 四个语义根并清零索引，`meta.toml`
  不动，实例 id 与 ANDROID_ID 保持；每款游戏的 sdcard 视图本就私有，因此整个覆盖层都属于该游戏数据。
- library：`LibraryStore::ClearSaves(key)` 排他库锁 + 实例运行锁，损坏条目与运行中拒绝。
- GUI：`library.clear_data {installation_id, confirmed:true}`；“更多 › 清空游戏数据”与游戏设置数据页按钮
  （写入完成后回游戏库）打开同一警告确认框：列出清空/保留范围、不可撤销提示，默认焦点在取消，运行中禁用。

不做：CLI 子命令、全局“清理全部沙盒”（仍标暂未开放）、清空前自动备份。

## 验证

- `ogplay_tests` 沙盒用例 23 项（新增：四根写入/目录/tombstone 清空、清空后可继续写、重开后为空且 ANDROID_ID 保留）。
- `ogplay_app_tests` GUI RPC/库相关 33 项（新增：缺 confirmed、多余字段、越界/不存在 id 拒绝；运行中 -32002
  与外部运行租约拒绝；成功后四根为空、meta.toml 逐字节不变、库内 APK 与 meta 保留）。
- `npm run check` 57 项；浏览器模拟宿主核对两种皮肤下菜单与设置页入口、确认后只发一次 confirmed 请求、取消不写入。
  顺带修正设置页 `.skin-glass dialog li` 规则泄漏到游戏库玻璃紫弹窗。
