# GUI-14 · 启动器视觉与空闲呈现质量

## 目标

改善启动器字体/图标清晰度、后台空闲功耗和非零退出日志的 UTF-8 完整性。

## 依赖

- GUI-4：CJK 字体、图标磁贴和主循环。
- GUI-6：有界 `last-run.log` 尾部呈现。

## 结果

- 可用宿主 CJK 字体直接作为 18px 主字体，保留子像素定位与 2× 水平 oversampling；不再
  把 16px 像素字体作为拉伸后的 Latin 主字体。无 CJK 字体时使用 ImGui scalable vector
  ASCII fallback。
- APK 图标从最近邻改为像素中心双线性 ARGB 缩放，128×128 缓存契约不变。
- 非 smoke 主循环以 `SDL_WaitEventTimeout` 最多等待 100ms；事件立即唤醒，子进程和后台
  分析仍有有界轮询，smoke 不等待。
- 日志按字节截尾时跳过开头残缺的 UTF-8 continuation bytes，弹窗不再以半个汉字开头。

## 验收

纯函数测试锁定双线性中心像素、idle/smoke 等待值和 UTF-8 截断边界；两个真实 GUI 冒烟
验证主字体可构建并呈现；Windows/MSVC `/W4 /WX` 与全量 CTest 通过。
