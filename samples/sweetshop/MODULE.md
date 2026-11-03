# 模块：sweetshop sample

## 职责

「糖果铺」是 OGPlay 自带的原创三消小游戏，以普通 Android APK 的形式交付，供用户首次打开
即可体验完整的导入 → 启动 → 游玩 → 存档流程，同时作为真实运行的回归载荷。

## 公共边界

- Android API 19、`armeabi-v7a`、`SweetShopActivity extends NativeActivity`，GLES 2.0。
- 只依赖 NDK 平台库 `android`、`EGL`、`GLESv2`、`OpenSLES`、`log`、`m` 与仓库内
  `third_party/stb`；不使用 `ANativeActivity_finish`、Play 服务、网络或权限。
- 输入只用单指触摸（OGPlay 默认鼠标直通）与 `KEYCODE_BACK`；画面在 1280×720 逻辑坐标内
  等比黑边绘制。
- 构建工具通过参数或环境变量定位 SDK/NDK，不记录开发机绝对路径。

## 不变量

- 美术源图只放 `art/source/`（WebP/JPEG 压缩稿），分辨率按运行时最大用量保留约 1.25～1.5 倍
  余量（背景即 1280×720 输出尺寸），不保存 AI 原始大图；运行时图集、背景、标题图与启动图标
  全部由 `tools/assets.py` 生成，不手工维护生成物。
- 音乐与音效在运行时合成，APK 不携带音频文件。
- 运行时贴图按内容选格式：彩色精灵为 RGBA 图集；白填充棕描边的文字与数字单独成图，用两色
  近无损调色板；标题与糖果罐插画为 JPEG 颜色加灰度透明度。`atlas.txt` 每项记录所在图与像素
  密度（文字 2 倍、缎带 1.5 倍），绘制尺寸由密度换算；原生库打包前去除调试段。
- 界面文字中英双语：`tools/text.py` 是唯一字符串表，用仓库自带的 DroidSansFallback/Roboto
  （Apache 2.0）在构建时栅格化进图集；默认跟随 `AConfiguration` 语言（zh 为中文，其余英文），
  标题页切换后写入存档。
- 启动卡片、标题页徽章、选关页角标与启动图标角标使用 OGPlay 图标和字标，标明这是 OGPlay
  自带示例。字标 `art/source/og_wordmark.png` 由 `tools/wordmark.py` 在 macOS 上按桌面端字体与
  配色渲染后入库，构建只用这张图，不携带字体文件；`art/source/` 中有 `title_zh`/`title_en`/`sheet_ribbons` 时替换代码生成的标题与缎带。
- 关卡目标按宿主侧随机玩家得分分布设定：一星接近 10%～25% 分位，三星需 1.7 倍目标。
- 存档为 `internalDataPath/progress.txt` 文本，先写临时文件再替换。
- APK、签名密钥与中间产物只进入 Git 忽略的 `out/`。

## 验证

`tools/build.py` 完成素材生成、NDK 编译、javac/d8、aapt2 打包、对齐、签名与内容检查；
`--assets-only` 只重新生成素材，不需要 SDK/NDK。真实运行用 `ogplay-cli run-apk` 加
`--mcp --mcp-manual-step` 截图与注入触摸。
