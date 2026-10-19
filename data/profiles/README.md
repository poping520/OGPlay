# Title Profiles

Profile 是“仅 quirk + 输入模板的精确身份覆盖层”（ADR-0141），文件名为
`<package>.profile.toml`，UTF-8、纯数据、不超过 200 行。只接受 v4；v1/v2/v3
直接拒绝，不提供迁移或兼容分支。

```toml
schema = 4
[identity]
package = "org.example.game"
version_code = [7]
so_sha256 = ["0000000000000000000000000000000000000000000000000000000000000000"]

[input]
template = "stick_action"
```

`package` 必填并精确匹配；`version_code` 对应 C++ `version_codes`，与 `so_sha256`
均为可选约束，声明时必须是非空、不重复的数组。哈希匹配 APK inventory 中任意 `.so`，
不选根库或 ABI；同包适用范围交叠失败。未命中 Profile 继续通用启动。
`identity.name` 不再支持，显示名称来自 APK。

可选 `[quirks]` 包含 `enabled` 与每个启用项的参数表，必须在 `data/quirks.toml`
注册并有“关闭即失败”的测试。可选 `[input]` 沿用 template/zone/binding 合并机制，
用户配置优先；发布策略仍遵守 ADR-0134。

API 固定为 19；ABI 和入口来自 APK inventory 与 Manifest MAIN/LAUNCHER。
逻辑表面默认 800×480，VM 与 guest call 使用各自默认值。外部目录由显式运行选项
提供；`--obb` 将原文件挂到 `/sdcard/Android/obb/<package>/<filename>`，游戏按标准
路径自行打开；不解包到自定义路径，不做资源清单预检。音乐与音效走标准 MediaPlayer /
SoundPool。Profile 禁止 runtime/data/audio/Java 预置或脚本。

机器契约：[title-profile-v4.schema.json](title-profile-v4.schema.json)。验证：

```sh
python3 tools/validate_title_profiles.py --schema data/profiles/title-profile-v4.schema.json --profiles data/profiles
python3 tools/validate_quirks.py --registry data/quirks.toml --profiles data/profiles --root .
```

校验脚本需要 Python 3.11+。
