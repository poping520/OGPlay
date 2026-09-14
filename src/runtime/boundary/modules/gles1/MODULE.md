# 子模块：GLES1

GLES1 core/选定 OES 扩展编组、固定管线状态、client arrays 和 shader draw。绘制使用 ANGLE，
不实现桌面 OpenGL 转译。fixed、legacy、matrix palette 和 client array 状态按 Context 保存；
VBO 内容按 share group 共享。OES map 返回 guest arena 地址，回写前校验 native 映射身份。

Matrix palette 四入口、32 矩阵、最多 4 权重通过受检 RAM/VBO 数组生成位置和法线。
GL_OES_framebuffer_object 的 15 个独立 OES ABI 入口复用唯一 ANGLE 对象与 share-group
元数据，扩展字符串只在整族 handler 已绑定时发布。
在 ES3 native Context 上执行固定绘制时使用内部 VAO，完成后恢复可编程 VAO 与属性常量。
读回只提交像素行，保留 padding。测试：`BND34 GLES1*` 与 `gles1_fixed_tests.cpp`；整体
一致性仍受已发布扩展与父级 boundary MODULE 的范围约束。
