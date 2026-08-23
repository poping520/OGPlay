# BND-7 · Boundary ownership 拆分

## 目标

把仍位于 facade 翻译单元内的 module、service 与通用 Fast Host Call 机制迁入对应目录，
使 `AndroidBoundaryHle` 只负责 session composition/lifecycle。

## 交付与验收

- [x] `core/` 拥有 binding、thunk arena、fast router 与 pending fault，且不依赖
  `modules/`/`facade/`。
- [x] `services/` 拥有 graphics context、Android memory 与 frame service。
- [x] Android/EGL/GLES1/GLES2 concrete module implementation 位于各自 module 目录。
- [x] module 只构造注入所需 core/service，不依赖 facade `Impl`。
- [x] facade 只组装 service/module、管理 surface/input/frame/stats 与公共入口。
- [x] architecture gate 机器验证依赖方向与 direct hot-path 结构。
- [x] 20 项 ownership/fault/preflight/late-load/shared-GL focused CTest 与 architecture
  gate 通过；会话最终全量 CTest 按总任务要求在 OpenSL 完成后执行。
