# WU-HLE-01 · O(1) thunk decode

目标：从 HLE execution path 删除 `BionicHleSymbolProvider::Resolve(pc)` 线性扫描，改为
range/alignment/index 算术定位 dense descriptor。

验收：

- [x] descriptor 冻结 library/name、route、function id 与 parameter count。
- [x] decode 同时接受 Thumb bit 地址与 code PC，拒绝越界、错位和未发布 slot。
- [x] logger/debugger 继续可用 `Resolve()`，execution 不再调用它。
- [x] focused perf case 可人工比较 1M 次 Resolve 与 Decode，不设置绝对时间 gate。
- [ ] HLE 阶段统一构建与测试。

非目标：execution routing 的 module/symbol 字符串比较由 WU-HLE-02 删除。
