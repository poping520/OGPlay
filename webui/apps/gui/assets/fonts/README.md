# SN Pro

启动器的离线圆体后备字体，供 Windows 的品牌字样、英文游戏名和数字使用。
macOS 仍优先使用系统圆体；中文沿用系统字体。仅在 GUI 加载，不影响 Dashboard 或按键编辑器。

- 来源：[Supernotes / SN Pro](https://github.com/supernotes/sn-pro/tree/e025ac4755352167a4b160bfb77c72068b04b55c)
- 文件：`sn-pro-wght.ttf`，正体可变字体，使用字重 400–900；Windows 品牌与大标题使用 800。
- 原始下载：[TTF](https://raw.githubusercontent.com/supernotes/sn-pro/e025ac4755352167a4b160bfb77c72068b04b55c/fonts/variable/SNPro%5Bwght%5D.ttf)，未修改。
- 许可：[SIL Open Font License 1.1](OFL.txt)，由 `tools/credits.py` 纳入统一 `data/credits.json`，声明 `platforms: ["Windows"]`。
  全平台原样分发同一份清单，仅“设置 → 关于 → 开源致谢”按平台筛选，Windows 可查看该条目及全文，其他平台不展示。

仅 Windows 目标构建由 Vite 将字体内嵌为 CSS data URL；macOS/Linux 构建排除字体资源，许可统一收录在公共清单中。
GUI CSP 仅允许 `font-src data:`，无需安装字体或联网加载。目标平台默认取构建主机，跨平台准备制品时设置
`OGPLAY_WEBUI_PLATFORM=Windows|Darwin|Linux`；CMake 构建会自动传入目标平台，并检查 GUI manifest 的平台是否匹配。
