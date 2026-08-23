# BND-5 · Android 4.4.4 liblog Virtual SO

## 目标

按 AOSP Android 4.4.4 `system/core/liblog` 的目标构建导出面，实现完整
`liblog.so` concrete Virtual SO，并把 guest 文本/二进制日志接入 OGPlay structured
logger。

## AOSP 基线

- `system/core/liblog/Android.mk`：target shared library 由 `logd_write.c`、
  `logprint.c`、`event_tag_map.c` 组成。
- public/internal headers：`include/android/log.h`、`include/log/log.h`、
  `include/log/logprint.h`、`include/log/event_tag_map.h`。
- KitKat 写端原本面向 `/dev/log/*`；OGPlay Virtual SO 保留函数与返回/解析语义，
  输出改投显式注入的 `core::Logger`，不伪造 Android kernel logger device。

## 交付与验收

- [x] catalog 发布上述三组源文件的全部 23 个 global API。
- [x] `LogModule final` 独立于 session facade，通过 `LogBoundaryContext` 构造注入 guest
  memory 与 logger。
- [x] writer 支持 text/buffer/`va_list`/variadic/assert/binary event，所有项目日志使用
  `guest` category 和 `[guest]` message 前缀。
- [x] logprint 支持 format/filter、wire buffer decode、line format/print；event-tag map
  支持打开、查询与关闭。
- [x] 普通与 fast host-call 复用同一 export-specific `{fn,self}` binding，memory fault
  保留原异常语义。
- [x] focused tests 覆盖完整符号面、priority 映射、A32 variadic/vprint、filter/buffer 与
  event map；随后全量 CTest 通过。
