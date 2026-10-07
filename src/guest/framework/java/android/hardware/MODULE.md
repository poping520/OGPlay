# 模块：有界传感器客户端

Sensor/SensorManager 保留固定 API19 AOSP 源码，编译进 BootDex；来源与哈希由 builder
inventory/manifest 封存。类型接口、SensorEvent/TriggerEvent 值类来自固定 framework.jar。
普通字段、缓存、旧接口位掩码、监听器映射与数学/坐标转换只由 Java 执行。

LegacySensorManager 只替换构造器 WMS 查询为私有 nativeGetDisplayRotation；其余 AOSP
算法不改。当前 managed viewport 不旋转，此边界返回 ROTATION_0，不建立 WMS watcher。
LocalSensorManager 是 Context 服务工厂选择的具体后端；native 只提供空设备目录与
无设备注册/注销事实，不生成 Sensor 对象或回调，不持有宿主硬件及重复监听器表。

支持无传感器路径，不包含 SensorService、Binder、动态设备、宿主传感器、真实事件
时间戳/队列或旋转监听。未来设备接入必须另行明确这些边界，保持同一 Java 客户端。
