# 模块：audio

## 职责

实现 guest OpenSL ES、AudioTrack、MediaPlayer/SoundPool 语义并对接 HAL 音频/视频时钟。

## 公共 API

M3/M6 定义对象表、PCM 队列、回调与媒体状态机。

## 不变量

- guest buffer 所有权和回调线程明确。
- 音频时钟接入统一 Clock；对象状态可快照。

## 禁止

- 不直接调用 CoreAudio 等平台 API。
- 不为某个资源编号硬编码封面音乐。

## 测试

`tests/audio/` 的 ABI、状态机和无设备离线混音测试。

