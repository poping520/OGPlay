# 子模块：GLES1

GLES1 core/选定 OES 扩展编组、固定管线状态、client arrays 和 shader draw。绘制使用 ANGLE，
不实现桌面 OpenGL 转译。fixed、legacy、matrix palette 和 client array 状态按 Context 保存；
VBO 内容按 share group 共享。OES map/query/unmap 使用共用 BufferMappingService，与
GLES2 OES/GLES3 core 保持同一身份；回写和缓存同步由 services 负责。

Matrix palette 四入口、32 矩阵、最多 4 权重通过受检 RAM/VBO 数组生成位置和法线。
关闭 lighting 时奇异 modelview 不触发 normal inverse；fog 只混合 RGB，二维纹理坐标保留
投影 Q，双面 PRIMARY_COLOR 使用实际面颜色。DrawArrays 与 flat 展开不受 16 位索引限制。
light position 与 spot direction 在设置时按当前 modelview 分别执行位置变换和
inverse-transpose 方向变换；flat shading 同时覆盖三角形与 line/strip/loop。
GL_OES_framebuffer_object 的 15 个独立 OES ABI 入口复用唯一 ANGLE 对象与 share-group
元数据，扩展字符串只在整族 handler 已绑定时发布。
在 ES3 native Context 上执行固定绘制时使用内部 VAO，完成后恢复可编程 VAO 与属性常量。
读回只提交像素行，保留 padding。测试：`BND34 GLES1*` 与 `gles1_fixed_tests.cpp`；整体
一致性仍受已发布扩展与父级 boundary MODULE 的范围约束。

压缩格式计数、数组与 boolean/float/integer 查询共用下层实际上传能力；ATC 格式
元数据沿现有 share-group 保存，禁止的子图更新在 guest 搬运前返回 GL error。

API19 OES/IMG 的 ABI 相同核心别名复用原 handler；blend OES 直接执行 ANGLE。
draw-texture 的八入口复用固定 shader 与 ANGLE 三角形绘制，支持共享纹理对象的
level-zero 尺寸及 crop 矩形、负 crop 翻转；忽略矩阵/数组/lighting/clip/cull，
保留当前颜色、纹理环境、零距离 fog、深度及片元操作，绘制后恢复可编程状态。
仍限现有两个 2D 纹理 stage，超限明确失败，不因此发布 GL_OES_draw_texture。
其余未支持入口遵守父级 API19 gl_unimplemented 规则。
