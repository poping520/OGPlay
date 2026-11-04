# GUI-21 · Windows 启动器单例

状态：Windows Debug / Release 构建与单例专项测试通过，2026-11-04。

范围：同一登录会话仅允许一个启动器，不按 EXE 路径或 library-root 区分。
游戏使用同一 EXE 的 `--run-game` 子进程，保持原有实例运行锁；帮助和版本查询直接执行。

实现：Windows EXE 入口在 GUI 初始化前取得 named mutex，持有至运行结束。
重复启动经主窗口属性定位已有窗口，授权前台切换并投递注册消息，恢复托盘隐藏或最小化窗口后退出。
首个窗口仍在初始化时沿用原有导航完成/超时显示机制。锁随正常退出或崩溃释放。
无新依赖，不改 macOS、CLI、游戏生命周期或运行时。

验证记录：提交前使用临时专项脚本检查已解锁桌面及 Explorer 托盘。
该检查仅管理自身创建的进程及临时库，覆盖四进程并发启动、重复打开、托盘恢复、最小化恢复、
拒绝启动不写入其他库、帮助/版本/游戏模式绕过锁，以及正常/异常退出后重新启动。
专项脚本及对应 CTest 注册已移除，验证日志保留在本地 `.local/gui21/`。

Debug 专项检查通过（3.02 秒）；能力账本 439 项校验及 `git diff --check` 通过。未运行全量测试或游戏兼容验收。
构建目标仅 `ogplay`，产物 `build/windows-msvc/Debug/ogplay.exe`。
现有 Debug 依赖首次构建遇到 C1128（android_boundary_hle 对象节数）和 C4702-as-error
（android_input 不可达代码）；仅在构建进程环境 `_CL_=/bigobj /MP2 /wd4702` 中处理，
不修改运行时源码或项目默认编译选项。该结果不代表默认 Debug 构建已无既有阻塞。
前次 Debug 验证时保留用户已有三个旧版 Release 启动器；本次 Release 构建前已确认旧进程退出。

Release 按默认编译选项构建通过，无需 Debug 的本地兼容参数：

```sh
cmake.exe --build --preset windows-msvc --config Release --target ogplay --parallel 2
```

Release 专项测试通过（2.73 秒），覆盖与 Debug 相同的单例/恢复/重启路径。
产物 `build/windows-msvc/Release/ogplay.exe`；日志 `.local/gui21/{build-release,test-release}.log`。
