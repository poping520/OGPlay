# 已知问题与长期限制

本文件记录跨会话仍然有效、但不一定阻塞当前 Work Unit 的事项。已解决项由 Git 历史保留，
不在这里累积；`CURRENT.md` 只列当前阻塞。

| ID | 状态 | 目标阶段 | 说明 |
| --- | --- | --- | --- |
| KI-0001 | active | 持续 | `cpu.interpreter` 是确定性参考/诊断后端，未覆盖完整 A32/Thumb-2、VFP/NEON、原子访问和异常模型；主执行后端为 Dynarmic，缺失指令必须明确失败。 |
| KI-0003 | planned | M4 | Linux 无显示环境可使用 SDL Unix-console；可见窗口构建仍需要 X11 或 Wayland 开发依赖。 |
| KI-0006 | active | 持续 | hosted CI（`.github/workflows/ci.yml`）已在 GitHub Actions 运行；最近 3 次 main 推送（2026-10-11、10-12）均在约 1–2 分钟内失败，原因未诊断。本地与测试机结果不替代 hosted CI 状态。 |
| KI-0007 | upstream | 持续 | doctest 2.4.11 在 CMake 4.x 配置期产生上游旧 policy deprecation warning，不影响 warnings-as-errors 编译与测试。 |

## 开发验证约束

- Windows 本机只使用 MSVC 预设，不再使用 Cygwin。
- 测试机连接信息、凭据和外部绝对路径不得写入仓库文档。
