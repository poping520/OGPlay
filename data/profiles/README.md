# Title Profiles

每个游戏配置命名为 `<package>.profile.toml`，必须是纯数据且不超过 200 行。身份至少包含
package、versionCode 和 `.so` SHA-256；匹配失败时由通用 profile/引擎指纹处理，禁止猜测。

允许描述：API level、ABI、生命周期模板、逻辑表面、资源清单、VFS 挂载、声明式 Java
绑定、已注册 quirk、输入模板。任何字段都必须对应已有通用机制，不能嵌入脚本或代码。

M5 迁移 DEMO 前不提交具体游戏 profile；游戏名、厂商名、包名只能在本目录出现。

