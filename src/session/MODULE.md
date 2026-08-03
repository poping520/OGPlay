# 模块：session

## 职责

编排识别、Profile、VFS、生命周期、存档、输入、统一 Clock 和运行检查点。

## 公共 API

- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：M0 确定性空会话。
- M1 将相同状态机装配 guest，M5 增加 Profile 生命周期模板。

## 不变量

- 生命周期只使用 native_activity、gl_surface_view、custom_jni 通用模板。
- 游戏身份信息只有 Title Profile 一个来源；无 profile 也使用通用默认值。
- 状态推进可由固定帧步进驱动，不依赖 sleep。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。

## 测试

`tests/session/` 的状态机、确定性与题库检查点测试。
