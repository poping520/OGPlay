# 黄金帧基线

基线按 `<profile-id>/<checkpoint>.png` 存放，并由题库检查点记录分辨率、软件渲染后端、
像素差阈值和感知哈希阈值。更新基线必须独立评审，不得用重录覆盖回归。

M0 仅验证无 GPU 的比较算法；M4 接入 ANGLE + SwiftShader/llvmpipe 后再加入实际画面。

