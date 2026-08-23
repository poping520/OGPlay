# BND-7 · Boundary ownership 拆分

## 目标

把仍位于 facade 翻译单元内的 module、service 与通用 Fast Host Call 机制迁入对应目录，
使 `AndroidBoundaryHle` 只负责 session composition/lifecycle。

## 交付与验收

- [ ] `core/` 拥有 binding、thunk arena、fast router 与 pending fault，且不依赖
  `modules/`/`facade/`。
- [ ] `services/` 拥有 graphics context、Android memory 与 frame service。
- [ ] Android/EGL/GLES1/GLES2 concrete module implementation 位于各自 module 目录。
- [ ] module 只构造注入所需 core/service，不依赖 facade `Impl`。
- [ ] facade 只组装 service/module、管理 surface/input/frame/stats 与公共入口。
- [ ] architecture gate 机器验证依赖方向与 direct hot-path 结构。
- [ ] focused 与全量 CTest 通过。

