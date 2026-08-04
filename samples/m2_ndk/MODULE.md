# 模块：m2_ndk sample

## 职责

提供 M2 无界面累计出口 `.so`，验证真实 Bionic、pthread、文件 syscall/VFS 与 allocator。

## 公共边界

- 导出普通 C 函数 `ogplay_m2_entry(const char* path)`，返回 0 表示全部契约通过。
- Android API 19、`armeabi-v7a`，不依赖 Activity、JNI、EGL/GLES、音频或游戏资源。
- 构建工具通过参数或环境变量定位，不记录开发机绝对路径。

## 不变量

- 必须实际创建并 join 一个 pthread，主/子线程都执行 malloc/free。
- 必须通过 open/write/lseek/read/close 读回并校验线程产生的数据。
- 任一阶段返回不同负数，禁止把缺失能力伪装成成功。

## 验证

`tools/build.py` 校验 ELF32/ARM、SONAME、普通 C 导出及 libc/pthread/file/malloc 导入。
