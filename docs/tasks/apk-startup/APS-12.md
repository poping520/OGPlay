# APS-12 · SurfaceTexture 端点契约与 ANGLE 后端验证

## 目标

为进程内 Surface 图像生产/消费链确定所有权与后端边界，用真实外部纹理像素验证取代能力猜测。
本 WU 为长期修复第一阶段，不将类可构造当作完整 SurfaceTexture 支持。

## 依赖与设计锚点

- APS-11 后续 ApplicationErrorReport 配方补齐；BND-34 的 EGLImage/ANGLE 对象接口。
- [ADR-0140](../../adr/media.md#adr-0140)：端点、队列、GL/线程和分阶段支持边界。
- 后续依赖：公共端点/队列与原版 Java/native 闭环；MediaPlayer 纹理生产端与启动后端设置。

## 语义出处

- AOSP `framework/base/graphics/java/android/graphics/SurfaceTexture.java`：init 的 Handler/WeakReference、
  native 边界、回调、release/finalize；构造可早于 current GL。
- `framework/base/core/jni/android/graphics/SurfaceTexture.cpp`：init 建 BufferQueue/GLConsumer，
  classInit 绑定 metadata，release abandon，finalize 释放引用。
- `framework/native/libs/gui/GLConsumer.cpp`：构造无 GL 操作；updateTexImage 检查 EGL，
  无新帧仍执行 external target bind；attach/detach 与锁存资源生命周期。
- `framework/native/libs/gui/ConsumerBase.cpp::abandon`：重复释放幂等。

## 变更

- 在现有 ANGLE 主题测试中验证固定 SDK 的硬件后端 capability/错误一致性，以及已有
  Vulkan/SwiftShader 的 RGBA texture→EGLImage→EXTERNAL_OES→samplerExternalOES→readback。
- 使用非均匀图像与两次替换，删除 producer texture/EGLImage handle 后仍验证 sibling 像素存储，
  避免常量 shader、清屏或对象句柄存在被误认为真实通路。
- 不修改 SDK/submodule、渲染器选择、guest 扩展发布或游戏路径，不导入尚无后端契约的空壳类。

## 验收

- 仅构建 `ogplay_tests`，仅运行 `SurfaceTexture*` 后端测试。
- 硬件缺 external 时必须同时验证 shader 编译拒绝和目标绑定 GL_INVALID_ENUM；
  具备 external 的后端必须真实采样。SDK 有 SwiftShader 时必须执行像素路径，不可跳过代替通过。
- 记录具体 renderer/SDK 与实际像素断言；未运行平台不标为已验收。
- capabilities、文档布局、UTF-8 与 diff 静态检查。

## 后续验收边界

- 下一阶段须使用同一公共模型实现 Surface producer 与 SurfaceTexture consumer，
  验证真实合成生产者完整链路、GC/退出、缓冲模式、矩阵/PTS、线程与事件，不仅构造。
- MediaPlayer 必须复用既有 decoder/Clock/PCM，保留 holder 输出；覆盖 seek/loop、解绑重绑。
- 渲染器只能在进程/图形资源创建前确定；现有 library `angle_backend` 设置仍为预留，
  后续接线前不能声称用户启动已可选择此软件通路。不得中途迁移已有 texture/context。

## 结果（第一阶段已达成）

- 固定 SDK ANGLE `c24d9971269a878a221238a5923abdcc933fa2e9`，macOS arm64、Apple M1 Pro。
  Metal 没有 external 扩展，实际 shader 编译失败且绑定 GL_INVALID_ENUM；
  Vulkan/SwiftShader（driver 5.0.0）使用原 external sampler 采样两张非均匀 RGBA 图像，
  替换及删除 source texture/EGLImage handle 后的 sibling 存储像素全部通过。
- Release `ogplay_tests` 目标构建退出 0；仅运行 `--test-case=SurfaceTexture* --no-colors`，
  **2 cases / 46 assertions** 全通过，软件分支实际执行。日志
  `.local/aps13-backend-build.log`、`aps13-backend-tests.log`；机器证据 `aps13-backend-proof.json`。
- ADR-0140 已明确跨模块依赖、端点/租约、缓冲模式、事件/锁和退出规则；能力账本中
  SurfaceTexture 保持 unimplemented，现有 EGLImage 能力仅增加真实后端验证入口。
- 本 WU 没有修改生产代码、BootDex、guest 扩展或默认 renderer，未重新运行原 APK；
  TML 最近实测首错仍是 SurfaceTexture 缺类。Windows/Linux 与完整媒体链未验收。
- capabilities check、文档布局、ADR 索引/编号、UTF-8 与 `git diff --check` 通过。
