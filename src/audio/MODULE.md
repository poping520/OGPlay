# 模块：audio

## 职责

实现 guest OpenSL ES、AudioTrack、MediaPlayer/SoundPool 语义并对接 HAL 音频/视频时钟。

## 公共 API

- `MusicPlayer::Play`：接收已解析的编码音乐字节和循环事实；具体解码与输出由后续通用
  实现提供。
- `JavaSoundPoolState`：保存 Java SoundPool 的可用/已销毁生命周期；initialize/destroy
  只在真实状态迁移时递增计数，重复调用幂等；voice 按普通 pool / big 分类并以
  resource + instance 标识，stop-all 可按类别清理且可保留一个 resource。
- M3/M6 定义对象表、PCM 队列、回调与媒体状态机。

## 不变量

- guest buffer 所有权和回调线程明确。
- 音频时钟接入统一 Clock；对象状态可快照。
- 编码资源的来源与路径解析发生在上层；播放器只接收一次调用期间有效的只读字节视图。
- SoundPool 生命周期状态不代表声音已加载或播放；未接入资源、解码和输出前不得发布
  loaded/playing 成功。
- SoundPool 状态必须可由不同 guest JNI 线程安全访问；destroy 同时清空所有 voice，
  initialize 不得恢复已销毁的 voice。

## 禁止

- 不直接调用 CoreAudio 等平台 API。
- 不为某个资源编号硬编码封面音乐。

## 测试

`tests/audio/` 的 ABI、状态机和无设备离线混音测试。
