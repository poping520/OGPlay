# 模块：audio

## 职责

实现 guest OpenSL ES、AudioTrack、MediaPlayer/SoundPool 语义并对接 HAL 音频/视频时钟。

## 公共 API

- `MusicPlayer::Play`：接收已解析的编码音乐字节和循环事实；具体解码与输出由后续通用
  实现提供。
- `JavaSoundPoolState`：保存 Java SoundPool 的可用/已销毁生命周期；initialize/destroy
  只在真实状态迁移时递增计数，重复调用幂等；voice 按普通 pool / big 分类并以
  resource + instance 标识，stop-all 可按类别清理且可保留一个 resource；已加载资源按
  类别 + resource 独立建账，pending load request 与 loaded 状态分离，供加载、查询、播放
  与卸载共享。
- `DecodeOggVorbis`：从有界内存输入解码 Ogg Vorbis，事务发布拥有型 mono/stereo PCM16、
  采样率与帧数；使用仓库固定的 stb_vorbis 1.22（MIT 或 public-domain 双许可）。
- `JavaSoundPoolMixer`：用注入的编码资源 loader 按 resource 去重解码，以
  kind + resource + instance 管理 voice，并输出确定性 stereo PCM16；loader 不存在时
  保持显式 disabled，缺失/损坏资源保留可查询失败原因。
- `OpenSlesPcmMixer`：为 Virtual `libOpenSLES.so` 保存线程安全 PCM player/queue，支持
  mono/stereo、unsigned PCM8/signed little-endian PCM16、线性重采样、millibel volume、
  mute、pan 与多 player 64-bit 饱和加性混音；完整消费只返回事件，mixer 不直接调用 guest
  callback 或 HAL。
- M3/M6 定义对象表、PCM 队列、回调与媒体状态机。

## 不变量

- guest buffer 所有权和回调线程明确。
- 音频时钟接入统一 Clock；对象状态可快照。
- 编码资源的来源与路径解析发生在上层；播放器只接收一次调用期间有效的只读字节视图。
- Ogg 输入最大 64 MiB、解码 PCM 最大 128 MiB，只接受 1/2 声道和正采样率；空、损坏、
  超限或不支持流必须在发布 PCM 前明确失败。
- SoundPool resource 只有在上游完成真实加载后才能 `MarkLoaded`；未接入加载、解码和
  输出前，load/play 只能留下可查询 pending request，查询必须返回目录事实，不得发布
  loaded/playing 成功；play 只有在 loaded 后才能以 resource + instance 创建 voice。
- SoundPool 音量必须是有限的 `[0, 1]` 值；同一 voice 的重复 play 更新状态而不复制身份。
- SoundPool voice control 以类别 + resource + instance 精确寻址；pause/resume 只迁移已有
  voice，stop 删除 voice，pitch 只接受 `[0.5, 2]`，reset 记录可查询的重置事实。
- 批量 pause/resume 只迁移指定类别的现有 voice，已处于目标状态的 voice 保持幂等；
  state 与 mixer 必须在同一受检 handler 下同步提交。
- looping 是每次 play 的显式 voice 状态；mixer 到 PCM 尾部只回绕 looping voice，普通
  voice 仍自然结束，pause/resume/reset 不得丢失 loop 事实。
- SoundPool 状态必须可由不同 guest JNI 线程安全访问；destroy 同时清空所有 voice，
  与 loaded resource；initialize 不得恢复已销毁的状态。stop 只影响 voice，不得隐式卸载。
- mixer 控制和 render 共用内部锁；mono 复制到双声道，mono/stereo 通过有界线性重采样
  消费 position，多个 voice 以 64-bit scratch 累加并饱和为 PCM16，完成 voice 自动清理。

## 禁止

- 不直接调用 CoreAudio 等平台 API。
- 不为某个资源编号硬编码封面音乐。

## 测试

`tests/audio/` 的 ABI、状态机和无设备离线混音测试。
