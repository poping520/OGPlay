# BND-2 · Boundary Fast Host Call

## 目标

增加 CPU 通用 HostCallHook，并把 `SVC #2` 接到 sealed dense hot table，使成功的
Virtual SO 调用不退出 `Cpu::Run()`。

## 交付与验收

- [x] 多页 sealed thunk arena 与 live-register A32 call frame。
- [x] Dynarmic handled/unhandled/fault 契约。
- [x] PC → slot → `{fn,self}` 常数时间调用。
- [x] observer 强制 slow fallback，nested/clone CPU 安装同一 hook。
- [x] fast/slow 结果等价测试；既有 decode benchmark 不设绝对阈值。
- [x] hot slot 直接保存 export-specific template handler 与 concrete module instance；
  成功路径不再经 `FastBinding`、descriptor、SONAME 或 local-id route。

历史说明：首轮保留的 legacy handler bridge 已在本次 BND-2/BND-4 闭环中删除。
