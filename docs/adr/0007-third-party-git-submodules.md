# ADR-0007 · 第三方源码依赖统一使用 Git submodule

- 状态：Accepted
- 日期：2026-08-03
- Supersedes: ADR-0005 中的依赖获取方式

## 背景

M1 开始引入 SDL3 与 Dynarmic。配置期下载和混用系统包会让同一提交在不同机器上得到
不同依赖源码，也不利于离线复现与许可证审计。

## 决定

所有需要源码参与构建的第三方库以 Git submodule 放在 `third_party/`，主仓库 gitlink
固定到明确提交。包含自身 submodule 的依赖必须递归初始化。CMake 不使用 FetchContent，
CI checkout 必须启用 recursive submodules；依赖未初始化时配置明确失败。

## 后果

- 克隆后必须执行 `git submodule update --init --recursive`。
- 依赖升级通过单独 Work Unit 更新 gitlink、许可证记录并完成全量构建测试。
- 默认配置不访问网络，同一主仓库提交对应唯一第三方源码集合。
