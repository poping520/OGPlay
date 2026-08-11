# WU-PERF-03 · GuestTransfer validation dedup

目标：消除 GuestTransfer 的重复 Validate，同时保留 ANGLE 调用前 output preflight。

验收：

- [x] input/PrepareGuestInput 由一次 Read 完成 validate + copy。
- [x] output/inout Prepare 取得包含映射世代的 write preflight 票据。
- [x] 正常 Commit 直接消费票据写入，不重复 Validate + Write validation。
- [x] preflight 后发生 Map/Protect/Unmap/Restore 时 Commit 重新验证并精确失败。
- [x] 测试覆盖权限撤销后的 address/access/reason/thread fault 与未提交状态。
- [x] PERF 阶段 `windows-msvc` 构建与 full CTest 506/506；Asphalt 6 exact 未退回
  mixed GLES/transfer 故障。
