# Title Profiles

每个游戏配置命名为 `<package>.profile.toml`，必须是纯数据且不超过 200 行。身份至少包含
package、versionCode 和 `.so` SHA-256；匹配失败时由通用 profile/引擎指纹处理，禁止猜测。

允许描述：API level、ABI、生命周期模板、逻辑表面、资源清单、VFS 挂载、声明式 Java
绑定、已注册 quirk、输入模板。任何字段都必须对应已有通用机制，不能嵌入脚本或代码。

M5 迁移 DEMO 前不提交具体游戏 profile；游戏名、厂商名、包名只能在本目录出现。

v1 的机器契约见 `title-profile-v1.schema.json`。提交 profile 前运行：

```text
python tools/validate_title_profiles.py --schema data/profiles/title-profile-v1.schema.json --profiles data/profiles
```

校验器拒绝未知字段、非声明式 TOML 值、非规范 guest 路径、重复身份指纹、文件名与 package
不一致以及不受支持的 API、ABI 和生命周期模板。具体 quirk 还必须在 `data/quirks.toml`
注册并提供“关闭即失败”的测试；该跨文件约束由后续 M5 Work Unit 接入。
