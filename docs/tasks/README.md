# Work Unit 索引

Work Unit 按所属里程碑存放，创建后不因完成状态移动。文件名中的四位编号全局唯一且持续
递增；任务之间使用编号声明依赖，具体内容通过本表定位。

| 里程碑 | 编号范围 | 状态 | 目录 | 验收记录 |
| --- | --- | --- | --- | --- |
| M0 | WU-0001..0014 | 完成 | [`m0/`](m0/) | [`M0-ACCEPTANCE.md`](../state/M0-ACCEPTANCE.md) |
| M1 | WU-0015..0040 | 完成 | [`m1/`](m1/) | [`M1-ACCEPTANCE.md`](../state/M1-ACCEPTANCE.md) |
| M2 | WU-0041..0103 | 完成 | [`m2/`](m2/) | [`M2-ACCEPTANCE.md`](../state/M2-ACCEPTANCE.md) |
| M3 | WU-0104..0149 | 完成 | [`m3/`](m3/) | [`M3-ACCEPTANCE.md`](../state/M3-ACCEPTANCE.md) |
| M4 | WU-0150..0169 | 进行中 | [`m4/`](m4/) | 待出口 |

## 规则

- 一个 Work Unit 在一次会话内形成一句话目标、显式依赖和机器可判定验收。
- 新任务直接创建在当前里程碑目录，例如 `docs/tasks/m4/WU-0150.md`。
- `CURRENT.md` 只保留正在进行和最近完成的少量 WU；完整历史保留在本目录、里程碑
  验收文档和 Git 中。
- 查找任务可使用 `rg --files docs/tasks | rg 'WU-0105.md'`，不要假定 WU 位于根目录。
