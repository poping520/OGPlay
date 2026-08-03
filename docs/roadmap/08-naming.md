# 08 · 项目命名

## 决定

**项目定名 `OGPlay`。**

`OG`（original / 元老、经典）+ `Play`。名字直接说清了项目干什么：**玩那些老game**。

优点：

- **语义直白**，不需要解释；技术圈和普通玩家都能一眼理解定位；
- **短、易拼、易读**，中英文语境都无负面歧义；
- **作为复合词有足够辨识度**，不会像 `Tales`、`Ember` 这类常用词那样被同名事物淹没；
- 规避了全部三条命名雷区（见下）。

---

## 命名雷区（定名时遵守的硬约束）

1. **不含 "Android"。** Android 是 Google 的注册商标，用在同类软件产品名里风险高。
2. **不含 "Droid"。** DROID 是卢卡斯影业的注册商标（授权给 Verizon/Motorola），
   历史上有过针对第三方软件的维权。`*droid` 这类命名看着自然，实际是雷区。
3. **不含 "Emulator" 做主名。** 一是这个项目严格说是**兼容层 + CPU 翻译**，
   叫模拟器不准确；二是不利于品牌化。

---

## 命名约定

| 对象 | 约定 |
| --- | --- |
| CLI 可执行文件 | `ogplay` |
| 内核库 | `libogplay` |
| C++ 命名空间 | `ogplay::` |
| 图形界面 | OGPlay |
| 游戏配置档 | `<package>.profile.toml`（位于 `data/profiles/`） |
| 题库/兼容性数据库 | OGPlay Compatibility DB |
| Agent 接口 | `ogplay-agent`（MCP server 名 `ogplay`） |
| 环境变量前缀 | `OGPLAY_` |
| 日志分类前缀 | `ogplay.<subsystem>`（见 [09](09-logging.md)） |
| 用户数据目录 | `~/.local/share/ogplay` · `%APPDATA%\OGPlay` · `~/Library/Application Support/OGPlay` |

**书写规范**：正式书面统一写 `OGPlay`（O、G、P 大写）。
标识符、路径、命令行一律小写 `ogplay`。不要出现 `OGplay` / `ogPlay` 等变体。

---

## 定名后待办

- [ ] GitHub 组织/仓库名确认（`ogplay`）
- [ ] 域名确认（`ogplay.dev` / `ogplay.org`）
- [ ] 目标市场商标检索（重点确认游戏软件类目无冲突）
- [ ] 设计 wordmark 与图标
- [ ] 代码内旧前缀统一迁移为 `ogplay` / `OGPLAY_`

