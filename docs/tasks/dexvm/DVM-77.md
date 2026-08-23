# DVM-77 · PackageManager P0 当前包查询面

## 目标（一句话）

在一个 WU 内为 DexVM 发布 API 19 PackageManager 的 P0 当前包查询能力及最小相关
对象模型，使游戏可读取自身 ApplicationInfo、PackageInfo、label、权限与显式平台 feature，
同时保持未知包、未知 flags 和范围外系统查询明确失败或返回 API 契约规定的否定结果。

## 依赖

- DVM-75：稳定的进程级 `PackageManager` guest identity。
- DVM-76：关闭 survey 的 reached-fault 调用链；当前首 fault 为
  `getApplicationInfo(currentPackage, GET_META_DATA)`。
- API 19 AOSP：`android.content.pm.PackageManager`、`PackageItemInfo`、
  `ApplicationInfo`、`PackageInfo` 与 `PackageManager.NameNotFoundException`。

## 范围

### P0 方法

1. `getApplicationInfo(String,int)`：只查询当前包；支持 `0` 与 `GET_META_DATA`。
2. `getPackageInfo(String,int)`：只查询当前包；支持 `0`、`GET_META_DATA` 与
   `GET_PERMISSIONS` 的组合。
3. `getApplicationLabel(ApplicationInfo)`：literal/resource label，缺失时回退包名。
4. `checkPermission(String,String)`：当前包按显式已授予权限事实回答，其他包返回
   `PERMISSION_DENIED`。
5. `hasSystemFeature(String)`：只按会话显式 feature 集合回答。

### 相关类

- `PackageItemInfo`：`name/packageName/labelRes/nonLocalizedLabel/icon/logo/metaData`。
- `ApplicationInfo`：P0 常读 identity、class/process、资源/目录、uid、target SDK、flags、
  enabled 字段。
- `PackageInfo`：包名、版本、ApplicationInfo、requestedPermissions。
- `PackageManager.NameNotFoundException`：未知包查询的 Java 异常类型。

## 语义边界

- 不引入 Binder、IPackageManager、安装包数据库、UID 数据库或其他应用可见性。
- Manifest 是当前 APK package/version/label/meta-data/requested-permission 的唯一事实源；
  `ApplicationInfo.metaData` 仅在 `GET_META_DATA` 下物化。
- OGPlay 的权限集合是兼容进程显式授予的 Manifest requested permissions；它不宣称模拟
  Android protection level、运行时授权或系统签名。
- feature 由会话按实际宿主能力注入；未知 feature 返回 false，不按游戏名/profile 猜测。
- 未支持 flags 抛 `UnsupportedOperationException`，未知包的 info 查询抛
  `NameNotFoundException`；不伪造成功或跨包结果。

## 验收（机器可判定）

- 两解释器后端均验证当前包、flags、字段、metadata Bundle、version 与 label。
- 未知包抛精确 `NameNotFoundException`；未支持 flags 明确失败。
- 权限与 feature 的 true/false 对照均覆盖，关闭事实即失败。
- Manifest parser 覆盖 requested permission 与 application meta-data 的字符串、整数/
  boolean、resource 值及畸形输入。
- Windows Debug 构建、focused tests、architecture gate 通过；Release PVZ 关闭 survey
  越过当前 `getApplicationInfo` fault 并固定下一 reached fault。

## 结果（机器可判定，已达成）

- PackageManager/Manifest focused 13/13（1227 assertions），覆盖 switch/threaded 配置、
  metadata flags/Bundle、version/label、permission/feature true/false、未知包与未知 flags。
- Windows Debug 全目标与 Release `ogplay` 构建通过，architecture 5/5。
- PVZ 原命令关闭 survey 已越过
  `PackageManager.getApplicationInfo(currentPackage, GET_META_DATA)`；Manifest 的 Nimble
  configuration 成功读取，新首 fault 为 `Ljava/util/LinkedHashMap;`。
- full CTest 首轮 948/953；其中 3 个 frontend smoke 由中断增量构建产物不一致导致，
  clean rebuild 后 3/3 通过。剩余两个仍是本 WU 未触及的 String catalog 43/44 与
  liblog tag `PVZ`/空断言漂移。

状态：完成。
