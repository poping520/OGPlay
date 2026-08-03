# ADR-0009 · Bionic 基线改为 API 19/22/23

- 状态：Accepted
- 日期：2026-08-03
- Supersedes：ADR-0002 中的 API 版本矩阵，不改变“默认加载真实 AOSP Bionic”的决定

## 背景

M2 可用的开发期 Bionic oracle 覆盖 Android API 19、22、23；原计划的 API 25 不再是
当前支持目标。版本矩阵必须在 loader、syscall 和 Bionic profile 开发前统一。

## 决定

M2 支持的 Android API 固定为 19、22、23。ROM 或设备提取物只导入被 Git 忽略的本地
oracle 目录，用于 ABI 分析和行为比较；发行数据仍必须来自可追溯的 AOSP 构建产物。

## 后果

- loader、Bionic profile、syscall 差异表和测试矩阵统一使用 19/22/23；
- API 25 不再属于当前支持范围；
- 本地 oracle 清单不得包含设备身份、外部绝对路径或访问凭据。
