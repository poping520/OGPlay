# 模块：gles

## 职责

提供 guest EGL/GLES 边界库、IDL 代码生成和 ANGLE 接入，维护可查询的 GPU 指标。

## 公共 API

M4 定义 EGL 上下文、资源搬运、present、trace 和快照接口。

## 不变量

- GLES 语义与能力来自 ANGLE，边界函数由 IDL 生成。
- guest 内存参数先验证再搬运；GL 状态可供 Agent 查询。

## 禁止

- 不手写 per-function GLES→桌面 GL 转译。
- 不把 `glFlush` 当 present，不伪造扩展或静默 no-op。

## 测试

`tests/gles/` 契约测试及软件后端黄金帧。

