# 图形、EGL 与帧生产

图形子系统把 API19 EGL/GLES 调用连接到 ANGLE，并将完成的生产端图像交给窗口合成。
gles 模块隔离 ANGLE 与搬运机制，boundary 管理 guest ABI 和 Context 状态，
Android 平台层负责 ROM Surface 到窗口图层的连接。

## 调用与数据流

```text
ARM EGL/GLES import ─────┐
Java GLES / GLImpl native ┼→ sealed binding → EGL registry / GL state → ANGLE
                        ┘                                      ↓
软件 Canvas / 视频 → WindowSystem ← Surface 发布 ← readback / swap
                         ↓
                    FrameService → HAL present
```

native 和 managed Java 入口共用 binding、registry、current 与 error。
Java 适配只解决数组、Buffer、字符串和句柄编组。
不能为 managed API 再创建一份图形状态或导出目录。
普通数学工具应执行 ROM Java；现存替换的移除另见已知问题。

## ANGLE 后端

ANGLE 承担 GLES 语义与平台驱动连接。
Windows、Linux、macOS 的候选顺序由显式能力探测决定。
软件渲染也是 ANGLE Vulkan/SwiftShader，不使用另一套 GLES 实现。
hardware-only 不得静默回退软件设备。
无 ANGLE 时明确不可用，不回退系统 EGL。

SDK 来自固定预编译来源，头文件、库和 manifest 必须匹配。
构建与打包规则见工具说明，本篇不维护版本或文件哈希副本。
平台原生类型限制在适配内部，不能进入 guest ABI。
宿主的内部 pbuffer 可以承载 guest 逻辑 window surface。
因此 guest Surface 身份不等于一个宿主窗口指针。

## EGL 对象关系

Display/config 描述进程可用能力。
Context 拥有 native Context，Surface 独立拥有存储。
二者不能合并，否则不同 draw/read Surface 和共享 Context 无法表达。
registry 以单调逻辑句柄向 guest 发布身份。

```text
Display
  ├─ Config
  ├─ share group → Context A / Context B
  └─ Surface X / Surface Y
thread current = Context + draw Surface + read Surface
```

current、bound API 和 sticky EGL error 按线程隔离。
Context 的 owner 决定哪个宿主线程可以操作实际 ANGLE 对象。
跨线程抢占失败，必须先显式解绑再移交。
current 对象销毁可以延迟到解绑，不能立即释放仍在执行的资源。

eglGetProcAddress 冷查 sealed callable，未知扩展返回 null。
稳定 thunk 按调用线程 current client version 路由。
同名导出不能由 ELF 依赖顺序决定语义。
无 Context 查询与实际 current 失配有各自错误规则。

## GL 状态与对象共享

每个 Context 有独立的固定管线、client array 与绑定状态。
可共享对象元数据按 share group 保存。
GLES1 与 GLES2/3 在同一 Context 上使用同一纹理、buffer 和共有状态。
对象名由 ANGLE 生成，不能由宿主影子表伪造。

shadow 只在 native mutation 成功后更新。
查询取真实 ANGLE 或对应逻辑 shadow，不能猜未知 pname 的长度。
内部 staging 操作保存并恢复 guest binding。
用于固定管线的临时 shader、VAO 和 buffer 不发布成 guest 状态。

guest GL 首错按 Context 锁存。
glGetError 先消费锁存，再查询 ANGLE。
诊断读取不消费错误，也不把旧错误归给本次成功调用。
内存与生命周期故障继续硬失败，不能全局转换成 GL_INVALID_ENUM。

## 参数搬运

标量按 A32/JNI 位型解码，指针有方向、长度和可空性声明。
输入完整预检并读取后才调用 ANGLE。
输出先预检，成功后显式提交，不能靠析构自动写回。
pack/unpack alignment、row/skip 和 PBO offset 共同决定内存范围。

