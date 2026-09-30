# BND-46：跨宿主输入快照与 Android API19 输入接口

状态：Windows 共用输入实现及定向验证完成；原游戏交互验收受独立 GLES2 首错阻塞。
按用户澄清，本次仅预留移动宿主接口，Android/iOS 移植与真机验收属于后续工作。
决策：[ADR-0086](../../adr/session.md#adr-0086)。

## 目标与阶段

1. HAL 采集宿主事实；input 管理坐标、触点和取消；session 生成 Android 事件快照。
   Java 与 NDK 使用同一份 source、设备、触点、时间和历史数据。
2. 补齐 API19 AInputEvent/AKeyEvent/AMotionEvent getter，64 位时间 ABI，按队列的
   不可变事件所有权、get/finish 与退役检查。未实现能力不得伪造成功。
3. SDL3 原生触摸、多指、取消、失焦/后台、坐标转换与合成鼠标去重。
4. 预留平台无关输入与生命周期通知接口；Android/iOS 实际接入及真机验证后续实施。

## 验收

- 定向检查导出全集、float/int64 ABI、当前与历史轴、索引、事件所有权及失败原子性。
- 单指/双指 down/move/up/cancel、稳定 pointerId、action index、downTime，Java/NDK 一致。
- SDL 原生触摸和测试注入走统一入口；旋转/失焦/切后台不残留按住状态。
- windows-msvc 受影响目标及定向测试；原 APK 点击弹框关闭按钮的复现。
- 本次不要求 Android/iOS 工具链、运行入口或真机验收；Windows 回归不作为移动支持证据。

## 当前证据

NDK 阶段 windows-msvc ogplay/ogplay_tests 构建通过，定向 12 用例/463 断言通过；
本地 API19 input.h 的 getter 全集与导出表核对无缺失。
Java 共用快照、session Clock 时间及生命周期取消已接入；SDL 原生触摸已接入。
用户确认先使用 Windows，移动真机环境稍后提供，真机验收延后。
证据：`.local/input-platform-{tests-build,tests}.log`、`.local/input-platform-exports.json`。
移动宿主整个平台、软件 NativeWindow 缓冲区、传感器及
手柄扩充不由输入 getter 的完成推导为已支持。完整游戏兼容与越过当前首错分开记录。

- 原 APK/OBB、临时沙盒、无 Profile、MCP 手动步进到 120 帧后点击 (699,114)。
  121 帧首次失败为 `GLES2 cannot stage client arrays from an opaque element buffer`；
  原 getSource 缺符号/PC=0 消失，但弹框关闭未验收。图形首错不扩入本输入任务。
  证据 `.local/input-platform-{click,after-state}.json`、启动日志和 before/after PNG。

- Java/session 阶段 windows-msvc 构建通过，定向 19 用例/1158 断言通过。MotionEvent
  快照由 GC 管理，双解释器读取多指身份、轴、历史、时间；KeyEvent 保留时间和 flags。
  View 深层派发不丢 action index，cancel 释放捕获；session timeline 保留 downTime，
  拒绝 Clock 倒退。既有 InputDevice、触摸消费、点击、滚动与 View 遍历回归通过。
  证据 `.local/input-java-{build,tests}.log`；未重新进行游戏交互验收。

- SDL/input 阶段 windows-msvc 构建通过，联合定向 31 用例/1324 断言通过。
  原生触摸保留 64 位宿主身份并映射稳定 pointerId，支持多指 action index、压力、
  坐标转换、cancel；双向过滤 SDL 合成事件。失焦、尺寸变化和键鼠移除取消按住状态，
  同时清空 MCP 手势；已加入前后台事件的 session 暂停/恢复编排。
  证据 `.local/input-touch-final-{build,tests}.log`。
- 最终 Windows 集成版本重跑同一 APK/OBB：120 帧点击 (699,114)，121 帧仍为上述
  独立 GLES2 首错；未再出现缺失输入 getter 或 PC=0。弹框关闭尚未验收。
  证据 `.local/input-final-{click,after-state}.json`、启动日志和 before/after PNG。
- 元数据核对补齐修饰键从 HAL 到触摸快照及 Android metaState 的传递，并在虚拟触屏
  设备目录声明已提供的压力轴范围。windows-msvc 构建及定向 26 用例/1037 断言通过，
  证据 `.local/input-metadata-{build,tests}.log`。

## 移动宿主后续边界

- 已加入 SDL event-watch 的有界通知缓冲：任意回调线程仅保存宿主事实，由 PollEvents
  所在线程交付；SDL 清队列后仍保留通知，去除重复后台阶段，丢弃跨生命周期的旧输入。
  windows-msvc 构建通过，定向 24 用例/884 断言通过，包含跨线程回调、SDL 清队列、
  终止通知、重新接收输入与容量耗尽显式失败。证据 `.local/input-lifecycle-{build,tests}.log`。
- 通知缓冲不能保证移动后台回调返回前完成暂停。SDL 文档要求同步完成后台处理，而
  现有 CLI 会在长 guest 调用内泵消息；此时不能重入 VM 的 Suspend。移动入口仍需
  拆分事件线程与 guest 拥有线程，完成停止渲染/输入及恢复握手。禁止从任意回调线程
  直接进入 VM，也不能持 SDL event-watch 锁等待可能调用 SDL 的线程；未宣称移动后台安全。
- Android/iOS 工具链、入口打包、真机多指/旋转/后台恢复分别验证；Windows 合成触摸
  测试仅验证共用模型及 SDL 翻译。safe-area 变化目前只取消手势，未完成布局避让。
- 当前环境核对：Windows 有 NDK r25c，未找到 xcodebuild/xcrun；仓库 ANGLE SDK 仅有
  Windows/Linux/macOS 制品，尚无 Android/iOS 制品与移动宿主启动入口。后续同步暂停
  握手必须在实际移动宿主事件循环中接入，不能靠现有 CLI 模拟测试宣称已完成。
