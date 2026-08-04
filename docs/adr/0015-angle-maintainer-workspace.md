# ADR-0015 · ANGLE 维护者工作区归属

日期：2026-08-04

Supersedes：ADR-0014 中保留 `third_party/angle` 源码 submodule 的部分。

## 背景

普通 OGPlay 开发已经只消费 `angle-prebuilt`，继续在主仓库保留 ANGLE 源码 gitlink 会让
checkout、CI 和依赖边界仍然暴露一个无需构建的源码依赖。完整 gclient 工作区和原始归档
只服务二进制 SDK 的维护者生产流程。

## 决定

- OGPlay 删除 `third_party/angle` submodule，只保留 `third_party/angle-prebuilt` 消费依赖。
- 本机 ANGLE checkout 与原始归档归入未跟踪的 `.local/angle-prebuilt-repo/` 维护工作区；
  它们不得提交到二进制仓库。
- 源码 commit 固定在 `tools/build_angle.py`，构建前必须与本地 checkout 完全匹配；生成的
  SDK manifest 继续记录相同 commit 和 GN 参数，升级时二者必须同一 WU 更新。
- `--source` 允许其他维护者使用任意本地绝对目录，但不放宽 commit 校验。

## 后果

普通 OGPlay clone 不再获取任何 ANGLE 源码；维护者仍可复用既有 15.5 GB 增量工作区生成
三平台 SDK。源码工作区的备份与清理由二进制仓库维护流程负责，不属于 OGPlay 消费流程。