client array 保存定义时的 buffer binding。
draw 时根据 first/count 或实际索引范围验证属性字节。
VBO offset 不等同于 guest 地址，EBO 索引需要按实际对象数据求范围。
staging 可复用容量，每次仍重新读取输入。
成功与异常出口都恢复内部临时绑定。

Buffer mapping 由共享服务分配受检 guest arena。
map、flush 和 unmap 保留实际范围与 flags。
explicit flush 只回写指定范围，unmap 不能再覆盖已提交内容。
映射身份跟随 share group 和真实 buffer，删除或最后 Context 退役时清理。

## GLES1 固定功能

固定管线状态转换成 ANGLE 可执行的 shader 和受检顶点数据。
矩阵、lighting、fog、alpha-test 与 texture environment 都必须影响绘制。
仅保存 setter 值不能算实现。
多纹理 stage 独立保存坐标、矩阵和 sampler，不借用错误 stage 的坐标。

flat shading 按 primitive 的 provoking vertex 展开。
point size、matrix palette 和 texture crop 各有明确支持边界。
范围外能力显式失败，不通过固定常量假装支持。
精确枚举与限制归 GLES1 契约维护。

## GLES2/3 与 API19 导出

shader、program、uniform、framebuffer 等语义由 ANGLE 执行。
编译日志和查询长度按真实 metadata 搬运。
relink/delete 清理旧 uniform shape，不能用过期宽度写 guest 输出。
GLES3 共用入口仍保留 GLES3 参数语义，不退回 GLES2 的较窄规则。

API19 导出表与可执行后端能力分开。
固定桩库要求的入口可以解析，缺后端能力按明确的 gl_unimplemented 规则记账。
这种回退不增加扩展串，也不消费未支持入口的指针。
EGL 使用自己的校验、error 与返回 ABI，不共用 GL 回退。

压缩纹理发布由真实能力加已实现解码形成 available，再由策略收窄。
格式数组、数量、上传校验和扩展串必须来自同一 published 集合。
ETC1、ATC 和 PVRTC 的软件解码只在确有实现时参与发布。
未知格式不能借通用回退伪造支持。

## Surface、读回与 FrameService

swap 只发布 draw Surface，保留 read framebuffer 绑定。
readback 转为顶部首行 RGBA，方向差异不泄漏给每个消费者。
glFlush 只提交命令，不代表 present 或推进帧序号。
超采样仅放大默认 framebuffer，用户 FBO 保持 guest 尺寸。
最终 resolve 返回逻辑尺寸，查询也报告逻辑坐标。

FrameService 管理统一发布序号、指标和帧缓冲回收。
软件、空白和 EGL 帧遵守同一代际规则。
复用容量不等于复用旧内容或旧序号。
窗口合成见 [Android 平台层](android-framework.md)。

实时路径可显式启用能力受检的异步读回。
collector 拥有内部共享 Context，停止时唤醒并 join。
旧尺寸、Context 或 Surface 的回调不能覆盖新代帧。
手动步进和普通 guest readPixels 保持原有同步完成语义。

SurfaceTexture 使用独立队列和图像 lease。
update 在合法 current Context 上上传或导入，不能隐式创建 Context。
缺 external 能力明确失败，构造本身不自动切换后端。
MediaPlayer 和 NDK producer 的剩余接入边界见已知问题。

## 契约、决定与操作

- [gles](../../src/gles/MODULE.md)、[boundary](../../src/runtime/boundary/MODULE.md)
- [EGL](../../src/runtime/boundary/modules/egl/MODULE.md)、[GLES1](../../src/runtime/boundary/modules/gles1/MODULE.md)
- [GLES2](../../src/runtime/boundary/modules/gles2/MODULE.md)、[GLES3](../../src/runtime/boundary/modules/gles3/MODULE.md)
- [services](../../src/runtime/boundary/services/MODULE.md)
- [ANGLE 决定](../adr/media.md#adr-0003)、[EGL 对象决定](../adr/media.md#adr-0063)
- [MCP 观察](../playbook/MCP-TESTING.md)、[已知问题](../state/KNOWN-ISSUES.md)
