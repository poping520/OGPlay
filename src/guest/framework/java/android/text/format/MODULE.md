# 模块：Android Time 的 guest 实现

原版 Time 由 BootDex 配方收录。TimeNative 实现其 API 19 native 入口，使用
GregorianCalendar 归一化公历字段、TimeZone/ZoneInfo 查偏移和 DST。
setToNow 委托 System.currentTimeMillis。format 使用原版 Time 从 LocaleData 和
Resources 取得的格式及本地化文本；无法识别的转换符明确抛异常。
