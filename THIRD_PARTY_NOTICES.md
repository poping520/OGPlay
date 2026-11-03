# Third-party notices

本文件记录 OGPlay 构建或发行所包含的第三方组件。启动器“关于”页的开源致谢及许可证全文由
`tools/credits.py` 生成到 `data/credits.json`，新增、移除或升级发行依赖时同步更新其 `PROJECTS` 列表。
外部源码依赖以 Git submodule 固定到明确提交，上游自带的许可证文件随 submodule 保留；参考算法派生
实现另行列明来源。

发行物包含：

| 组件 | 版本 | 来源 | 许可证 | 用途 |
| --- | --- | --- | --- | --- |
| SDL | 3.4.10（`8e37db5e797b6167f3a00d697d816a684bd259c7`） | https://github.com/libsdl-org/SDL | zlib | 窗口、输入与宿主多媒体 HAL |
| ANGLE（预编译 libEGL/libGLESv2） | `c24d9971269a878a221238a5923abdcc933fa2e9` | https://github.com/poping520/OGPlay-angle-prebuilt（上游 https://chromium.googlesource.com/angle/angle） | BSD-3-Clause，附带组件见包内 `licenses/third-party/` | GLES 到 Metal/Vulkan 的图形后端 |
| Dynarmic | 6.7.0（`05b7ba50588d1004e23ef91f1bda8be234be68f4`） | https://git.eden-emu.dev/eden-emu/dynarmic | 0BSD | ARM 动态翻译后端 |
| {fmt} | 11.1.4（Dynarmic externals） | https://github.com/fmtlib/fmt | MIT | Dynarmic 依赖 |
| mcl | 0.1.13（Dynarmic externals） | https://github.com/azahar-emu/mcl | MIT | Dynarmic 依赖 |
| unordered_dense | 4.5.0（Dynarmic externals） | https://github.com/martinus/unordered_dense | MIT | Dynarmic 依赖 |
| oaknut | 2.0.2（Dynarmic externals） | https://github.com/merryhime/oaknut | MIT | Dynarmic arm64 代码生成 |
| Xbyak | 7.22（Dynarmic externals） | https://github.com/herumi/xbyak | BSD-3-Clause | Dynarmic x86-64 代码生成 |
| Zydis / Zycore | 4.0.0 / 1.5.2（Dynarmic externals） | https://github.com/zyantific/zydis | MIT | Dynarmic x86-64 反汇编 |
| ext-boost | 1.90（`6a85c3100499e886e11c87a5c2109eedacea0a61`） | https://github.com/azahar-emu/ext-boost | Boost Software License 1.0 | Dynarmic 所需的裁剪 Boost 头文件 |
| Boost.Pool | 1.90（`740c8076f9d02f0216e8f3dbb15d2fd80f67d7f4`） | https://github.com/boostorg/pool | Boost Software License 1.0 | 补充 Dynarmic 使用的 pool allocator 头文件 |
| FFmpeg（`ogplay_ffmpeg` 预编译共享库） | n8.1.3（`1041abdc962f4cc4f394aa8de9dc5236c0c3b9e7`） | https://github.com/poping520/OGPlay-ffmpeg-prebuilt（上游 https://git.ffmpeg.org/ffmpeg.git） | LGPL-2.1-or-later | 游戏 CG 视频解码：mov 容器、H.264/MPEG-4/H.263/AAC |
| SQLite amalgamation | 3.53.4（`ed6f533374f8340dee587f714115e75d6d8d6aa6`） | https://github.com/poping520/sqlite-amalgamation | Public Domain | guest SQLite 数据库运行时 |
| yyjson | 0.12.0（`8b4a38dc994a110abaec8a400615567bd996105f`） | https://github.com/ibireme/yyjson | MIT | JSON 解析与生成 |
| stb | stb_image 2.30、stb_image_write 1.16、stb_truetype 1.26、stb_vorbis 1.22 | https://github.com/nothings/stb | MIT 或 Public Domain（Unlicense） | 图片解码/编码、字体栅格化、Ogg Vorbis 解码 |
| minimp3 | `ea99364f61c14656440e8d77e9c233ccf3124633` | https://github.com/lieff/minimp3 | CC0-1.0 | MP3 解码 |
| Compressonator（ATC 解码文件） | `f4b53d79ec5abbb50924f58aebb7bf2793200b94` | https://github.com/GPUOpen-Tools/compressonator | MIT | ATC 纹理软件解码 |
| PowerVR Native SDK | `2b1bf2f14d3365d0bb801e2a6a131a319d3a2e48` | https://github.com/powervr-graphics/Native_SDK | MIT | 原样引入 `PVRTDecompress.cpp/.h` 实现 PVRTC1 软件解码 |
| webview | `3ab4b5d722438fc8a13e6ca830c5e2372d19a01d` | https://github.com/webview/webview | MIT | 启动器 WebView 宿主 |
| Microsoft WebView2 SDK（仅 Windows） | 构建机 `OGPLAY_WEBVIEW2_SDK` | https://developer.microsoft.com/microsoft-edge/webview2 | Microsoft WebView2 SDK 许可 | Windows 启动器 WebView；许可证安装为 `data/webui/gui/WEBVIEW2-SDK-LICENSE.txt` |
| Preact | 10.29.8 | https://preactjs.com | MIT | 启动器、Dashboard 与按键设置 Web UI |
| three.js | 0.169.0 | https://threejs.org | MIT | 启动器 3D 卡带（cart3d.js） |
| Draco decoder | 随 three.js 0.169.0 | https://github.com/google/draco | Apache-2.0 | cart3d.js 模型解码 |
| uPlot | 1.6.32 | https://github.com/leeoniya/uPlot | MIT | Dashboard 图表 |
| AOSP API 19 guest 运行时 | Android 4.4.4 r2.0.1 | https://android.googlesource.com（固定清单见 `data/android/19/source-manifest.xml`） | 见随包逐库 NOTICE（Apache-2.0、BSD、OpenSSL、STLport、zlib、ICU 等） | boot DEX、Bionic libc/libm/libdl/libstdc++、libcrypto、libstlport、libz、ICU、字体与 tzdata |

