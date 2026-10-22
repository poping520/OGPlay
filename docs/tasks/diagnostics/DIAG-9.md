# DIAG-9 · WU9-0 / WU9-0b Framework 契约差分

状态：首轮只降不升基线已建立（2026-10-22）。只记录偏差，不修任何运行期行为。
依赖：DIAG-8 的基线/自测门禁形态、真实 dump-surface，以及固定 API19 ROM 骨架。

## 范围与判据

以 `data/dexvm/api19-skeleton.bin` 的 own-member 形状、flags、超类和直接接口为唯一判据，
覆盖 core/framework/framework2/ext 四个固定 ROM JAR，不以游戏、Java 规范或 ART 为判据。
二进制读取统一复用 `tools/api19_skeleton.py::unpack`，格式与
`src/runtime/dexvm/skeleton_surface.cpp` 一致；不另建 ROM 解析器。

仅比较 `source=intrinsic` 的类，排除 BootDex 和 skeleton 类。
`dump-surface` 方法条目新增 `overridable` 和 `origin`，其余字段不变；origin 优先按
`is_skeleton` 标 skeleton，其次 intrinsic kind 标 handwritten，其余标 bytecode。
现有 title_preflight、select_shapes、select_rom_java、dexvm_stub_gen 按字段读取，兼容新增字段。
字段没有 origin；intrinsic 类内 ROM own fields 不存在的字段不可能是骨架补入，按手写字段检查 C。

## 类别与稳定键

每个偏差键为 `类别|类描述符|成员名|成员描述符`。同一成员同类多个原因合并为一个键；
类修饰符用 `@class`，层级用 `@superclass` / `@interfaces`，其成员描述符为空。

- **A1 可覆写性**：ROM 非 final 类内非 final、非 static、非 private、非 `<init>/<clinit>` 的方法，
  运行时 overridable=false 或带 final；ROM 非 final 类在运行时为 final 也计入。
- **A2 其余修饰符**：类、方法、字段的 public/protected/private/static/final/abstract
  与 ROM 不一致。A1 已计入的新增 final 位不重复计入 A2；其他位仍独立比较。
- **B 同族/构造器缺口**：同名族至少一个 handwritten 且 implemented 的方法/构造器，
  ROM 其他 public/protected 重载为 unimplemented-ledger；另外，只要类内有任一
  handwritten 且 implemented 的方法/构造器，ROM public/protected 构造器处于
  unimplemented-ledger 就计入，不要求存在手写构造器。
- **C 多余成员**：在 ROM 存在的 intrinsic 类内，手写 public/protected own member
  在 ROM own members 中不存在。ROM 没有对应类的 intrinsic 归“OGPlay 私有类”清单，
  不参与任何偏差类别，也不进入基线类清单。
- **D 层级**：超类不同或直接接口集合不同；接口顺序不计差异。
- **E 信息项**：原版为纯 Java 的 intrinsic 类清单**未测**。
  select_rom_java 的纯 Java 准入判定耦合候选集、smali 和调用闭包，没有可直接复用的独立入口。

不推断缺失 own member 的调用可达性，不将 implemented 等同于完整语义支持；
不运行游戏场景、dalvik 或 JNI 探针，不宣称游戏兼容验收。

## 入口与门禁

```sh
build/release/ogplay-cli dexvm dump-surface > .local/analysis/wu9-0/surface.json
python3 tools/conformance/framework_contract.py --self-test
ctest --test-dir build/release -L tools -R '^tools.framework_contract_self_test$' --output-on-failure
python3 tools/conformance/framework_contract.py --surface .local/analysis/wu9-0/surface.json --report .local/analysis/wu9-0/report.md --check-baseline
# 首次初始化，或偏差减少后收紧基线：
python3 tools/conformance/framework_contract.py --surface .local/analysis/wu9-0/surface.json --update-baseline
```

默认判据和基线分别为 `data/dexvm/api19-skeleton.bin`、
`tools/conformance/framework_contract_baseline.json`；可用 `--skeleton` / `--baseline` 指定。
stdout 和 `--report` 均输出每类总数、按包前 15 名、每类最多 10 个样例、私有类清单及 E 状态。

