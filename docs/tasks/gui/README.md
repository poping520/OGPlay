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
| [DASH-01](DASH-01.md) | Dashboard agent 只读聚合、事件游标与线程关联 | agent 层完成 |
| [DASH-03](DASH-03.md) | Dashboard HTTP、顶栏/拓扑/时间轴/线程表 | Windows 完成；其余来源/面板待后续 |

GUI-7 按设计接入；Linux 暂缓。Dashboard 任务也放在本目录，按
[Dashboard 规划](../../design/gui/dashboard.md) 使用 `DASH-xx.md` 编号。

启动器任务使用 `GUI-x.md` 编号；旧版启动器任务保留在 [launcher](../launcher/README.md)。
