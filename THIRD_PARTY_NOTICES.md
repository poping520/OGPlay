# Third-party notices

本文件记录 OGPlay 构建或发行所包含的第三方组件。所有源码依赖以 Git submodule 固定
到明确提交；上游自带的许可证文件随 submodule 保留。

| 组件 | 版本 | 来源 | 许可证 | 用途 |
| --- | --- | --- | --- | --- |
| doctest | 2.4.11 | https://github.com/doctest/doctest | MIT | 单元与契约测试 |
| SDL | 3.4.10 | https://github.com/libsdl-org/SDL | zlib | 窗口、输入与宿主多媒体 HAL |
| Dynarmic | `05b7ba50588d1004e23ef91f1bda8be234be68f4` | https://git.eden-emu.dev/eden-emu/dynarmic | ISC | ARM 动态翻译后端 |
| ext-boost | 1.90（`6a85c3100499e886e11c87a5c2109eedacea0a61`） | https://github.com/azahar-emu/ext-boost | Boost Software License 1.0 | Dynarmic 所需的裁剪 Boost 头文件 |
| Boost.Pool | 1.90（`740c8076f9d02f0216e8f3dbb15d2fd80f67d7f4`） | https://github.com/boostorg/pool | Boost Software License 1.0 | 补充 Dynarmic 使用的 pool allocator 头文件 |

Dynarmic 的递归 submodule 版本由其固定提交的 `.gitmodules` 与 gitlink 决定，各组件
许可证位于对应源码目录。规划中但尚未引入发行物：ANGLE、Qt 6、FFmpeg、zlib、zstd
及 AOSP Bionic。任何游戏 APK、OBB、解包资源或设备提取的系统库均不得提交或再分发。
