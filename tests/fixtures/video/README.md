# 视频制品

`short-mp4v-aac.mp4` 是既有 64×32、8 fps、约一秒 MPEG-4/AAC 回归制品。

`4k60-h264-aac.mp4` 是合成灰色画面和静音，3840×2160、60 fps、一秒 H.264/AAC；
用于让原生像素字节背压先于帧数背压发生，验证空读不伪装 EOF 及 seek 清理。
没有外部素材，FFmpeg 7 生成命令：

```sh
ffmpeg -f lavfi -i color=c=gray:s=3840x2160:r=60:d=1 \
  -f lavfi -i anullsrc=r=48000:cl=mono -t 1 \
  -c:v libx264 -preset ultrafast -threads 2 -pix_fmt yuv420p -g 30 \
  -c:a aac -b:a 32k -movflags +faststart 4k60-h264-aac.mp4
```

以下三个 64×32、8 fps、一秒合成制品用于 ogplay_ffmpeg 裁剪边界（ADR-0136），由 FFmpeg 8.1
（需 libx264/libx265）生成：`hevc-aac.mp4` 视频编码未编入；`h264-ac3.mp4` 音频编码未编入，
不得静默丢弃音轨；`h264-silent.mp4` 无音轨，打开即 `PcmEnded`。

```sh
F() { ffmpeg -hide_banner -loglevel error -y "$@"; }
F -f lavfi -i testsrc2=s=64x32:r=8:d=1 -f lavfi -i sine=f=440:r=8000:d=1 \
  -c:v libx265 -x265-params log-level=error -tag:v hvc1 -pix_fmt yuv420p \
  -c:a aac -b:a 16k -ac 1 hevc-aac.mp4
F -f lavfi -i testsrc2=s=64x32:r=8:d=1 -f lavfi -i sine=f=440:r=48000:d=1 \
  -c:v libx264 -pix_fmt yuv420p -c:a ac3 -b:a 64k -ac 1 h264-ac3.mp4
F -f lavfi -i testsrc2=s=64x32:r=8:d=1 -c:v libx264 -pix_fmt yuv420p -an h264-silent.mp4
```
