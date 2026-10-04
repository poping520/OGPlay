# BND-51：按原生能力发布浮点纹理扩展

状态：实现与 macOS 定向验证完成；Windows/Linux 与 title gate 未验收。依赖：既有 ANGLE 纹理上传/采样、受检像素搬运与统一扩展清单。

范围：GLES2/3 在当前 ANGLE 扩展完整 token 存在时发布 GL_OES_texture_float；
GL_OES_texture_float_linear 另需基础浮点支持。保留 RGBA/FLOAT 原生精度与既有
client-memory/null 分配、cube 六面、subimage、unpack 与 ES3 PBO 边界。
三平台共用实现；不修改 APK、伪造格式指针或发布整个原生扩展列表。
GLES1、半浮点及浮点颜色附件不扩充；不引入新渲染后端或 CPU 纹理转换。

验收：完整 token/缺基础/缺 linear/重复与相似 token 条件测试；ES2/3 支持范围内的
字符串、索引及数量查询一致；真实 guest 2D/cube 浮点 nearest/linear 采样与超范围值
保真，null 分配/subimage/客户端越界、RGB 行对齐、尺寸溢出、PBO 区间验证。
原 APK 同路径 reached-fault 一次，记录原空格式指针读取消失及下一首错。
Windows/Linux/macOS 实跑分别记账；呈现帧不等于黑屏或 title gate 验收。

原错分析：纹理格式 0x01 对应 RGBA/FLOAT，原生后端已支持，guest 扩展过滤导致
格式查找返回 null；渲染线程读取 0x10。清理阶段 EVP_CIPHER_CTX_cleanup 只转报
进程首错。证据 `.local/tales-cubemap-fault-analysis/`。

验证证据：`.local/tales-float-texture-fix/`。结束前关闭全部 OGPlay 测试进程。

实际验证（2026-10-04）：
- Release ogplay/ogplay_tests 构建通过；BND51、既有 BND50 与像素对齐回归共
  8 用例/912 断言通过。ES2 禁用 ES3 索引入口，ES3 索引/64 位数量查询与同一清单一致。
  macOS Metal 实际执行 nearest/linear、2D/六面 cube 上传、超范围浮点值采样，
  客户端/PBO 越界及尺寸溢出受检。未重建无关 BootDex/guest JNI。
- 原 APK、无 Profile/survey、隔离空沙盒：旧渲染线程读取 0x10 的故障消失，
  MCP 在 f=1790/presentedFrame=73 报下一首错：
  VideoView.setOnPreparedListener(MediaPlayer.OnPreparedListener) 方法解析缺口，
  来自 fCoreJava.fVideoView 构造与 fAndroidHarness$7.run。仅 reached-fault。
- MCP shutdown 后进程未在限时内退出，TERM 后仍存活，最终 SIGKILL（退出 -9）；
  未以强杀视为正常退出验收。verification.json 确认三类 OGPlay 进程全关闭。
  尚未验收可见画面、视频生命周期、全游戏兼容或 Windows/Linux。
