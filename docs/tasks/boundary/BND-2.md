# BND-2 · Boundary Fast Host Call

## 目标

增加 CPU 通用 HostCallHook，并把 `SVC #2` 接到 sealed dense hot table，使成功的
Virtual SO 调用不退出 `Cpu::Run()`。

## 交付与验收

- [ ] 多页 sealed thunk arena 与 live-register A32 call view。
- [ ] Dynarmic handled/unhandled/fault 契约。
- [ ] PC → slot → `{fn,self}` 常数时间调用。
- [ ] observer 强制 slow fallback，nested/clone CPU 继承 hook。
- [ ] fast/slow 等价测试与无绝对阈值 benchmark。
