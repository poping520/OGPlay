# LUI-14 · include / scale / resources

## 目标（一句话）

补足常见 XML 复用、简单资源值和图片缩放，使通用 UI fixture 不依赖 inline 常量。

## 依赖

- LUI-12。
- LUI-13。

## 验收与结果

- `<include>` 在 inflation 前展开 layout resource；concrete root 支持 id/visibility/layout
  params override，merge root children 直接接现有 parent，include 与等价 inline tree geometry
  一致。
- include child、非法 override、missing layout、depth >16 与直接/间接 cycle 明确失败；入口
  layout id 加入 cycle stack，失败不发布部分 UiTree。
- ArscEntry 保留简单 Res_value type/data；UI resolver 最多跟随 16 层 reference，支持默认配置
  ASCII string、ARGB color、px/dp/sp dimension，以及 bitmap/color drawable。未知 session density
  使用显式 1.0 fallback。
- XML text/textColor/textSize/padding/margin/background/src 与 Java setImageResource/
  setBackgroundResource 共用 resolver 和 UiNode/image cache。
- ImageView 支持 API19 enum 值 CENTER、CENTER_INSIDE、FIT_CENTER、FIT_XY、CENTER_CROP；
  XML/Java setter 共用状态，确定性 nearest-neighbor raster 与 CENTER_CROP golden 锁定。
- complex style bag、未命中 selector、matrix/fitStart/fitEnd 和非默认 qualifier 不扩张范围并
  明确失败；当前资源/layout fixture 无需这些能力。
- configure/build、include/resource/scale tests 与 full CTest 761/761 通过。
