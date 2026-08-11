# WU-PERF-06 · 窗口呈现改为 renderer 流式纹理上传

目标：把 `hal::WindowInput::PresentRgba8` 从 CPU surface blit（填充黑边 + 软件
swizzle/缩放 + `SDL_UpdateWindowSurface`）改为 SDL_Renderer 流式纹理：上传一次
RGBA8，黑边清屏与等比缩放全部由 renderer 完成。

背景（稳定期 3 秒采样）：软件 blit 路径占主线程约 25%，其中
`SDL_UpdateWindowTexture` 每帧再做一次全帧 CPU 拷贝。

验收：

- [x] `PresentRgba8` 经 `SDL_UpdateTexture` + `SDL_RenderClear` +
  `SDL_RenderTexture(NEAREST, BLENDMODE_NONE)` + `SDL_RenderPresent`；布局仍复用
  `FitDisplayRect`（HiDPI 用 render output 尺寸），present 计数语义不变。
- [x] 纹理按 guest 帧尺寸缓存，尺寸变化时重建；窗口关闭时显式销毁 renderer 与纹理。
- [x] 像素契约测试改为 `SDL_RenderReadPixels` 读回，黑边/内容/夹角断言不变；
  dummy/offscreen 后端下 full CTest 527/527。
- [x] 稳定期呈现占比 25% → 10%（剩余为纹理上传 memcpy）；总时长持平
  （15.8s→15.9s，瓶颈转移至 `glReadPixels` 同步回读，见 ADR-0019）。
