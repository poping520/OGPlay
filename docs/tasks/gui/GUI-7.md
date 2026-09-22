# GUI-7 · 机型预设数据与校验

状态：数据阶段完成。依据 [GUI v2 规划](../../design/gui/README.md)，运行时能力另立任务，Linux 暂缓。

## 交付与边界

- [data/devices](../../../data/devices/README.md)：schema 1、三个明确标注的通用虚拟预设。
  完整覆盖品牌/厂商/型号/设备/产品、CPU、GPU、屏幕/密度、RAM、语言/地区/时区；API 19/ARMv7 固定。
- [校验脚本](../../../tools/validate_devices.py)：标准库 TOML、严格键集合、字段类型/范围、
  UTF-8 文本预算、文件名/ID、16 KiB 单文件和 128 项目录预算；缺目录、空目录或坏数据失败。
- CTest `tools.device_presets_current` / `tools.device_presets_validator` 接入现有工具测试。
- 保持全局/每实例 `device_preset` 的预留状态；不增加 RPC、选择器或 BuildLaunchPlan 参数，
  不修改 Profile、guest Build/ro.*、ANGLE、Clock、线程模型或沙盒 ANDROID_ID。

## 验证

- 三份当前预设校验通过。
- 7 项 Python 定向测试通过：各层缺字段/未知键/类型、数值上下界与布尔混淆、UTF-8 字节边界、
  错误身份/版本/ABI/locale、重复 TOML 键、坏编码、文件/目录预算及 CLI 非零退出。
- Windows `windows-msvc` 配置成功，两项设备预设 CTest 通过。
- UTF-8、文档链接、TOML 及 `git diff --check` 静态检查通过。未修改 C++/WebUI，不构建二进制或运行游戏。

后续若接入预设选择，应先明确配置继承和未知 ID 行为；运行时消费另立能力任务，不能因数据校验通过就宣称机型模拟完成。