基线保存稳定键、有 ROM 判据的 intrinsic 类清单及 ROM SHA256。
`--check-baseline` 判断新增偏差时按去掉类别前缀后的 `类|成员|描述符` 比较；
类别集合变化单列为 reclassified，并输出数量。偏差减少时提示 `--update-baseline`；
`--update-baseline` 只在无新增成员偏差时写入，允许重分类。
ROM 哈希变化或已纳入基线的类消失也失败，防止输入替换或不完整盘点掩盖偏差。
退出码：成功 0，新偏差/类消失 1，输入错误 2。首次无基线只能通过 update 初始化。

## 首轮数字与验证

共盘点 264 个 intrinsic 类：234 个有 ROM 判据，另 30 个为 OGPlay 私有类。

| 类别 | 偏差键 |
| --- | ---: |
| A1 | 381 |
| A2 | 384 |
| B | 274 |
| C | 13 |
| D | 74 |
| 合计 | 1126 |

- 必需阳性全部命中：A1 的 `GLSurfaceView.onPause()V`、`onResume()V`；
  B 的 `java/lang/ThreadGroup.<init>(Ljava/lang/String;)V`。
- 自测覆盖 A1/A2 拆分、仅 final/仅 overridable/双类原因、B 无手写构造器规则、
  C 排除私有类、非 intrinsic 排除、输入顺序、格式拒绝和基线拒绝新键/允许减少。
  直接 self-test 与 tools label 下的新 CTest 均通过。
- WU9-0 已仅构建 ogplay-cli、ogplay_app_tests，格式化后 surface 定向测试
  1/1、349463 断言通过；WU9-0b 没有新增 C++ 改动，未重建或重跑 surface 测试。
  class_linker.cpp 改动行 clang-format dry-run 加 `-Werror` 通过。
- 基线生成后立即 `--check-baseline` 复跑通过。未修偏差；E 仍未测。

证据目录：`.local/analysis/wu9-0/`。WU9-0 原有构建/测试日志保留；
本次结果、完整逐键原因、报告、阳性、自测、CTest、基线和格式验证日志使用 `wu9-0b-` 前缀。

WU9-1 检查器修正后，174 条 ROM final 类方法从 A1 重分类到 A2，已逐条核对；
检查器与中间基线 207/558/274/13/74 已提交为 `2181ffec`。上限放宽到 30 后，完整审查
46 类、207 条真实 A1，修改 28 处内部虚分派；当前工作区基线为 0/542/272/13/74，
无新增成员偏差，A1 无保留项。DEX 正负回归和 dalvik 零回归；主题测试 13 项失败与
7bf89836 相同。九场景基点均因 SDL 无显示器启动失败，无法比较帧时间，按停止条件
停止，六款小游戏未跑；未提交，见 [DVM-261](../dexvm/DVM-261.md)。

## WU9-2 B 类收敛

状态：完成本单契约与转发门禁（macOS，2026-10-23）；未运行游戏或 APK。
基点 `d20f55f1`；A1/A2/B/C/D 从 **0/542/272/13/74** 收敛到 **0/542/184/13/74**，
B 减少 88。`--check-baseline` 无新增偏差，已执行 `--update-baseline` 收紧基线。

272 条已逐条提取 ROM 方法体并分类：**F=38、S=50、X=184**；F/S 共 88，
已全部实现，未触发超过 200 条的上限规则。分类与 ROM 文件/行号见
`.local/analysis/wu9-2/classify.json`；50 个 S handler 格式化后方法体均不超过 30 个非空行，
自动盘点最多 27 行。handler 存在不代表所有参数/后续能力已兼容。

四个代表（Canvas、Message、Button this 链、EditText this/super 链）双后端全部通过，
4 用例 / 110 断言。两个构造用例明确断言 ROM style 常量和被调方原有的主题/样式异常，
不把该 X 失败伪造成构造成功；X 归属见下方新增子系统。

相关主题候选 297/325、d20f55f1 基点 293/321；28 个既有失败名称及失败断言数一致，
包括 WU9-1 已知 13 项及本次较广主题范围内另 15 个基点失败，无新增失败。
静态目录检查仅维护新增 ROM 重载的精确清单；未删减原检查。
改动后 dalvik 双后端各 85/91，回归、新通过、其他变化及 oracle migrations 均为 0。
仅构建 ogplay-cli/ogplay_tests；改动行格式检查 `-Werror`、格式化后重建及主题复跑通过。
详细入口与证据见 [DVM-262](../dexvm/DVM-262.md)。

### X 清单

下列 184 条保持原有未实现记账及异常；不通过忽略参数、改调近似重载或空状态伪造成功。
各条具体依赖与 ROM 调用指令保存在 classify.json，列表按子系统分组。