仅构建与测试使用、不进入发行物：

| 组件 | 版本 | 来源 | 许可证 | 用途 |
| --- | --- | --- | --- | --- |
| doctest | 2.4.11 | https://github.com/doctest/doctest | MIT | 单元与契约测试 |
| TypeScript、Vite、Vitest 等 npm 开发依赖 | 见 `webui/package-lock.json` | npm | 各自许可证 | Web UI 构建与测试 |

Dynarmic 的递归 submodule 版本由其固定提交的 `.gitmodules` 与 gitlink 决定，各组件
许可证位于对应源码目录；按目标架构只编入 oaknut（arm64）或 Xbyak/Zydis（x86-64）。
API 19 Android guest 运行时的逐库 NOTICE 随 `data/android/19/notices/` 发布。ANGLE 与
FFmpeg 预编译包随各平台目录携带许可证，清单记录上游 commit 与构建参数。规划中但尚未引入
发行物：zstd 及其它 API 版本的 AOSP Bionic。任何游戏 APK、OBB、解包资源或设备提取的
系统库均不得提交或再分发。

## PowerVR Native SDK PVRTC decompression algorithm

OGPlay vendors the unmodified `PVRTDecompress.cpp` and `PVRTDecompress.h`
from PowerVR Native SDK commit `2b1bf2f14d3365d0bb801e2a6a131a319d3a2e48`.

- `PVRTDecompress.cpp` SHA-256: `74559c5a4b8161aafce1ebe984bf8060896feaa81276b614491dbee755d6e4c7`
- `PVRTDecompress.h` SHA-256: `67123a36b99df380af76a65f3dfa3a7f368a153f5401b36a9ff1ed233cee6f5f`
- `LICENSE.md` SHA-256: `b1aea79afab593649ede742eccbb7feb74d216d52d120b99c4d349133871ba9f`

Copyright (c) Imagination Technologies Ltd.

Licensed under the MIT License:
https://github.com/powervr-graphics/Native_SDK/blob/master/LICENSE.md
