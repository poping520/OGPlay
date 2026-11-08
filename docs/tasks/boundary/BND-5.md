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

## 非 UTF-8 日志修复（2026-11-08）

Sacred Odyssey 800×480 点击 NEW GAME 后，`SaveQuest.bin` 打开失败日志的方括号内
含原生非 UTF-8 字节，JSON 序列化抛出 `invalid utf-8 encoding in string`，中止 nativeRender。
liblog tag/message 在投递宿主日志前逐项校验，非法内容以 `<hex:…>` 无损表示，
返回长度仍按原字节计算。有效 UTF-8、过滤和 guest 内存访问边界沿用原行为。

- 回归覆盖非法 tag/message、残缺多字节、Modified UTF-8 NUL 和有效中文/emoji，
  同时检查原字节返回数及 JSON 可解析。修改前重现同一 JSON 异常，修改后连同 liblog、
  strict JSON 定向共 11 项/142 断言通过。
- Release `ogplay_tests`、`ogplay_bundle` 构建与包签名验证通过。同一原 APK/数据、
  独立新沙盒实测点击 NEW GAME，保留触发日志原字节，越过 Loading 到 Mission，
  已呈现开场对白/Skip，至第 1744 呈现帧无 guest fault，MCP 关闭退出 0。
- 3D 区域仍黑，保留为后续问题；本次仅证明原首错消失，未做 title gate 或跨平台验证。
  证据在 `.local/sacred-odyssey-debug/start-game/`，原始运行分别为 `before-4carynhs/`、
  `after-l38uc9fd/`，后者含 `proof.json`、原日志和 `opening-dialogue.png`。
