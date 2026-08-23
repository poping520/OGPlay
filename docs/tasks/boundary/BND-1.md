# BND-1 · Virtual SO 基础与完整 ELF

## 目标

以 API 过滤并封口的 `BoundaryCatalog` 取代 Bionic Profile 的平行 SONAME 表，且
synthetic Virtual SO 从 catalog 发布完整 active exports，不再按当前 imports 裁剪。

## 交付与验收

- [x] 5 个已有实现的 Virtual SO 具有确定性 module/local/global slot 布局。
- [x] `BoundaryModuleInstance` 提供无继承的运行期 type erasure 形状。
- [x] namespace、dependency closure、dynamic loader 共用 catalog 判定 SONAME。
- [x] synthetic dynsym 来自 provider 的完整 module export 集。
- [x] late import 可从既有 Virtual SO 解析首次未导入的 export。
- [x] focused tests：Bionic profile/namespace 与 boundary descriptor。

非目标：本 WU 保留 legacy slow transport、`HleRoute` 与中央 dispatch。
