# OGPlay 糖果铺（sweetshop）

OGPlay 自带的原创三消小游戏：8×8 棋盘、12 关、步数内达到目标分评 1～3 星。
连 4 个生成条纹糖（消整行或整列），L/T 形生成炸弹糖（3×3），连 5 个生成彩虹糖
（交换后消除同色）；两个特殊糖互换会合成更大的爆炸。

## 构建 APK

需要 Android SDK（build-tools、platform 19 与 `cmake/<版本>/bin` 下的 cmake/ninja）、
NDK、Python 3 + Pillow 和 JDK，过程不访问网络：

```bash
python3 samples/sweetshop/tools/build.py --sdk <android-sdk> --ndk <android-ndk>
```

输出 `out/sweetshop/ogplay-sweetshop.apk`。只改了美术时可以先预览生成物：

```bash
python3 samples/sweetshop/tools/build.py --assets-only
```

## 在 OGPlay 里运行

```bash
build/release/ogplay-cli run-apk out/sweetshop/ogplay-sweetshop.apk
```

也可以在 OGPlay 桌面端把 APK 拖进窗口导入。鼠标点击或拖动交换糖果，Esc 返回上一层。
界面支持中文和英文，默认跟随设备语言，标题页右下角的「中 / EN」按钮可以切换。

## 素材

`art/source/` 是 AI 生成后压缩的源图：两张精灵表（糖果、界面图标）、背景、标题插画和应用
图标。`tools/assets.py` 按每格最大连通区域自动裁剪精灵表，再生成运行时图集、条纹叠层、
数字字形和各密度启动图标；`tools/text.py` 渲染双语文字、标题与缎带。可选的 AI 素材
`title_zh`、`title_en`（游戏名字样）和 `sheet_ribbons`（三条缎带竖排：粉、金、紫）放进
`art/source/` 后会自动替换代码生成的版本。
