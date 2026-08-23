# BND-10 · OpenSL ES PCM mixer

## 目标

实现独立于 boundary object ABI 与 HAL 的线程安全 OpenSL ES PCM buffer-queue mixer。

## 交付与验收

- [x] player id、bounded queue、stopped/paused/playing 与 stale player 明确失败。
- [x] mono/stereo、unsigned PCM8、signed little-endian PCM16 与线性重采样。
- [x] millibel volume、mute、stereo position 和多 player 64-bit accumulation/saturation。
- [x] additive render 保留已有 SoundPool/video 样本，不打开或提交 HAL device。
- [x] queue full 不部分提交；消费后递增 play index 并返回 callback event metadata。
- [x] control/render 并发测试与 architecture documentation gates 通过。

## 验证

`ctest --preset dev --output-on-failure -R "OpenSL mixer|architecture.documentation_layout|architecture.capabilities"`

5/5 通过；最终全量 CTest 延后到 OpenSL WU 全部完成。
