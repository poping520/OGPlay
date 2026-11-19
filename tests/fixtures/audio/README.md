# MP3 测试制品

- `short-mp3.mp3` 原样取自 [minimp3 的 `vectors/l3-he_free.bit`](https://github.com/lieff/minimp3/blob/ea99364f61c14656440e8d77e9c233ccf3124633/vectors/l3-he_free.bit)，提交 `ea99364f61c14656440e8d77e9c233ccf3124633`，许可 CC0-1.0。SHA-256 `b55afc2a21492c4b3035d423f36fe87560ef34f65eb997da60af9b01f300362c`。用于验证 free-format MP3 被明确拒绝。
- `short-cbr-mp3.mp3` 由 440 Hz 合成正弦波生成，16 kHz 单声道、40 kb/s CBR，含 LAME Info 标签。裁剪 encoder delay 和 padding 后为 4,800 帧（0.3 秒）。SHA-256 `4fbe427ae3c03e2012900df055f634277c6752919a62bda25fb47436ac6e5265`。

在本目录运行以下命令即可重新生成合成 fixture（FFmpeg 8.1.2、LAME 4.0）。

```sh
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=16000:duration=0.3' -c:a pcm_s16le /tmp/ogplay-short-cbr-mp3.wav
lame --silent --cbr -b 40 -m m /tmp/ogplay-short-cbr-mp3.wav short-cbr-mp3.mp3
```
