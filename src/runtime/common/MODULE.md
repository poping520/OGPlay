# 子模块：runtime/common

## 职责

保存多个 runtime 子模块共同消费、没有上层所有权的稳定 POD/枚举契约。

## 不变量

- 只依赖标准库和更低层公共类型；不得包含 session、integration、JNI、syscall 或 boundary
  实现。
- `SupervisorCallProgress` 只表达可观测进展分类，不决定 watchdog 预算或 teardown 策略。

## 禁止

- 不把便利 helper、业务状态或可变全局状态堆入 common。