ThreadGroup 的 5 条均为 **WU9-4 边界下移候选：ROM 为纯 Java
（libcore/libdvm ThreadGroup.java），应改用原版，而不是扩充手写状态**。

### 主题/默认样式解析（Context.getThemeResId、defStyleAttr 投影）

这些是已实现 F 转发后的 X 依赖，不把 F 构造器重新计为 B 缺口或重复计入 184 条 X。
转发保留 ROM style 常量和真实被调方；主题/样式失败继续记账并抛异常，本单不扩充。

| 受阻的 F 构造链 | ROM style 常量 | X 依赖 |
| --- | --- | --- |
| `Button(Context,AttributeSet)` → `Button(Context,AttributeSet,int)` | `0x01010048` | 裸 Context 的 `getThemeResId()I` 未实现，ULE |
| `EditText(Context,AttributeSet)` → `EditText(Context,AttributeSet,int)` → `TextView(Context,AttributeSet,int)` | `0x0101006e` | `defStyleAttr` 不在既有默认样式投影，UnsupportedOperationException |
| `EditText(Context,AttributeSet,int)` → `TextView(Context,AttributeSet,int)` | 原样传递 | 传入投影外 style 时，同上 |
| `TextView(Context,AttributeSet)` → `TextView(Context,AttributeSet,int)` | `0x01010084` | 同一默认样式投影拒绝该 style |

### 组件与系统服务

| 类 | X 方法 |
| --- | --- |
| `android/app/AlarmManager` | `set(IJJJLandroid/app/PendingIntent;Landroid/os/WorkSource;)V` |
| `android/app/PendingIntent` | `send(ILandroid/app/PendingIntent$OnFinished;Landroid/os/Handler;)V`<br>`send(Landroid/content/Context;ILandroid/content/Intent;Landroid/app/PendingIntent$OnFinished;Landroid/os/Handler;)V`<br>`send(Landroid/content/Context;ILandroid/content/Intent;Landroid/app/PendingIntent$OnFinished;Landroid/os/Handler;Ljava/lang/String;)V` |
| `android/content/Context` | `getString(I[Ljava/lang/Object;)Ljava/lang/String;`<br>`registerReceiver(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;Ljava/lang/String;Landroid/os/Handler;)Landroid/content/Intent;`<br>`sendBroadcast(Landroid/content/Intent;Ljava/lang/String;)V`<br>`sendBroadcast(Landroid/content/Intent;Ljava/lang/String;I)V`<br>`startActivity(Landroid/content/Intent;Landroid/os/Bundle;)V` |
| `android/content/ContextWrapper` | `registerReceiver(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;Ljava/lang/String;Landroid/os/Handler;)Landroid/content/Intent;`<br>`sendBroadcast(Landroid/content/Intent;Ljava/lang/String;)V`<br>`sendBroadcast(Landroid/content/Intent;Ljava/lang/String;I)V`<br>`startActivity(Landroid/content/Intent;Landroid/os/Bundle;)V` |
| `android/content/pm/PackageManager` | `getResourcesForApplication(Landroid/content/pm/ApplicationInfo;)Landroid/content/res/Resources;`<br>`queryBroadcastReceivers(Landroid/content/Intent;II)Ljava/util/List;` |

### 对话框与 Drawable

| 类 | X 方法 |
| --- | --- |
| `android/app/ProgressDialog` | `<init>(Landroid/content/Context;I)V`<br>`show(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/app/ProgressDialog;`<br>`show(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;Z)Landroid/app/ProgressDialog;`<br>`show(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;ZZ)Landroid/app/ProgressDialog;`<br>`show(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;ZZLandroid/content/DialogInterface$OnCancelListener;)Landroid/app/ProgressDialog;` |
| `android/graphics/drawable/BitmapDrawable` | `<init>()V`<br>`<init>(Landroid/content/res/Resources;)V`<br>`<init>(Landroid/content/res/Resources;Ljava/io/InputStream;)V`<br>`<init>(Landroid/content/res/Resources;Ljava/lang/String;)V`<br>`<init>(Ljava/io/InputStream;)V`<br>`<init>(Ljava/lang/String;)V` |
| `android/graphics/drawable/Drawable` | `<init>()V` |
| `android/graphics/drawable/PaintDrawable` | `<init>()V` |

### 资源与内容提供者

