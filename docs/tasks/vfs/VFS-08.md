# VFS-08 · 打开状态与原子追加写

状态：有界完成。日期：2026-10-10。依赖：VFS-01 节点/打开状态、VFS-02 资源预算、syscall ABI。

## 范围与实现

open/openat 显式解码 ARM O_APPEND=0x400；VfsOpenOptions 将不可变 append 状态保存到
OpenFile，dup 共享同一状态。普通非空 Write 在节点锁内选 EOF、检查尺寸/配额、写入并
更新打开状态 offset；seek 不解除 append，独立 FD 与别名共享节点锁，不是打开时单次 seek。
空 Write/WriteAt 不改变 offset、尺寸或 generation。

WriteAt 对 append fd 忽略指定位置并追加，同时保留顺序 offset，遵循 Android/Linux
[pwrite 行为](https://man7.org/linux/man-pages/man2/pwrite.2.html)；非 append fd 的定位语义保持。
[O_APPEND](https://man7.org/linux/man-pages/man2/open.2.html) 的 EOF 选择和写入在同一锁内。

writev 先完整预检各向量及输入，再合并为一次 Write，不允许其他 append writer 在
同一向量记录中间插入。每段最多 1 MiB、最多 64 段；普通 FD 暂存消耗既有 VFS 共享
资源预算，失败时不交付前半段。大于 64 KiB scratch 的 append write/pwrite64 同样
暂存后一次提交，在共享预算内有界；非 append 的大请求仍使用原分块/短 IO。
追加目录不支持，未知 flags 明确 EINVAL；fcntl 状态读改仍按账本 ENOSYS，不伪造。
没有改变 BootDex/原 stdio 算法、游戏二进制或真实统计服务。

## 验证

macOS Release ogplay/ogplay_tests 构建通过，定向 16 项/489 断言通过。
覆盖 seek/dup/alias、独立并发 writer 的完整记录、空写、只读 FD、Linux append pwrite、
ARM openat 实际 flags 0x20441、writev 预检失败不部分提交、超过 scratch 边界的一次
追加提交、暂存资源配额，以及 dirty quota 失败后内容/offset/generation 保持、flush/
跨重开与 truncate+append。复用非 append 大文件/定位 IO/权限/FD reuse 与日志 writev 回归。
证据 `.local/wb-append-fix/{build-final.log,tests.log,tests-positioned.log}`。

## reached-fault

原 APK/外部数据、无 Profile/无 survey、复制用户沙盒，原 fputs/__sfvwrite 读 0xC 首错
未再出现，appInit 完成。实际 native stdio 落盘 glot_log.txt 为 1508 字节、11 条完整 JSON
记录，验证原 fopen(a)→fputs/flush 路径能够继续，未用假 FILE* 或吞错。
达到 --exit-after-frames 300 的停止条件时，f=11795 开始正常 teardown；onPause 的
Game.nativeCanInterrupt 因累计清理墙钟预算耗尽报错。这不是新运行期首错。
退出 1、无超时，35.21 秒，进程已结束；不宣称 clean shutdown/完整游戏画面或可玩验收。
退出策略、Java IoRuntime 既有 seek 模拟 append 的并发收敛及 Windows/Linux 实跑不在本轮范围。
证据 `.local/wb-append-fix/{run.log,result.json,sandbox/}`。
