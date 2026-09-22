# DASH-02 · 运行时有界只读快照

状态：Windows 实现与定向验收完成；Linux 暂缓。依据 [Dashboard 规划](../../design/gui/dashboard.md)。

## 实现

- DexVM：try VM 锁读取 heap used/target/growth/max、对象、登记/链接/初始化类、解释/intrinsic/native 调用；GC 使用 Clock 累计暂停与回收字节。
- JNI：local/global/weak 与附着线程计数；memory：8 种页权限（含 PROT_NONE）及 mapping generation。
- Dynarmic：owner 完成 Run 后发布实际缓存容量/占用/full flush 与时间戳；读取最多 128 个 processor。未发布行是 unavailable/null，不冒充零或实时状态。Windows x64 构建目录生成受检扩展，固定 submodule 不变。
- VFS：mount/FD 各 128 项、总数、稳定 node_id/offset、IO 预算和成功 FlushAll 计数。忙碌打开状态单行 busy，不等待 backing IO。
- NativeLibraryLoader：try-lock registry，最多 128 项、文本 512 UTF-8 bytes；保持 Loading/Loaded/Failed。
- run-apk 将这些来源接到既有 Dashboard；忙/未接入保持 unavailable，集合裁剪明确 partial。

## 验证

- Windows Release `ogplay_tests` / `ogplay` 构建成功。
- `Dashboard*,MCP HTTP*`：23 项 / 596 断言通过，含 VM/JNI/VFS 竞争下 100 ms 内返回、真实 GC 回收计数、权限变化、FD 身份、实际 JIT 发布及未发布 null。
- 精确 CPU Dynarmic / runtime JNI / runtime VFS 文件筛选：45 项 / 317 断言通过。
- native loader 与 lifecycle 定向筛选：8 项 / 92 断言通过，含 registry 快照和嵌套 JNI OnLoad。
- 真实 APK manual-step 会话连接全部 14 区；持续观测 frame 保持 0，独立 MCP step 后快照达到 frame 3。生命周期 running 与未发布 CPU null 已机器断言。

## 验收边界

扩展文件通配检查误包含 DexVM File VFS 用例，114 项中 21 项失败：涉及 File 类形状/权限/FD 语义，以及 GC/对象流 fixture 的 `CursorWindow.nativeAllocRow` 未绑定。失败证据保留于 `.local/dash02-module-tests.log`；不把它们计作通过，也未修改这些独立问题。直接 GC 计数用例不依赖该 BootDex fixture。

这些证据是诊断能力验证，不是游戏兼容验收。CPU 是最近完成 Run 的发布值；不承诺跨模块原子快照。
