# 子模块：runtime/boundary/modules/gles3

## 职责

- 承载 API 19 GLES3 相对 GLES2 的 104 项新增 core handler；导出仍属于
  `libGLESv2.so`，只有 current EGL Context client version 为 3 时允许执行。
- 标量调用通过 `AngleFrame` 进入真实 ANGLE；guest 指针在 ANGLE 调用前完整预检，输出
  成功后一次提交。64 位参数按 A32 AAPCS 偶数字槽对齐，sync/map 返回值不得暴露 host 指针。
- 复用 EGL registry 当前 Context、`GuestGlContext` error 和执行锁，不拥有第二套 Context、
  Surface、对象表或 Java 状态。

## 当前边界

104/104 项均已进入真实 ANGLE。普通/压缩 3D texture 按 ES3 unpack 状态计算 guest 范围；
多指针 query 先预检全部输出再提交；transform-feedback 与 uniform 名称数组逐项受界读取；
map-buffer 通过 `0x78000000` guest arena 隔离 host 指针，并在 flush/unmap 时回写。


## BND-34 安全与状态修复

UBO ACTIVE_UNIFORM_INDICES 先查询真实元素数；sampler/vertex/64 位 state query 精确分配。
Uniform 查询从 native program 刷新宽度。二维与三维 PBO 地址是 buffer offset，普通 guest
pixel 指针按 row/skip 计算范围。VAO bind/delete 同步属性和 element buffer shadow；整数
属性与 divisor 保存原类型。map identity 使用 share-group + GLuint，验证 native mapped
状态后才能访问保存的 host 指针；arena 重用空闲区，显式 flush 只提交指定范围，unmap
不得再次覆盖它，share group 最后成员销毁时退役其 map/sync 记录。
WRITE-only 且未 invalidate 的 mapping 内部升级为 READ|WRITE host access 以预取旧内容，
guest access 身份仍为 WRITE-only，保证局部修改不会覆盖未修改字节。

扩展数量、glGetString、glGetStringi 共用受检发布清单，禁止透传整个 ANGLE extension list。
