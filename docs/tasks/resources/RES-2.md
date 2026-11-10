# RES-2 ROM 资源 native 与共享资产接入

目标：ROM Java 的资源、主题、XML 和资产流改由独立资源核心承载，NDK 共享同一 AssetManager 与配置，系统资源从裁剪且对齐的 framework-res 读取。

依赖：[RES-1](RES-1.md) 的不可变资源核心、payload 和只读挂载，以及 P1 Skia BitmapFactory 和既有 guest VFS 描述符区间协议。

边界：适配层负责令牌、封送、异常、同步与 FD，资源算法留在核心。runtime/ui 的封存皮肤数据与 Loader ARSC 仅保留为第三轮投影迁移入口；不扩展 Android 系统服务。

实现：确定性裁剪工具；按配置缓存 bag 和不可变 ZIP 名称索引；Java 与 NDK 共享核心流和只读 FD；STORED 位图直接借用编码字节；恢复 ROM 资源类，移除旧游标与系统资源 JSON。UI 通过实际 Java Bitmap 识别 ROM 缓存产生的 BitmapDrawable 副本。

验收记录：本地 `.local/analysis/p2-resources/round2/` 存放 native 清单、surface 差异、定向测试、20 款游戏、基准和打包证据。最终结果及缺口见其中 `report.md`。未提交；本轮不修改 CURRENT。
