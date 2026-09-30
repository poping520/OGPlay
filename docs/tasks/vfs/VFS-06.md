# VFS-06 · native 安全随机字符设备

状态：设备能力完成；真实存档闭环阻塞。决策：[ADR-0086](../../adr/runtime.md#adr-0086)。

## 范围

- VFS 注入式只读字符设备，每次 read 生成新字节；FD 复制、关闭、回调撤销及错误传播。
- integration 为 `/dev/urandom` 与 `/dev/random` 注入现有 HAL 安全随机源。
- ARM open 支持游戏实际访问的 `0x20900` 标志，stat64/目录发布字符设备元数据。
- 不模拟 Linux 熵池、不改 Java/BootDex/游戏，也不补支付服务。

## 证据与验收边界

- 修复前实际游戏 open 两条随机路径均返回 -EINVAL，flags=133376 (`0x20900`)；
  除设备缺失，还存在 O_NONBLOCK/O_NOCTTY 解码缺口。
  证据 `.local/random-baseline-flags.stderr.log`。
- Windows `windows-msvc` Release `ogplay` / `ogplay_tests` 构建通过；沿用本机已有
  `_CL_=/wd4996 /wd4834`，未修改相关警告源码。
- 定向 58 用例/458 断言通过：连续读取、短读/错误、回调撤销等待、FD 关闭/复用/dup，
  ARM flags/stat64/DT_CHR/EFAULT/EIO/分块失败，以及真实 API19 ARM libc→HAL 两种设备。
  证据 `.local/random-final-build.log`、`.local/random-tests.log`。
- 同一 APK/OBB、无 Profile、独立持久沙盒、自由运行 MCP：f=1810 关闭剧情弹框，
  Mono 以 `0x20000` 打开 `/dev/urandom` 得到 fd=91，read(8) 返回 8；原安全随机源异常
  未再出现。随后 f=1811 在 nativeRender/NewStringUTF 失败，提示 modified UTF-8 未终止，
  LR=0x6142bca8，CLI 返回 1；没有形成游戏进度存档，保存/退出/重载未验收。
  证据 `.local/random-game-{before.png,click.json,stderr.log}`，属于 reached-fault。
- 修复前诊断复跑的关闭请求未及时结束，有限观察后停止；不据此宣称退出已验收。
