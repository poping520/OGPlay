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

本 WU 保留 legacy handler 作为 hot entry 的业务实现，中央 route 的删除属于 BND-4。
