# APS-15 · Title Profile v4 最小身份覆盖层

目标：Profile 只保留 identity、quirks、input，标准启动不依赖 Profile。
依赖：APS-1/2/7/8 的 Manifest/ABI/generic startup；标准音频与 VFS 已有实现。
决定：[ADR-0141](../../adr/session.md#adr-0141)。

## 边界与前置证据

- 删除前重建 ogplay_tests：MediaPlayer/SoundPool、默认音频 loader、Manifest launcher/alias
  共 9 项通过；原 OBB 标准路径测试 1 项通过。旧二进制曾报 SurfaceTexture native
  未绑定，重建后消失；未修改该能力。
- 删除 v1/v2/v3 兼容、runtime/data/audio 与 legacy 根库启动规划及其专用测试。
- 保留标准 Android Context/Surface 等混合测试，不将其误作 Profile 专用测试删除。
- Scenario 继续记录真实 APK ABI，但 Profile 约束只按 package/version/hash；删去对
  Profile ABI 的工具依赖，fixture 改为 v4。
- 无新增依赖；不实现新音频/文件系统/入口能力，不做 title gate。
- 状态：完成字段收缩；相关标准能力的既有测试失败保持单独列账。

## 验收

- 构建：`cmake --build --preset dev --target ogplay_tests ogplay-cli ogplay_app_tests ogplay -j 8`，通过；只构建受影响目标。
- session/audio/VFS/guest call/application startup 定向集合：218 项中 212 通过；6 项
  Android 平台旧断言失败，共 50 条断言。使用 HEAD 原始源码重新构建并仅复跑这 6 项，
  同样 6 项/50 条失败；v4 源码随后恢复。基线日志：
  `.local/profile-baseline-{build,tests}.log`，最终日志：`.local/profile-v4-targeted-tests.log`。
- 旧失败：catalog 数量/直接绑定、ContextWrapper flags、Context.getString 异常、
  PackageManager flags、Configuration screenLayout、dynamic SurfaceView 销毁回调计数。
  这些标准 Android 断言不在本次字段删除范围内，未顺带修复。
- frontend GUI/CLI/library：66 项通过；TypeScript 检查和 GUI import/library 的 10 项
  Vitest 通过。GUI 制品已重建，manifest 检查通过。
- profile/quirk/scenario 校验与 runner 自检、GUI manifest 共 9 项 CTest 通过；
  `python3 tools/capabilities.py check` 通过，`git diff --check` 通过。
- 无 Profile 启动：
  `build/dev/ogplay-cli run-apk .local/games/com.rovio.angrybirds_2.3.0.apk --profiles-dir data/profiles --ephemeral-sandbox --exit-after-frames 120`。
  启动日志明确 `profile=none api_level=19 surface_width=800 surface_height=480`、
  `backend=switch source=default`，呈现 120 帧、退出码 0。
  日志 `.local/profile-v4-no-profile-launch.log`；此为有界启动证据，不是游戏兼容验收。

## 删除与剩余缺口

- 删除 identity.name/ABI 守卫、全部 runtime（API/lifecycle/入口/表面/VM/预算/静态预置）、
  data（mounts/cwd/manifest）、audio（封面音乐/SoundPool 资源映射）；删除仅供旧路径
  使用的根库 adapter、资产 bundle、MusicPlayer 接口、专用测试与 CMake 项。
- 三项前置条件均通过，无因前置核实失败而保留的字段。
- VM/guest call 一律用通用默认值，用户显式解释器选择保留；前端设置中的旧 `profile`
  选项改为 `default`，不保留旧选项迁移。TOML 版本数组继续叫 `version_code`，对应 C++
  `version_codes`；可选约束语义延续通用启动定位。
- 剩余缺口：上述 6 项既有测试失败；Windows/Linux 未构建实跑；未做完整 title gate、
  所有资源格式或真实游戏音频听测。