| 类 | X 方法 |
| --- | --- |
| `android/content/ContentResolver` | `<init>(Landroid/content/Context;)V`<br>`query(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;Landroid/os/CancellationSignal;)Landroid/database/Cursor;` |
| `android/content/res/AssetManager` | `<init>()V` |
| `android/content/res/Resources` | `<init>(Landroid/content/res/AssetManager;Landroid/util/DisplayMetrics;Landroid/content/res/Configuration;)V`<br>`<init>(Landroid/content/res/AssetManager;Landroid/util/DisplayMetrics;Landroid/content/res/Configuration;Landroid/content/res/CompatibilityInfo;Landroid/os/IBinder;)V`<br>`getDrawable(IZ)Landroid/graphics/drawable/Drawable;`<br>`getString(I[Ljava/lang/Object;)Ljava/lang/String;`<br>`getText(ILjava/lang/CharSequence;)Ljava/lang/CharSequence;` |

### IntentFilter 完整状态

| 类 | X 方法 |
| --- | --- |
| `android/content/IntentFilter` | `<init>(Landroid/content/IntentFilter;)V`<br>`addDataAuthority(Landroid/content/IntentFilter$AuthorityEntry;)V` |

### 图形与 native raster

| 类 | X 方法 |
| --- | --- |
| `android/graphics/Bitmap` | `createBitmap(Landroid/graphics/Bitmap;)Landroid/graphics/Bitmap;`<br>`createBitmap(Landroid/graphics/Bitmap;IIII)Landroid/graphics/Bitmap;`<br>`createBitmap(Landroid/graphics/Bitmap;IIIILandroid/graphics/Matrix;Z)Landroid/graphics/Bitmap;`<br>`createBitmap(Landroid/util/DisplayMetrics;IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;`<br>`createBitmap(Landroid/util/DisplayMetrics;[IIIIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;`<br>`createBitmap(Landroid/util/DisplayMetrics;[IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;` |
| `android/graphics/Canvas` | `<init>()V`<br>`<init>(I)V`<br>`<init>(Landroid/graphics/Bitmap;)V`<br>`clipRect(FFFF)Z`<br>`clipRect(IIII)Z`<br>`clipRect(Landroid/graphics/Rect;)Z`<br>`clipRect(Landroid/graphics/Rect;Landroid/graphics/Region$Op;)Z`<br>`clipRect(Landroid/graphics/RectF;)Z`<br>`clipRect(Landroid/graphics/RectF;Landroid/graphics/Region$Op;)Z`<br>`drawBitmap(Landroid/graphics/Bitmap;Landroid/graphics/Matrix;Landroid/graphics/Paint;)V`<br>`drawBitmap(Landroid/graphics/Bitmap;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Paint;)V`<br>`drawBitmap(Landroid/graphics/Bitmap;Landroid/graphics/Rect;Landroid/graphics/RectF;Landroid/graphics/Paint;)V`<br>`drawBitmap([IIIFFIIZLandroid/graphics/Paint;)V`<br>`drawColor(ILandroid/graphics/PorterDuff$Mode;)V`<br>`getClipBounds(Landroid/graphics/Rect;)Z`<br>`save()I` |
| `android/graphics/Matrix` | `<init>(Landroid/graphics/Matrix;)V` |
| `android/graphics/Paint` | `<init>(Landroid/graphics/Paint;)V` |
| `android/graphics/Path` | `addRect(Landroid/graphics/RectF;Landroid/graphics/Path$Direction;)V` |
| `android/text/TextPaint` | `<init>(Landroid/graphics/Paint;)V` |

### 定位与设备服务

| 类 | X 方法 |
| --- | --- |
| `android/location/Location` | `<init>(Landroid/location/Location;)V` |
| `android/location/LocationManager` | `<init>(Landroid/content/Context;Landroid/location/ILocationManager;)V`<br>`removeUpdates(Landroid/app/PendingIntent;)V`<br>`requestLocationUpdates(JFLandroid/location/Criteria;Landroid/app/PendingIntent;)V`<br>`requestLocationUpdates(JFLandroid/location/Criteria;Landroid/location/LocationListener;Landroid/os/Looper;)V`<br>`requestLocationUpdates(Landroid/location/LocationRequest;Landroid/app/PendingIntent;)V`<br>`requestLocationUpdates(Landroid/location/LocationRequest;Landroid/location/LocationListener;Landroid/os/Looper;)V`<br>`requestLocationUpdates(Ljava/lang/String;JFLandroid/app/PendingIntent;)V`<br>`requestLocationUpdates(Ljava/lang/String;JFLandroid/location/LocationListener;)V` |
| `android/media/AudioManager` | `<init>(Landroid/content/Context;)V` |
| `android/os/Debug` | `getMemoryInfo(ILandroid/os/Debug$MemoryInfo;)V` |
| `android/os/Vibrator` | `vibrate(ILjava/lang/String;J)V`<br>`vibrate(ILjava/lang/String;[JI)V` |
| `android/telephony/PhoneStateListener` | `<init>(I)V` |
| `android/telephony/TelephonyManager` | `<init>(Landroid/content/Context;)V`<br>`getPhoneType(I)I` |

