# 模块契约索引

依赖方向从上到下：`frontend/agent/session → runtime/gles/audio/input/loader → cpu/memory → hal → core`。
下层不得引用上层；跨层通知使用显式接口。

| 模块 | 契约 | M0 状态 |
| --- | --- | --- |
| core | `src/core/MODULE.md` | 日志、错误、配置、序列化骨架 |
| hal | `src/hal/MODULE.md` | 宿主边界接口占位 |
| cpu | `src/cpu/MODULE.md` | CPU 后端契约占位 |
| memory | `src/memory/MODULE.md` | guest 地址空间契约占位 |
| loader | `src/loader/MODULE.md` | 包/ELF/linker 契约占位 |
| runtime | `src/runtime/MODULE.md` | Bionic/syscall/JNI/framework 边界占位 |
| gles | `src/gles/MODULE.md` | ANGLE/IDL 边界契约占位 |
| audio | `src/audio/MODULE.md` | guest 音频 API 边界占位 |
| input | `src/input/MODULE.md` | 输入事件与映射占位 |
| session | `src/session/MODULE.md` | 会话编排占位 |
| agent | `src/agent/MODULE.md` | 结构化控制接口最小实现 |
| frontend | `src/frontend/MODULE.md` | CLI/GUI 边界；M0 只建 CLI |

