# BND-30 · GLES1 固定管线光照与纹理环境闭合

## 目标与依赖

依赖 BND-27，补齐已发布 GLES1 core 固定管线的光照与纹理环境绘制语义。

## 交付

- fixed shader 消费 LIGHT0..7 enable、ambient/diffuse/specular、position、spot、衰减，
  以及前后材质、emission/shininess、two-side 和 color-material。
- texture environment 增加 GL_BLEND 与 GL_DECAL，继续按 level-zero base format 区分
  alpha、color、color-alpha 组合。
- GL_OES_matrix_palette 未在扩展字符串宣告；状态入口保留，draw 继续明确失败，不伪装支持。

## 验证

- Windows Release `ogplay_tests` 受影响目标构建通过。
- GLES1 定向 18 用例/1510 断言通过，覆盖真实 ANGLE shader 编译、固定管线 draw、
  GL_BLEND/GL_DECAL 执行及 shader 状态消费结构。
