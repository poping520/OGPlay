# WU-GLCTX-04 · GLES1 fixed draw Native State Guard

目标：GLES1 fixed-function translator 可临时使用内部 GLES2 program/buffer/attribute，
但 draw 返回前必须恢复 guest programmable state。

验收：

- [x] `NativeGlState` 显式标记 fixed draw transaction，拒绝嵌套。
- [x] 成功与异常路径均显式恢复 current program、VBO/EBO、FBO、active texture/binding
  和 vertex-attrib enable/pointer。
- [x] fixed internal program 不写入 guest-visible `GL_CURRENT_PROGRAM`。
- [x] mixed contract test 在 fixed draw 后继续执行 GLES2 draw，并核对 program/buffer。
- [ ] GLCTX 阶段统一构建与测试。

非目标：programmable/fixed renderer 选择由 WU-GLCTX-05 收口。
