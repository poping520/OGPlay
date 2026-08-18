# 08 · 验证体系

本任务的风险集中在启动顺序与重入，单靠现有 title 人工试玩不够。每个 WU 都必须有
机器可判定出口。

## 1. 测试层级

| 层级 | 目标 |
| --- | --- |
| loader/unit | Manifest Application/launcher/alias、类名归一化、native inventory、ABI 选择 |
| runtime/unit | process shell 无 app root、dynamic namespace append、loaded registry、JNI_OnLoad 调用计数 |
| DexVM integration | `System.load*` handler 真正进入 loader、Java 异常映射、Java↔native 重入 |
| lifecycle integration | Application → Activity 顺序与失败短路 |
| APK fixture | 无 Profile 的最小 APK 从 Manifest 启动并动态装库 |
| title regression | 现有 exact Scenario 三轮、clean shutdown、无新 guest fault |

## 2. 最小 APK fixture 矩阵

建议建立一个可由仓库工具稳定生成的 APK fixture，而不是提交不透明二进制。
需要覆盖：

| Case | Java 触发点 | 预期 |
| --- | --- | --- |
| A | Application `<clinit>` loadLibrary A | A 在 Activity 前 Loaded，JNI_OnLoad 一次 |
| B | Application `onCreate` loadLibrary A | 同上 |
| C | Activity `<clinit>` loadLibrary A | Application 已完成，A 在 Activity instance 前加载 |
| D | Activity `onCreate` loadLibrary A | onCreate 中加载成功 |
| E | `System.load(syntheticGuestPath)` | path load 成功 |
| F | 同库 loadLibrary 两次 | constructors/JNI_OnLoad 不重复 |
| G | A 的 JNI_OnLoad 回调 Java，Java load B | 嵌套成功、无 deadlock |
| H | 缺失 library | Java link error + structured loader error |
| I | wrong ABI/malformed library | 明确失败，不回退 profile |
| J | custom Application throws | Activity 不启动 |

## 3. Manifest 单元测试

至少包含：

- no `android:name` → default Application；
- `.App` / `App` / fully-qualified name 三类归一化；
- direct Activity launcher；
- activity-alias launcher；
- no launcher；
- multiple launcher 的确定性选择；
- disabled component；
- profile launcher override 指向不存在类的负例。

## 4. ABI/native inventory 测试

覆盖：

- only armeabi；
- only armeabi-v7a；
- 两者共存时固定优先级；
- APK ABI 与 runtime supported ABI 无交集；
- 同 soname 在多个 ABI 中存在但只解析 selected ABI；
- `System.loadLibrary("x")` 不因其它 ABI 的 `libx.so` 而误成功。

## 5. native loader 测试

必须能直接断言：

- map 次数；
- constructor 次数；
- JNI_OnLoad 次数与返回 version；
- explicit requested library 与 dependency 集合；
- loaded registry 最终状态；
- dependency symbol resolution；
- reverse finalization 顺序（若现有 runtime 有此契约）。

**关键负例：** dependency `libB.so` 自带 `JNI_OnLoad`，仅 explicit load A、A 的
`DT_NEEDED` 引入 B 时，B 是否调用 JNI_OnLoad 必须与本地 Dalvik 参考行为一致，不能
由实现者凭经验决定。

## 6. reentrancy gate

至少一个集成 fixture 的调用栈必须真实形成：

```text
DexVM → System.loadLibrary(A)
  → guest JNI_OnLoad(A)
    → JNI → DexVM callback
      → System.loadLibrary(B)
        → guest JNI_OnLoad(B)
```

验收同时检查：

- 不超时/不死锁；
- A/B 都只初始化一次；
- Java callback 返回值正确穿透；
- pending exception 为零；
- shutdown 无悬挂线程/模块。

## 7. Profile 降级 gate

- 无任何 Profile：通用 fixture 可启动；
- 有旧 v2 exact profile：能应用 override 但启动不依赖 root-so 预装；
- 修改 APK 使 old hash 不匹配：旧 profile 不应用，但 generic path 仍继续；
- v3 无 `so_sha256`：profile 可合法解析并应用非 native-root quirk。

## 8. title regression

至少选择已有 dexvm pilot title 做迁移 gate：

1. 迁移前记录 exact Scenario baseline；
2. 新启动路径跑三轮；
3. golden/frame/tick/guest fault/clean shutdown 等项目既有门禁保持；
4. 若 title 原 profile 有入口覆盖/静态 preset，证明它们仍作为 quirk 生效；
5. 记录 native libraries 的实际 explicit load 顺序，确认不再由 frontend preload。

## 9. 完成定义（DoD）

只有同时满足以下条件，APS-9 才能关闭：

- APS-1..8 的机器 gate 全绿；
- `run-apk` generic path 不要求 Title Profile；
- frontend 无 app root `.so` 选择和 `InitializeJniLibrary()` 调用；
- `System.load` + `System.loadLibrary` 均为真实加载；
- Application startup 在 launcher Activity 前；
- reentrancy fixture 通过；
- full CTest 无回归；
- old design 冲突处加 superseded 指向；
- `MODULE.md` / capability ledger / CURRENT 按项目流程同步。
