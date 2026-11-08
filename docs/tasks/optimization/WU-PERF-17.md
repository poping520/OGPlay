# WU-PERF-17 · SurfaceView EGL 呈现性能

目标是降低 Angry Birds 2.3.0 在 1280×720 的逐帧耗时。实现走通用的 Java EGL、
SurfaceHolder、FrameService 和 Android 呈现路径。依赖 [WU-PERF-12](WU-PERF-12.md) 的
帧序号及读回退役语义，保留 holder swap 同步交付本次像素。

## 背景

macOS arm64 Release 带符号采样显示，SurfaceView native window 每次 swap 持 VM 锁同步
读回 RGBA8，并为黑底分配整帧。GL 线程约 18% 的采样在 ANGLE Metal 等待 GPU，
主线程等 VM 锁在 720p 占 29%，在 480p 占 7.5%。多个 owner 屏障每帧约四次深拷贝
producer 像素，占主线程 31%。逐像素合成占 8.4%，SDL 上传占 8%。
原始 4000 帧样本的 720p 中位数为 8.27 ms（step 1.70、present 6.52），
480p 为 2.92 ms（step 1.88、present 0.79）。

## 改动

- SurfaceView、Canvas 和视频帧以共享不可变对象发布。快照保活同一像素存储。
- Java EGL swap 在 VM 锁外同步执行，图形锁释放后恢复 VM 锁。独立锁串行化像素投递
  与窗口退役，发布和 queue 通知复核读回前的窗口代次。1× holder 读回复用已无读者的 RGBA8 存储。
- 黑底在 FrameService 中以 alpha 标记提交，只在取帧时用回收的逻辑尺寸缓冲物化。
  黑底保留原帧序号、alpha 和软件帧退役规则。
- 不透明 1:1 合成按可见区域逐行复制并强制 alpha 为 255。缩放沿用已有的列映射和
  重复行复用，透明层沿用原混合及叠放顺序。

## 实测

macOS arm64 Release，原 APK、`--ephemeral-sandbox`、1×、无帧率上限，
各轮 4000 次实际 present，退出码均为 0。下表取 `frontend.frame_timing` 的中位数，单位 ms。

| 阶段 | 分辨率 | median | step | present |
| --- | --- | ---: | ---: | ---: |
| 同机改前 | 1280×720 | 5.287 | 3.600 | 1.585 |
| 同机改前 | 800×480 | 1.835 | 1.115 | 0.672 |
| 共享帧及 swap 修改后 | 1280×720 | 1.866 | 0.172 | 1.653 |
| 共享帧及 swap 修改后 | 800×480 | 0.992 | 0.171 | 0.762 |
| 加入 1:1 合成后 | 1280×720 | 1.300 | 0.188 | 1.076 |
| 加入 1:1 合成后 | 800×480 | 0.783 | 0.193 | 0.530 |

720p 总中位耗时下降 75.4%，present 下降 32.1%。720p/480p 总耗时比从 2.88× 降至
1.66×，低于 2.40× 像素数比。该指标是成功呈现帧的活跃阶段耗时。
证据位于 `.local/wu-perf-17/`。

Release 定向测试覆盖快照租约、裁剪与 alpha、EGL window swap 与独立 buffer 尺寸、
黑底回收及 FrameService 异步尾帧和 Context 切换。33 项中 32 项通过。
DVM-197 renderer EGL policies 的 12 条失败断言（原始基帧颜色和后台 Surface 状态）
在改前现有 Release 测试二进制同样复现。定向二进制只链接相关测试对象和夹具，
生产目标按 `cmake --build --preset release --target ogplay-cli` 重建。

## 剩余缺口

holder 路径的 GPU 等待仍在 GL 线程，软件合成后仍走 SDL 纹理上传。
本次原 APK 验证覆盖 macOS 4000 帧运行，正式 title gate、画面与跨平台验收待做。
