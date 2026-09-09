# BND-29 · GLES2 capability 参数错误回送

## 目标与依赖

依赖 BND-28，使 OGPlay 自身的 capability 枚举校验与 ANGLE 原生 GL 错误走同一锁存。

## 交付

- enable/disable/isEnabled 共用前置校验；仅该校验的 invalid_argument 转为
  GlesApiError(GL_INVALID_ENUM)，非法查询返回 false，非法修改不触碰 backend/状态。
- 不增加 GL_TEXTURE_2D 到 GLES2 白名单；GLES1 固定功能状态保持原语义。
- RequireFrame 在转换范围外，生命周期与内存等契约错误仍终止，不全局吞异常。

## 验证

- 真实 ANGLE 定向 2 用例/99 断言通过，覆盖三入口、Texture2D/未知枚举、
  错误读取清除、重复错误锁存、合法状态保持、GLES1 隔离、无 frame 失败。
- 原 PvZ 命令有界探索观察不再出现原第 2 帧 capability 错误，进程继续运行；
  不据此宣称游戏可玩或通过场景验收，观察后关闭本轮窗口。
- 首次测试目标已链接，但并行实跑占用 ANGLE DLL 使 post-build 复制失败，
  关闭实跑后 ogplay/ogplay_tests 构建完整成功。
- 关闭窗口后出现 JNI monitor thread is not a DexVM thread，属于后续退出路径问题，
  本次不扩展修复。capabilities_monotonic/boundary_hot_path/documentation_layout 通过。
