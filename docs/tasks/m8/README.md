# M8 · 兼容性冲刺

M8 以 M6 的 Profile-backed Scenario runner 为唯一 exact-title 验证出口，按
“先盘点、后分组、再实现”推进。不得退回人工 smoke，也不得为单个缺失符号
创建一个 Work Unit。

Asphalt 6 是首个 M8 目标，以
`docs/demo/games/Asphalt6/Asphalt_6_Adrenaline_HD_GLstore_v1.3.2.apk` 和对应外置
payload 为本地 fixture；APK、payload、证据和宿主绝对路径不进入 Profile、Scenario
或 Result。

适配按以下批次切分，只在实际开始时分配下一个全局 WU 编号：

| 顺序 | 批次 | 机器出口 |
| ---: | --- | --- |
| 1 | exact 身份、payload 布局和静态能力矩阵 | Profile 校验、ELF/Bionic 预检和聚合缺口清单 |
| 2 | JNI/Java 调用族 | 以 DEX、ELF 字符串和有界启动现场一次统计类/方法/签名/槽位，按通用语义批量实现 |
| 3 | GLES2/RTT 调用族 | 对目标 96 个 GL/EGL 导入做已绑定/未绑定矩阵，按 state/resource/query/draw 批量闭合 |
| 4 | 线程、VFS 与安卓边界 | 真宿主线程、外置数据和启动阶段在统一 session 中有界运行 |
| 5 | 音频/影片及其他主界面前置能力 | 只补真实命中的通用缺口，不伪造播放或网络成功 |
| 6 | 主界面 Scenario 与 gate | 启动→标题触摸→Main Menu golden→shutdown 连续三轮稳定 |

每个实现批次仍须遵守单 WU 不超过 10 个文件；批次内允许一次实现多个同类
函数，但不允许把 JNI、GLES、VFS 和线程无边界混在同一 WU。

已建工作单：

- [WU-M8-001](WU-M8-001.md)：JNI Guest Binding 统一注册入口。
- [WU-0360](WU-0360.md)：Asphalt 6 exact 身份、静态能力矩阵与根 SONAME 别名预检。
- [WU-0361](WU-0361.md)：exact APK + external payload 有界启动采样与四组缺口聚合。
- [WU-0378](WU-0378.md)：恢复 Dungeon Hunter 有界加载预算并补齐 run-apk 结构化进度日志。
- [WU-0379](WU-0379.md)：定位 Asphalt 6 混合 GLES draw 的 buffer-state 分裂根因。
