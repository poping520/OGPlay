# VFS-11 · guest 数据路径的父目录解析

状态：有界完成，2026-11-01。依赖：VFS-09、现有 VFS 路径索引与宿主 backing 隔离。
决策：[ADR-0020](../../adr/session.md#adr-0020)。

## 范围与验收

普通 guest 数据路径支持相对/绝对 `..`，逐级验证中间目录，根的父目录仍为根；
解析仅查询 guest 索引，别名、只读与 overlay 权限沿用既有机制。挂载条目、沙盒编码及
宿主 backing 继续拒绝 traversal；不引入宿主 chdir、symlink 或标题分支。

- 既有 vfs_tests.cpp 补充资源相对路径、根/别名、中间目录错误及宿主隔离回归；修复前失败。
- 现有 syscall_tests.cpp 覆盖 open/openat/stat/access 共享解析，失败不改变 cwd。
- 仅构建 Release ogplay-cli/ogplay_tests，运行 VFS 与 GLES1 定向测试及 capabilities check。
- DH 1024×600 同一路径进入第一关：MCP 截图确认精灵与光晕恢复、无白块，SIGTERM 退出 0。

## 取证

原 glLightfv 确有误用当前矩阵模式的问题，但 DH 设置位置时为 MODELVIEW，矩阵修正不消除白块。
真机 GL trace 对应 quad 带 UV 和纹理，宿主却是无纹理顶点。宿主 open 记录显示
`data/3D/characters/faeries/../../interface/faery_dust.tga` 被旧 traversal 政策拒绝，
导致纹理未创建。证据位于 `.local/dh-{device-gl.log,file-probe.jsonl,upload-probe.jsonl}`。

## 结果

- 修复前两个 VFS 回归均抛 traversal 拒绝；修复后 VFS/沙盒 67 用例、25851 断言通过，
  cwd/文件 syscall 5 用例、86 断言通过；包含虚拟别名、只读权限、中间目录与宿主边界。
- glLightfv/glLightxv 的 position/spot direction 均固定取 modelview；新增回归修复前
  24 断言失败，修复后 91 断言通过。GLES1 主题共 26 用例、2037 断言通过。
- clang-format 格式化改动范围，仅重建 Release ogplay-cli/ogplay_tests；
  capabilities check、UTF-8、文档链接及 git diff --check 通过。
- 原 library run 命令、原存档与 1024×600，MCP 按既定点击路径进入第一关。
  两次捕获均显示精灵、金色光晕、烛台及场景正常，大白块消失；SIGTERM 退出码 0，
  本次停止时呈现 15745 帧。证据：`.local/dh-fixed-{level.png,level-final.png,capture.json,run.log,proof.json}`
  与 `.local/dh-{vfs-tests,gles1-tests,syscall-tests}.log`。
- 原版材质、贴图格式与光照算法未改，无 Profile/标题专用补丁；所有临时探针与 shader 染色已撤除。
  真机 GL trace 设置恢复、屏幕熄灭。选职业界面、全关卡和跨平台未复测，不构成 title gate；
  默认 800×480 已知 CG 后闪退保持本次范围外。
