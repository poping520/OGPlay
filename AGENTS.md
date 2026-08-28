# OGPlay 开发约定

## 开始工作前

1. 使用 UTF-8 读取代码和文本。
2. 阅读 `docs/state/CURRENT.md`、`docs/tasks/<里程碑>/` 下的相关任务单、相关模块的
   `MODULE.md` 以及相邻模块契约；已完成阶段只在需要追溯时读取验收文档。
3. 在 `capabilities.toml` 中确认能力现状，不凭猜测补实现。
4. 适配新游戏、跑测试或排查症状前，先读 `docs/playbook/` 下对应的操作手册
   （总览 `docs/playbook/README.md`），按既有流程做，不要另起一套。

## 范围边界

OGPlay 是安卓老游戏兼容层，不是 Android 模拟器。只实现游戏进程会直接调用的能力。
禁止引入完整 Android 系统、Binder/system_server/Zygote、完整 ART/Dalvik、Play 服务、
现代支付/社交/反作弊或手机端运行能力。范围有争议时先写 ADR。

## 架构规则

- 依赖方向只能由上层指向下层；回调必须通过显式接口。
- `src/` 中禁止出现游戏名、厂商名、包名和游戏专属分支；差异只能进入 `data/profiles/`。
- 每个生产代码目录必须有 `MODULE.md`；契约与代码冲突时以契约为准。
- guest 指针必须使用强类型包装。
- 未实现能力必须记账、可查询并明确失败，禁止伪造成功和静默返回零。
- 所有时间源必须通过统一 Clock；线程模型为一个 guest 线程对应一个宿主线程。
- 图形使用 ANGLE，窗口与输入使用 SDL3；禁止重新引入手写 GLES 到桌面 GL 转译。
- 禁止在 `src/` 裸用 `printf`、`std::cout`、`std::cerr`；使用结构化日志。

## 工作单与验证

- 一个 Work Unit 应能在单次会话完成，目标一句话可说明，依赖显式。
- 每个行为变更必须有机器可判定的测试；quirk 必须有“关闭即失败”的测试。
- 修改模块时同步更新 `MODULE.md`；架构决定写入只追加的 ADR。
- 代码改动只构建受影响目标，并运行与改动直接相关的单点/定向测试；没有用户明确要求，
  禁止运行全量测试。Windows Visual Studio 2026 环境使用对应的 `windows-msvc` 预设。
- 纯文档改动只做 UTF-8、链接和差异静态检查，不因此触发构建或测试。
- 结束工作前把 `docs/state/CURRENT.md` 更新为滚动快照，并核对 `capabilities.toml`；
  能力有变化时同步更新，状态只能前进不能后退。
