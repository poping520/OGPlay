# DVM-71 · Window/Display 状态与 JNI 继承身份

## 目标（一句话）

按 API19 可观察语义闭合当前 reached Window/Activity/Display 状态方法，修复 JNI
批量发布丢失 superclass chain 的对象模型缺陷，并暴露下一真实阻断。

## 依赖

- DVM-70 设计
- 已有 `DexVmAndroidContext`、Activity/Window intrinsic 与稳定 singleton identity
- AOSP `android-4.4.4_r2.0.1` `Window.java` / `DisplayMetrics.java` / `Display.java`
- `DexVmGuestBridge::RegisterClassForNative()` 的递归发布路径与 JNI class registry
  既有严格 object-array assignability contract

## 语义边界

- `LayoutParams` 声明 API19 可观察 flags/windowAnimations/softInputMode/type；
  `setFlags(flags, mask)` 按 `(old & ~mask) | (flags & mask)` 修改同一 record，
  `addFlags`/`clearFlags` 分别等价于 `setFlags(flags, flags)` / `setFlags(0, flags)`；
  `setSoftInputMode(0)` 只取消 explicit-mode 事实，按 AOSP 不覆盖旧值；
- requested orientation 以 Activity identity 保存，默认
  `SCREEN_ORIENTATION_UNSPECIFIED(-1)`；本族只保存 guest 可观察状态，不伪造宿主
  Binder/IME/旋转；
- `DisplayMetrics` 宣告 API19 public/noncompat metric fields，constructor 保持 Java
  初始零值；`Display.getMetrics/getRealMetrics` 填充同一 caller-owned record，
  managed surface 无 system decor/兼容缩放，app/real 尺寸相同；density 来自注入值，
  `densityDpi=round(density*160)`，不读宿主私有显示器信息；null output 抛 Java
  `NullPointerException`；
- DexVM linker 中 application Activity 的 superclass 是 intrinsic
  `android/app/Activity`；批量发布 interpreted class 必须复用递归、幂等的
  `RegisterClassForNative()` 由父到子注册完整层级，不维护第二套类声明/handler 安装
  逻辑；不放宽 `JniObjectArrayStore` 严格 assignability，不改变 object identity、
  JNI reference domain 或 reflection 类型语义。

## 验收

- [x] `LayoutParams` 声明 flags/windowAnimations/softInputMode/type 字段，Window
      set/add/clear flags、soft-input mode 与 type 写入同一 attributes record；
- [x] Activity requested orientation 按 receiver identity 隔离，默认 -1，set/get 等价；
- [x] `DisplayMetrics`/`Display` 以注入 surface/density 事实填充 caller-owned record，
      null output 明确失败；
- [x] interpreted Activity subclass 在 JNI registry 中保留完整 superclass chain，
      object array 按真实 Java assignability 接受子类对象且无关类型仍被拒绝；
- [x] focused intrinsic/object model 测试通过；
- [x] 关闭 survey 的 bounded exact run 越过已知 fault，并固定新第一失败或帧；
- [x] 运行后无 `ogplay` 残留，同步 MODULE/CURRENT/capability。

验收：focused 3/3、3/3、5/5。exact run 关闭 survey，frame limit 3、wall limit 45 s，
依次越过 Window/orientation 方法、`DisplayMetrics` class/method/field 链接与
object-array assignability fault；新首 fault 为
`android.os.Environment.getDataDirectory()Ljava/io/File;` 未解析。结束后无
`ogplay` 进程。

状态：完成。
