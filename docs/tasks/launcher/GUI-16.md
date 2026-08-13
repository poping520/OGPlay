# GUI-16 · APK 边缘元数据回退

## 目标

让 Android 合法的零版本号和不可显示 application label 不阻止游戏导入。

## 依赖

- GUI-2：严格库元数据 round-trip。
- GUI-3：application label 提取与 fallback 记账。

## 结果

- `LibraryMetadata.version_code` 接受完整 `uint32_t` 范围，包括 Android Manifest 可表达的
  0；其他 package、UTF-8、必填字段和路径校验不变。
- application label 含 C0/DEL 控制字符时回退 package name，并追加
  `label_control_characters`；导入控制器继续把该 fallback 写入结构化日志。
- TOML 层仍拒绝任意字段中的未转义控制字符；只有来自 APK 的显示名在进入持久模型前
  执行受检回退，不放宽持久格式。

## 验收

模型测试锁定 versionCode 0 导入/读回；合成 binary Manifest 锁定控制字符 label 回退与
fallback 枚举；Windows/MSVC `/W4 /WX` 构建和全量 CTest 通过。
