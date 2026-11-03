# GUI-19 · 内置 Demo 游戏

状态：macOS 定向测试与新库首启实测已验；App 内设置切换与删除点击、Windows 安装包待验。依赖：GUI-4（全局设置）、GUI-8（移除）。

目标：OGPlay 安装包自带原创示例游戏「OGPlay 糖果铺」，首次启动即在游戏库中可玩；设置里可选择是否展示。

## 范围

- 打包：`samples/sweetshop/out/ogplay-sweetshop.apk`（仓库提交的成品）在 `ogplay_stage_data` 中复制到
  `data/demos/`，随 .app 资源与 install 规则交付。`data/demos/` 只放 OGPlay 自己的原创示例，不放第三方游戏。
- 识别：包名与随包 Demo 一致的库条目即 Demo，不论自动放入还是手动导入；webui 收到 `builtin_demo`。
- 设置：全局「常规 · 展示内置 Demo 游戏」`show_demo_games`，默认开。
  - 开：启动器每次启动把库中缺失的 Demo 按普通导入放入（无数据包，/sdcard，沿用残留存档 id）；
    由关改开时立即补入。
  - 关：`library.list` 不列出 Demo 条目，条目、存档与 id 不动。
- 删除：删除 Demo 即关闭该项（返回 `demo_hidden`），下次启动不再放回；确认框说明此行为。

不做：随包 Demo 升级时替换库中旧版、多个 Demo 的单独开关。

## 验证

- `ogplay_app_tests`：
  - 库：FindBundledDemos 只读 *.apk、缺目录为空；新库首次补入、重复不补；关时不补；重新开启沿用原 id。
  - RPC：Demo 带 `builtin_demo`；关时列表隐藏而条目保留；`settings.set` 由关改开只补入一次；删除 Demo 关闭该项。
- webui：65 用例通过，设置图标覆盖新键。
- 实测（macOS release）：空库根启动 OGPlay.app，库中出现 `org.ogplay.sweetshop`（OGPlay 糖果铺 / OGPlay Sweet Shop），
  gui.log 记录 `frontend.gui.demo`；.app 内含 `Contents/Resources/data/demos/ogplay-sweetshop.apk`。
- 待验：App 内切换设置与删除 Demo 的点击流程、Windows 安装包与首启。
