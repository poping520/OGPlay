# SBX-15 · 匿名 MAP_FIXED 原子替换

状态：能力交付完成。依赖 [SBX-14](SBX-14.md) 与 memory/syscall 契约。

## 范围与验收

- 普通 Map 保持冲突检查；独立 ReplaceAnonymous 在同一地址空间锁内替换目标 guest 页，
  允许空洞，先准备 backing，再清零并发布权限、账本、直接页表和 mapping generation。
- 4 KiB guest 边界不随宿主页尺寸变化；范围外数据保留，失败不改变现有 guest 映射。
- mmap2 匿名私有 MAP_FIXED 使用该接口，返回原地址或 errno；不扩展文件 mmap。
- 定向验证部分/跨映射覆盖、空洞、清零、相邻页、权限、写入票据、世代及失败路径；
  同启动路径确认原段扩展错误消失并记录下一首错，保留 JNI 版本校验。

## 实现与验证

- `AddressSpace::ReplaceAnonymous` 与普通 Map 共用锁内清零/发布流程；普通 Map 保留冲突检查。
  backing 提交回滚列表预先分配，避免成功提交后因记录分配失败遗漏回滚。
- mmap2 对匿名私有 MAP_FIXED 返回原地址；未对齐/非法参数 EINVAL、低地址 guard EPERM、
  长度或范围溢出与 backing 失败 ENOMEM。未扩展文件 mmap 或非零页偏移。
- `--diag` 经既有上层 syscall observer 记录固定匿名映射地址、长度、权限、flags、返回值与
  JNI pending 状态；未 attach 的线程记为 unattached，不触碰其 JNI 异常状态。
- Release 受影响目标 `ogplay_tests/ogplay` 构建通过。定向 22 用例/165 断言通过，含新增
  5 用例以及已有 first-fit、Map、bus、快照、mmap/mprotect/munmap/brk/madvise 回归。
  本机宿主页 16 KiB，实测部分覆盖、跨映射/空洞、邻页数据、权限与直接页表、世代、
  旧写入票据、受检并发读、非法参数不变及最后一个 guest 页。未注入宿主 backing OOM。

同 APK/OBB、无 Profile、关闭 survey、ephemeral sandbox 的原启动路径（120 帧上限）
实测 `mmap2(0x6112c000,0x1ef54,3,0x32,-1,0)` 返回 `0x6112c000`，pending=false；
原 `failed to extend segment` 与 JNI 返回版本 0 错误均未再出现。
下一首错为 JNI_OnLoad 内 guest memory fault：`pc=0x304a42ec` 读取未映射地址 `0xb5`。
代理初始化未完成，本轮只证明越过原段扩展错误，不代表游戏兼容验收。

证据：`.local/sbx15-build.log`、`.local/sbx15-tests.log`、`.local/sbx15-dt-startup.log`；
启动返回值与下一首错的机器断言见 `.local/sbx15-startup-check.log`。未修改 APK、BootDex
或 JNI 版本校验。
