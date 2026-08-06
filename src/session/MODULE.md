# 模块：session

## 职责

编排识别、Profile、VFS、生命周期、存档、输入、统一 Clock 和运行检查点。

## 公共 API

- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：M0 确定性空会话。
- `LoadTitleProfileText` / `LoadTitleProfile`：M5 将 v1 纯数据 TOML 严格转换为
  identity/runtime/data/audio/java/quirks/input 强类型模型。
- `TitleProfileCatalog::LoadDirectory/Match`：稳定加载 profile 目录，并且只在 package、
  versionCode 与 `.so` SHA-256 三项全部命中时返回 profile。
- `QuirkRegistry::Load/Validate`：严格加载 `data/quirks.toml` 的理由、风险、owner 与
  测试引用；含 quirk 的 Profile 目录必须显式通过注册表验证。
- M1 将相同状态机装配 guest，后续 M5 WU 增加 Profile 生命周期模板。

## 不变量

- 生命周期只使用 native_activity、gl_surface_view、custom_jni 通用模板。
- 游戏身份信息只有 Title Profile 一个来源；无 profile 也使用通用默认值。
- Profile 文件为 UTF-8 纯数据且不超过 200 行；未知字段、路径逃逸、非法或歧义身份失败。
- data/audio/java/quirks/input 只保存声明，不直接调用相邻模块或执行脚本。
- 每个 enabled quirk 必须有注册定义和可定位测试；未注入注册表时不得进入匹配目录。
- 状态推进可由固定帧步进驱动，不依赖 sleep。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。

## 测试

`tests/session/` 的状态机、Title Profile、精确身份匹配、确定性与题库检查点测试。
