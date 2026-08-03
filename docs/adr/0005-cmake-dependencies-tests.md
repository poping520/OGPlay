# ADR-0005 · CMake、固定依赖与 doctest

- 状态：Accepted
- 日期：2026-08-03

## 背景

三平台需要一致构建和可机器判定的验证。DEMO 自制测试框架不利于发现、筛选和 CI 报告。

## 决定

使用 CMake 3.25+、C++20 与 Ninja 预设；依赖固定版本。M0 使用 doctest + CTest，后续通过
包管理器接入大型依赖。构建不得隐式依赖游戏文件。

## 后果

首次配置可能需要获取 doctest；离线构建可提供 `OGPLAY_DOCTEST_SOURCE_DIR` 指向预置源码。

