# WU-GLCTX-05 · 删除 mixed GLES1/GLES2 draw redirect

目标：删除按 symbol origin 推断 renderer 的临时补丁，由统一 Context 的 guest-visible
draw state 选择 fixed-function 或 programmable path。

验收：

- [x] renderer policy 同时检查 current program、fixed vertex array 与 programmable attrib。
- [x] library origin 不参与 Context ownership 或 renderer identity。
- [x] mixed VBO offset 0 场景经 programmable path 正常执行，不再依赖 split-state 诊断。
- [x] GLCTX 阶段统一构建、full CTest 506/506；Asphalt 6 exact 越过旧 mixed draw
  故障，停在后续 `CallStaticIntMethodV` class reference 边界。

非目标：HLE string routing 在后续 WU-HLE 系列消除。