### 窗口、输入与样式

| 类 | X 方法 |
| --- | --- |
| `android/opengl/GLSurfaceView` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V` |
| `android/view/Display` | `<init>(Landroid/hardware/display/DisplayManagerGlobal;ILandroid/view/DisplayInfo;Landroid/view/DisplayAdjustments;)V` |
| `android/view/InputQueue` | `<init>()V` |
| `android/view/MotionEvent` | `addBatch(JFFFFI)V`<br>`addBatch(Landroid/view/MotionEvent;)Z`<br>`obtain(JJIFFFFIFFII)Landroid/view/MotionEvent;`<br>`obtain(JJIFFI)Landroid/view/MotionEvent;`<br>`obtain(JJIIFFFFIFFII)Landroid/view/MotionEvent;`<br>`obtain(Landroid/view/MotionEvent;)Landroid/view/MotionEvent;` |
| `android/view/SurfaceView` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/view/View` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V`<br>`invalidate(IIII)V`<br>`invalidate(Landroid/graphics/Rect;)V`<br>`postInvalidate(IIII)V` |
| `android/view/ViewGroup` | `<init>(Landroid/content/Context;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/view/WindowManager$LayoutParams` | `<init>(Landroid/os/Parcel;)V` |
| `android/view/WindowManagerImpl` | `<init>(Landroid/view/Display;)V` |
| `android/view/inputmethod/InputMethodManager` | `hideSoftInputFromWindow(Landroid/os/IBinder;ILandroid/os/ResultReceiver;)Z`<br>`showSoftInput(Landroid/view/View;ILandroid/os/ResultReceiver;)Z` |
| `android/webkit/HttpAuthHandler` | `<init>()V` |
| `android/widget/AbsoluteLayout$LayoutParams` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V` |
| `android/widget/AbsoluteLayout` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/FrameLayout` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/ImageButton` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/ImageView` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/LinearLayout` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/ProgressBar` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;II)V` |
| `android/widget/RelativeLayout` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/ScrollView` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V` |
| `android/widget/TableLayout` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V` |
| `android/widget/TableRow` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V` |
| `android/widget/TextView` | `getTextColors(Landroid/content/Context;Landroid/content/res/TypedArray;)Landroid/content/res/ColorStateList;`<br>`setCompoundDrawablesWithIntrinsicBounds(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V`<br>`setText([CII)V` |
| `android/widget/Toast` | `<init>(Landroid/content/Context;)V` |

### 任务与消息队列

| 类 | X 方法 |
| --- | --- |
| `android/os/AsyncTask` | `execute(Ljava/lang/Runnable;)V`<br>`get(JLjava/util/concurrent/TimeUnit;)Ljava/lang/Object;` |
| `android/os/Handler` | `<init>(Landroid/os/Handler$Callback;Z)V`<br>`<init>(Landroid/os/Looper;Landroid/os/Handler$Callback;Z)V`<br>`<init>(Z)V`<br>`hasMessages(ILjava/lang/Object;)Z`<br>`removeCallbacks(Ljava/lang/Runnable;Ljava/lang/Object;)V` |
| `java/util/Timer` | `scheduleAtFixedRate(Ljava/util/TimerTask;Ljava/util/Date;J)V`<br>`schedule(Ljava/util/TimerTask;Ljava/util/Date;)V`<br>`schedule(Ljava/util/TimerTask;Ljava/util/Date;J)V` |

### 文件描述符与电源服务

| 类 | X 方法 |
| --- | --- |
| `android/os/ParcelFileDescriptor` | `<init>(Landroid/os/ParcelFileDescriptor;)V`<br>`<init>(Ljava/io/FileDescriptor;)V`<br>`<init>(Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;)V`<br>`open(Ljava/io/File;ILandroid/os/Handler;Landroid/os/ParcelFileDescriptor$OnCloseListener;)Landroid/os/ParcelFileDescriptor;` |
| `android/os/PowerManager$WakeLock` | `release(I)V` |

### WebView provider

| 类 | X 方法 |
| --- | --- |
| `android/webkit/WebView` | `<init>(Landroid/content/Context;Landroid/util/AttributeSet;)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;ILjava/util/Map;Z)V`<br>`<init>(Landroid/content/Context;Landroid/util/AttributeSet;IZ)V`<br>`loadUrl(Ljava/lang/String;Ljava/util/Map;)V` |

### 类加载与 XML 工厂

| 类 | X 方法 |
| --- | --- |
| `dalvik/system/BaseDexClassLoader` | `<init>(Ljava/lang/String;Ljava/io/File;Ljava/lang/String;Ljava/lang/ClassLoader;)V` |
| `javax/xml/parsers/SAXParserFactory` | `newInstance(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljavax/xml/parsers/SAXParserFactory;` |
| `org/xml/sax/InputSource` | `<init>(Ljava/io/Reader;)V`<br>`<init>(Ljava/lang/String;)V` |

### 编码器与 NIO adapter

| 类 | X 方法 |
| --- | --- |
| `java/io/InputStreamReader` | `<init>(Ljava/io/InputStream;Ljava/nio/charset/CharsetDecoder;)V` |
| `java/io/OutputStreamWriter` | `<init>(Ljava/io/OutputStream;Ljava/nio/charset/CharsetEncoder;)V` |
| `java/nio/CharBuffer` | `wrap(Ljava/lang/CharSequence;)Ljava/nio/CharBuffer;`<br>`wrap(Ljava/lang/CharSequence;II)Ljava/nio/CharBuffer;` |
| `java/nio/DirectByteBuffer` | `<init>(Ljava/nio/MemoryBlock;IIZLjava/nio/channels/FileChannel$MapMode;)V` |
| `java/nio/charset/Charset` | `<init>(Ljava/lang/String;[Ljava/lang/String;)V` |

### ThreadGroup 原版下移

| 类 | X 方法 |
| --- | --- |
| `java/lang/ThreadGroup` | `<init>(Ljava/lang/String;)V`<br>`<init>(Ljava/lang/ThreadGroup;Ljava/lang/String;)V`<br>`enumerate([Ljava/lang/Thread;Z)I`<br>`enumerate([Ljava/lang/ThreadGroup;)I`<br>`enumerate([Ljava/lang/ThreadGroup;Z)I` |

### 网络与 URL handler

| 类 | X 方法 |
| --- | --- |
| `java/net/DatagramPacket` | `<init>([BIILjava/net/SocketAddress;)V`<br>`<init>([BILjava/net/SocketAddress;)V` |
| `java/net/DatagramSocket` | `<init>(I)V`<br>`<init>(ILjava/net/InetAddress;)V`<br>`<init>(Ljava/net/DatagramSocketImpl;)V`<br>`<init>(Ljava/net/SocketAddress;)V` |
| `java/net/Socket` | `<init>(Ljava/lang/String;ILjava/net/InetAddress;I)V`<br>`<init>(Ljava/lang/String;IZ)V`<br>`<init>(Ljava/net/InetAddress;ILjava/net/InetAddress;I)V`<br>`<init>(Ljava/net/InetAddress;IZ)V`<br>`<init>(Ljava/net/Proxy;)V`<br>`<init>(Ljava/net/SocketImpl;)V` |
| `java/net/URL` | `<init>(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V`<br>`<init>(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/net/URLStreamHandler;)V`<br>`<init>(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V`<br>`<init>(Ljava/net/URL;Ljava/lang/String;)V`<br>`<init>(Ljava/net/URL;Ljava/lang/String;Ljava/net/URLStreamHandler;)V`<br>`openConnection(Ljava/net/Proxy;)Ljava/net/URLConnection;` |

### Locale 扩展

| 类 | X 方法 |
| --- | --- |
| `java/util/Locale` | `<init>(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/util/Set;Ljava/util/Map;Ljava/util/Map;Z)V` |

### 原始 guest 内存

| 类 | X 方法 |
| --- | --- |
| `libcore/io/Memory` | `peekInt(JZ)I`<br>`peekLong(JZ)J`<br>`peekShort(JZ)S`<br>`pokeInt(JIZ)V`<br>`pokeLong(JJZ)V`<br>`pokeShort(JSZ)V` |
