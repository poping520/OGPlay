# GUI-12 · 导入数据包归游戏库所有

状态：macOS 完成，2026-10-20；GUI 实际点击与 Windows/Linux 未验。依赖：GUI-10、VFS-09。决策：[ADR-0144](../../adr/session.md#adr-0144)。

目标：导入后游戏不再依赖原始下载目录。数据包默认复制进库，引用原目录保留为显式选项；
导入按钮文案不再出现“实例”。

## 范围

- hal：`TryCloneFile` 在 macOS 用 clonefile、Linux 用 FICLONE，Windows 返回不支持。
- library：BuildLibraryImport 接受 copy/link；Import 在库锁外暂存复制（克隆优先，回退流式，
  回退前检查空间），带进度与取消，库锁内随条目原子发布到 `data/`。meta 用
  `external_data = "library"` 表示库内数据，加载时解析为条目 `data/` 绝对路径；旧 meta 按 link。
  移除条目同时删除库内数据。CLI `library import` 新增 `--link-external`。
- GUI：导入向导选择复制/引用，导入时显示复制进度并可取消；详情区分“已复制到游戏库”与
  “引用原目录”；主按钮改为“导入到游戏库”，移除说明同步新语义。

- 重新导入（ADR-0145）：同包残留存档沿用原 id，不再产生 `-2`、`-3`；移除可选“同时删除存档”
  （GUI 勾选、RPC `delete_saves`、CLI `--delete-saves`）。

不做：zip/obb 自动解压、多份数据去重、已导入条目的模式转换、Windows 块克隆。

## 验证

只构建 Release ogplay_app_tests 与 ogplay-cli。ogplay_app_tests 全部 78 用例/1782 断言通过，
新增库内复制（嵌套/空目录、进度、源删除后仍可 ResolveLaunch、设置覆盖转 link、移除删除
data 保留存档）及符号链接/与库根重叠/发布前取消/残留暂存清理回归。webui check 47 用例通过。
capabilities check 通过。

真实数据：隔离库 CLI 导入 Dungeon Hunter P1000 APK 与 312 MB/2071 文件数据包，APFS 克隆
0.55 秒，磁盘可用空间未下降，`diff -rq` 与源一致，meta 不含源路径，暂存目录只剩锁文件。
以库内 `data/` 运行 run-apk（/sdcard/gameloft/games/DungeonHunter、1024×600）呈现 900 帧，
退出 0。证据：`.local/gui12-owned-data/`（import.json/time、run.log、exit.txt）。

重新导入：库测试覆盖“导入两份→移除 -2→再导入仍为 -2 且存档/meta 不变”、删除存档后
编号重新分配为全新沙盒、meta 属于其他包的残留不沿用；RPC 测试覆盖 delete_saves 类型校验
与删除存档。真实 CLI（隔离库、Dungeon Hunter APK）：导入→写入存档标记→移除→再导入仍为
裸 id 且标记保留；`--delete-saves` 移除后沙盒为空，再导入得到全新沙盒，库内只有一个裸 id 条目
（`.local/gui12-reimport/log.txt`）。重建 OGPlay.app，GUI 冒烟与文档/能力检查通过。

缺口：未在 OGPlay.app 中实际点击导入向导（复制/引用选项、进度、取消）；流式复制回退与
空间不足只由逻辑覆盖，未在非 APFS 卷实测；Windows/Linux 未构建。
