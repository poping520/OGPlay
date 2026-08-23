# BND-13 · OpenSL ES 最终验收闭环

## 目标

完成 OpenSL ES Virtual SO 的全量回归，修正 ABI 扩展与模块拆分留下的验收元数据，并在
证据完整后晋升 capability。

## 交付与验收

- [x] A32 call-frame 上限测试使用 `kMaximumA32CallArguments + 1` 验证越界，不再隐含旧的
  9-word 上限；OpenSL 11-word constructor ABI 保持受检。
- [x] `gles1_material_front_face` quirk 注册表指向模块化后的真实测试源，registry loader 与
  validator 恢复一致。
- [x] OpenSL focused CTest 19/19 已在 BND-12 通过。
- [x] 最终 full CTest 923/923 通过，包含 capability monotonic、documentation layout 与
  boundary hot-path architecture gate。
- [x] `runtime.opensles_virtual_so` 从 `partial` 晋升为 `complete`。

## 验证

```text
cmake --build --preset dev -j 8
ctest --preset dev --output-on-failure
100% tests passed, 0 tests failed out of 923
```
