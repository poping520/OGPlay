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
- [x] API range 在 seal 时过滤 inactive module/export；local id 无需连续或等于数组序号。
- [x] link preflight 只装配 `BionicProfile + BoundaryCatalog` 生成的符号元数据与 loader，
  不构造 graphics runtime，也不接受 backend/surface 参数。

历史说明：首轮 BND-1 保留的 legacy dispatch 已由 BND-4 收口；本次闭环不新增 WU。
