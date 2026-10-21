# LUI-18 · Guest 触摸分发与窗口属性

## 目标及依赖

新能力；依赖 LUI-9/16/17、每实例 Window 内容树与 API19 intrinsic 字段。
把 guest Decor→Activity 虚派→Window 内容树→Activity fallback 接入共享分发器；
宿主/session 不保留第二套 hit-test/capture/cancel 实现。

## 支持边界

- DOWN 触发 onUserInteraction；View enabled listener 优先，随后 onTouchEvent；
  ViewGroup intercept、reverse-Z 子树与 CANCEL；捕获目标按 DOWN 固定，多指不 split。
- Window 独立属性对象；公开 API19 LayoutParams 实例字段真实存取，setAttributes
  按 copyFrom 复制，已赋 token/packageName 保留。亮度等仅保存，不改变宿主屏幕。
- 其他 action 明确记账失败；不扩展完整 Decor/系统窗口服务或标题专属规则。

## 验证

定向 widget_click/hierarchy/window_dialog；受影响目标 ogplay_tests、ogplay-cli。
原始证据 `.local/analysis/wu4/tml_click.json`；同路径点击 survey 单次 reached-fault，
新增缺口只记录。最终结果见下方。

## 结果

- 完成：分发核心及 capture 由 integration context 唯一持有，session 删除旧实现及独立捕获字段。
  Decor 虚派 Activity 覆写；基类 DOWN 通知、Window 内容树、Activity fallback，View listener
  顺序、ViewGroup intercept/CANCEL 与非 split 多指均有主题回归。窗口属性保持实例身份，
  覆盖默认值、复制而非别名、亮度读回及 token/packageName 的特殊复制规则。
- 受影响目标：`cmake --build --preset release --target ogplay_tests ogplay-cli -j 6` 通过。
  工作区并行改动的 `bootdex_library_tests.cpp` 双参数 CAPTURE 使首次构建失败，仅拆开宏调用。
- `build/release/ogplay_tests --source-file='*widget_click_tests.cpp,*widget_hierarchy_tests.cpp,*widget_window_dialog_tests.cpp'`：
  33 用例、2608 断言通过；两种解释器后端覆盖 guest decor/Activity 和窗口属性。
  日志 `.local/analysis/wu4/lui18-{build,tests-build,tests}.log`。
- 复现命令：`python3 .local/analysis/wu4/mcp_survey.py '<library>/com.amazon.games.tilmorningslight/game.apk' .local/analysis/wu4/tml_click_lui18 '[[400,240],[400,240],[400,240]]' 12 --ephemeral-sandbox --exit-after-frames 2400`。
  MCP 接受 requestSequence 1/2/3，frameSequence 3/3/143；JSON summary：missing_classes=0、
  missing_members=0、stub_hits=0，命中清单 `[]`，原三项全部未再命中。证据
  `.local/analysis/wu4/tml_click_lui18.{json,log}` 与 `tml_click_lui18-driver.log`。
- 无新运行期首错；退出 host_shutdown 时已有的 onPause→FMODAudioDevice.stop/blockStreaming
  抛 `IllegalThreadStateException: the VM is shutting down`，退出码 1，未扩修。
  原日志已有的 `getSystemService("display")` UnsupportedOperationException 警告仍在，
  不属于新增 survey 命中，未扩修；此次仅 reached-fault 证据，非 title gate。
- Unity 源码核对：`j.a()` 读取 ForwardNativeEventsToDalvik，`UnityPlayer.c()` 设置 `l` 并调用
  nativeForwardEventsToDalvik；`onTouchEvent→a(MotionEvent)` 在 `l=true` 时返回 false，
  不进入排队的 nativeInjectEvent，OGPlay 无人为反向转发，因此该路径不会无限递归。
- `python3 tools/capabilities.py check`：414 条目通过；`git diff --check` 通过。
