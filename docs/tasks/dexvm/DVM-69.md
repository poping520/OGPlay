# DVM-69 · Reflection metadata closure

## 目标（一句话）

只解析反射需要的 Dalvik system annotations，闭合 nested/enclosing、MemberClasses
与 declared Throws 元数据，并完成 bounded reflection foundation。

## 依赖

- DVM-62..68 完整 reflection/linker/loader/operation 基础栈
- 本地 API-19 `dalvik.annotation` InnerClass、EnclosingClass、EnclosingMethod、
  MemberClasses、Throws 定义与 libcore `Class.java`

## 交付

- loader 受检解析 annotations_directory、annotation_set 和所需 encoded values，
  只输出 host system metadata，不物化 guest Annotation proxy。
- linker 将 type/method indices 解析为稳定 Class/member identity；Method/Constructor
  的 exception types defensive copy 来自 Throws。
- Class simple/canonical/declaring/enclosing name 与 member/local/anonymous 分类严格读取
  system metadata，不用 `$` split；暴露 enclosing Method/Constructor 和 declared classes。
- dexasm 增加只面向测试夹具的 bounded system-metadata directives。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 覆盖 member/local/anonymous、enclosing class/
  method/constructor、canonical null 规则、MemberClasses、InnerClass modifiers 与 Throws。
- DVM-67..69 全部完成后运行一次全量测试。

状态：完成。
