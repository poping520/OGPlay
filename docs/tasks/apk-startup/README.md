# APK Startup WU 任务书归档

本目录归档 `docs/design/apk-startup/` 的实施 Work Unit。格式参考
`docs/tasks/dexvm/DVM-*.md`：任务书短、可单会话执行、验收机器可判定，完成后原地保留
作为实现历史。

## 命名规则

```text
APS-<顺序号>.md
```

例如：

```text
APS-1.md
APS-2.md
APS-9.md
APS-10.md
```

规则：

- `APS` = APK Startup；
- 顺序号从 1 单调递增，不补零；
- 编号一旦使用不得复用；
- 不使用 `APS-3a` / `APS-3-fix`；若任务需要拆分，新增下一个编号并在依赖中引用；
- 所有该设计的实现 WU 永久放在 `docs/tasks/apk-startup/`，不散落到临时目录。

## 任务书最小结构

```markdown
# APS-N · 标题

## 目标（一句话）

...

## 依赖

...

## 设计锚点

...

## 语义出处

...（涉及 Android/Dalvik 语义的 WU 必填本地 AOSP 文件 + 函数）

## 变更

...

## 验收（机器可判定）

...
```

任务完成后不删除“验收”，而是在同文件补充：

```markdown
## 结果（机器可判定，已达成）
```

记录实际测试结果、关键实现偏差和本地证据摘要。

## WU 大小

遵守项目既有 AI 工作流：一个 WU 应能在单会话完成。预估触及超过 10 个 code/test
文件、或同时跨 loader + runtime + DexVM + profile 四层时，先拆成新的 APS 编号。

## AOSP 本地参考

固定根：

```text
.local/asop/dalvik
.local/asop/framework/base
.local/asop/libcore
```

语义敏感 WU 在编码前把“文件 + 函数 + 采用结论”写进自己的任务书。参考源码不入库。

## 当前任务序列

| WU | 内容 |
| --- | --- |
| [APS-1](APS-1.md) | Manifest Application / launcher startup facts |
| [APS-2](APS-2.md) | APK native inventory + process ABI resolver |
| [APS-3](APS-3.md) | rootless Android guest process shell |
| [APS-4](APS-4.md) | dynamic NativeLibraryLoader + JNI_OnLoad |
| [APS-5](APS-5.md) | DexVM System.load/System.loadLibrary + reentry |
| [APS-6](APS-6.md) | minimal Application startup |
| [APS-7](APS-7.md) | AndroidAppProcess + launcher Activity + frontend cutover |
| [APS-8](APS-8.md) | optional Profile v3 + legacy adapter |
| [APS-9](APS-9.md) | integration gates + design migration closeout |
