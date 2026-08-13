# DVM-36 · android.* intrinsic 按类分文件迁移与占位生成器切换

## 目标（一句话）

把 integration 层全部 android.* intrinsic（`dexvm_android_catalog_*.cpp`
声明 + `dexvm_android_{activity,device,files,graphics,io,media,misc,video,
widget_dispatch}.cpp` 约 250 个 handler）迁移为
`src/runtime/integration/dexvm_android/` 下每类一文件的声明即绑定形态，
`misc` 杂物文件拆散归类，`tools/dexvm_stub_gen.py` 改为生成新形态。

## 依赖

- DVM-34（builder 与双通道）；建议在 DVM-35 之后执行（沿用其目录约定与
  迁移纪律）。

## 方案

### 1. 目录与命名

```
src/runtime/integration/dexvm_android/
  MODULE.md
  catalog.h / catalog.cpp        # 聚合 AndroidIntrinsicCatalog(context)
  shared.h                       # Self/Singleton/MakeString/StreamOf 等既有共享 helper
  android_app_Activity.cpp       # Declare_android_app_Activity(context)
  android_content_Intent.cpp
  android_os_Bundle.cpp
  android_widget_TextView.cpp
  ...
```

- 命名规则与 DVM-35 相同；每文件一个
  `IntrinsicClassDecl Declare_<类名>(const Context& context)`——android
  handler 捕获 `std::shared_ptr<DexVmAndroidContext>`，因此逐类函数接受
  context 入参，聚合函数签名变为
  `AndroidIntrinsicCatalog(const Context&)`。
- 装配点收敛为单入参：`run_apk.cpp`（606-629 行）改为只传合并后的
  catalog，`RegisterAndroidBuiltins` 与 `platform_handlers` 回调在本 WU 后
  不再被调用（符号保留到 DVM-37 删除）。`dexvm_android.h` 公共面同步。
- `dexvm_android_misc.cpp`（Pair/Bundle/SAX/Receiver/Intent/IntentFilter/
  PendingIntent/Uri/Toast/MotionEvent 等）必须拆散到各自类文件，禁止出现
  新的 misc/杂物聚合文件。
- 现属 `dexvm_android_internal.h` 的批次入口声明随迁移消亡；共享 helper
  留在 `shared.h`。

### 2. 占位生成器切换

`tools/dexvm_stub_gen.py` 当前由 survey 报告生成"catalog 行 + 绑定到既有
标准 handler id"的代码：

- 标准中性 handler（按返回 shorty 的 0/null/void、字符串占位）改为
  `shared.h` 中的具名工厂（如 `NeutralHandler(char shorty)`、
  `PlaceholderString(...)`），生成器输出 builder 调用而非 id 字符串；
- 生成器输出目标从"往批次文件里贴行"改为"生成/追加对应类文件的
  `Declare_*()` 骨架"；
- `tests/tools/`（或既有生成器测试所在处）同步锁定新输出形状。

### 3. 迁移纪律

与 DVM-35 相同：逐类机械搬运、行为零变化、按包分批（content/app →
view/widget → graphics/media/os/net 其余）、每批全量 CTest 全绿。约 130 个
类文件的新建数超出 10 文件预算，属机械迁移型批次的预期偏离（同 DVM-35）。

## 边界（不做）

- 不删 registry/id 通道与旧入口符号（DVM-37）。
- 不改任何 handler 行为、不动 `DexVmAndroidContext` 结构。

## 验收（机器可判定）

1. 全量 CTest 全绿；`videoview`/`widget_click`/`file_vfs` 等 dexvm 集成
   用例逐位不变。
2. 迁移完成判定：`src/runtime/integration/**` 中 `Register("` 出现次数为 0；
   `AndroidIntrinsicCatalog(context)` 产出的 decl 全部内嵌实现或显式留空。
3. 生成器判定：以一份夹具 survey 报告运行 `dexvm_stub_gen.py`，输出为可
   编译的 `Declare_*()` 骨架且不含任何 handler id 字符串。
4. 抽样断言代表类（Activity/Intent/Bundle/TextView）的方法集合与迁移前
   一致；`AndroidIntrinsicCatalog` 无重复类描述符。
5. 收尾：`src/runtime/integration/MODULE.md` 与新目录 `MODULE.md` 同步、
   `docs/playbook/NEW-TITLE.md` 中生成器使用说明更新、`CURRENT.md` 滚动
   更新。

## 结果（已完成，形态偏差由 DVM-38 收口）

- `src/runtime/integration/dexvm_android/` 交付 165 个平台类的逐类
  `Declare_*(context)` 文件 + `catalog`/`shared` + `MODULE.md`；misc 拆散，
  装配点收敛为单 catalog 入参；System/Date 的 7 个平台动作由 bridge 以
  类型化成员补入 core 声明（目标缺失即装配失败）。
- `dexvm_stub_gen.py` 重写为生成逐类 `Declare_*()` builder 骨架
  （`NeutralHandler`/`PlaceholderString` 工厂，零 handler id），
  `NEW-TITLE.md` 同步；integration 层 `Register("` 归零；全量 CTest 全绿。
- 与任务书的偏差：交付形态为"声明逐类 + 实现按域聚合在 `support_*.cpp`
  填充 `AndroidHandlers` 结构体"（类型化成员消除了字符串 id，但实现未与
  声明同址）；该迁移期装配由 DVM-38 收口为完全同址。
