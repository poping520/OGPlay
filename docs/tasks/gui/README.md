# GUI · WebView 启动器

按 [GUI v2 规划](../../design/gui/README.md) 顺序推进。

| WU | 目标 | 状态 |
| --- | --- | --- |
| [GUI-1](GUI-1.md) | Windows WebView 宿主、RPC 与静态构建骨架 | Windows 完成 |
| [GUI-2](GUI-2.md) | 网格/列表/详情与筛选 | 已提交；原生拖放待验收 |
| [GUI-3](GUI-3.md) | APK 导入向导与原生文件/目录选择 | Windows 主流程完成；跨窗口拖放待验收 |
| [GUI-4](GUI-4.md) | 全局设置、配置升级与已有启动参数接入 | Windows 完成；运行时预留项明确标记 |
| [GUI-5](GUI-5.md) | 每实例继承/覆盖、启动模式与参数预览 | Windows 主流程完成；运行时/沙盒管理预留 |
| [GUI-6](GUI-6.md) | 运行实例、Dashboard 独立窗口与自动打开 | Windows 主流程完成 |
| [GUI-7](GUI-7.md) | 机型预设 schema、通用数据与校验 | 数据阶段完成；运行时接入另立任务 |
| [DASH-01](DASH-01.md) | Dashboard agent 只读聚合、事件游标与线程关联 | agent 层完成 |
| [DASH-02](DASH-02.md) | 堆/GC、JNI、CPU 缓存、权限、VFS/动态库快照 | Windows 完成；扩展 BootDex 回归有阻塞 |
| [DASH-03](DASH-03.md) | Dashboard HTTP、顶栏/拓扑/时间轴/线程表 | Windows 完成；新增来源/面板见 DASH-02/04 |
| [DASH-04](DASH-04.md) | 共享键联动、诊断面板与事件泳道 | Windows 完成；缺少精确发生键的指标明确标记 |
| [DASH-05](DASH-05.md) | Dashboard 首错/停滞排查与独立取证手册 | 文档完成；静态检查通过 |

GUI-1..7 已按各任务边界落地；未验收项见对应任务，Linux 暂缓。Dashboard 任务也放在本目录，按
[Dashboard 规划](../../design/gui/dashboard.md) 使用 `DASH-xx.md` 编号。

启动器任务使用 `GUI-x.md` 编号；旧版启动器任务保留在 [launcher](../launcher/README.md)。
