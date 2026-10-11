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
