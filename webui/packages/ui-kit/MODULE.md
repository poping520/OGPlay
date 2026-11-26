# 模块：Web UI 主题

启动器（游戏库、对话框、设置页）共用的“磨砂黑”样式基座：`:root` 设计令牌、元素基线（按钮默认无装饰）
与共享控件 `.pill`、`.seg`、`.key`、`.led`、`.kicker`、`.brand`、`.back`/`.crumb`（返回游戏库与面包屑）。由 `apps/gui/kit.tsx` 最先加载，页面样式在其后
只增加布局与上下文尺寸，需要不同取值时在自身作用域覆盖令牌。
`skin-glass` 段（“玻璃紫”）提供皮肤令牌、共享控件与对话框的玻璃外观及 `.glass-sky` 背景；视图根带 `skin-glass` 即生效，
规则以 `:root` 提一级权重，压过视图自身的磨砂黑默认值。各视图样式只补本视图特有的玻璃差异。
组件 `glyph.tsx`（线性图标 `Glyph`，24 栅格圆头描边，禁用字体符号作图标）与 `key.tsx`（手柄键位徽标 `Key`）只依赖 Preact，
不带应用资源或 RPC，任何应用可直接导入；依赖 logo、多语言的片段留在应用内（如 gui 的 `kit.tsx`）。
`i18n.ts` 提供共享 language/setLanguage/useLanguage/t/tj/has、语言标签（与宿主 ui_text.h 同组）与消息目录注册：
代码以语义 ID（`area.subject.name`）取消息，消息为 ICU MessageFormat；缺失时依次回退英文（zh-TW 回退简体）、
zh-CN、ID 本身。`icu.ts` 实现所用子集（`{arg}`、plural/select、`#`、`offset:`、撇号转义），复数类别取
`Intl.PluralRules`，结果按段返回，参数可为 JSX；不引入外部依赖；
`theme.css` 按 html lang 选 CJK 字体（--cjk），日文、繁体不用简体字形；各应用 bundle 只注册自己的表，不依赖 GUI。
`i18n-test-utils.ts` 只用于测试，提供 TS 与 C++ 字面量扫描（C++ 扫描区分注释、字符、普通与 raw string）
及 `catalogProblems` 目录一致性检查。
不访问 RPC、文件或运行时，不决定业务状态。应用通过类名和原生 disabled/ARIA 状态驱动样式。
按键设置窗口（apps/controls）在自身样式前直接导入本主题，只用磨砂黑（由游戏进程提供，不读启动器的界面风格）；
Dashboard 仍使用自己的样式文件，尚未接入本主题。
