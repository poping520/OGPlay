# 虚拟设备预设 · schema 1

本目录是 GUI-7 的纯数据目录，不对应真实手机，也不声明游戏兼容性。
当前 GUI 的 `device_preset` 仍为预留字符串；尚未加载本目录，没有 `devices.presets` RPC、
一键带入或运行时传参。配置不会改变 Build/系统属性、CPU/GPU、屏幕、内存或地区事实。
运行时入口及字段映射需独立任务决定，不把预设混入现有 Title Profile schema。

## 校验

在仓库根运行（Python 3.11+，只用标准库）：

```powershell
python tools/validate_devices.py
python tools/validate_devices.py --devices data/devices
```

默认目录相对脚本定位，不依赖当前工作目录。失败输出路径和原因，退出码 1；成功退出码 0。
可执行 schema 唯一入口为 [validate_devices.py](../../tools/validate_devices.py) 的 `SCHEMA`；
未知键、缺字段、额外表、数组、错误类型和重复 TOML 键全部拒绝，没有隐式默认或类型转换。

## 文件与身份

- UTF-8 TOML，每文件不超过 16 KiB；目录须存在且包含 1..128 个预设。
- 只枚举目录直属 `.toml` 文件，不递归。符号链接拒绝，其他扩展名忽略。
- 文件名严格为 `<id>.toml`，ID 全目录唯一。ID 以小写字母开头，由小写字母/数字及单个
  连字符分隔组成，最多 64 UTF-8 bytes；不能用路径、空格、大小写变体或显示名作为 ID。
- 所有下列字段必填。普通文本非空、无首尾空白与 ASCII 控制字符；长度以 UTF-8 bytes 计。
  数字必须是 TOML 整数，布尔/浮点不等同整数；范围均含端点。

| 表 | 字段 | 约束/含义 |
| --- | --- | --- |
| 根 | `schema` | 固定整数 1；未知版本失败 |
| 根 | `id`, `label`, `description` | ID 64、显示名 128、说明 512 bytes |
| `build` | `brand`, `manufacturer`, `model`, `device`, `product` | 每项最多 128 bytes；将来的 guest 身份输入，非宿主探测结果 |
| `build` | `android_release`, `api_level`, `abi` | 固定 `"4.4.4"`、整数 `19`、`"armeabi-v7a"` |
| `cpu` | `name`, `cores` | 名称最多 128 bytes；逻辑核数 1..32，不声明实现了多核调度 |
| `gpu` | `vendor`, `renderer` | 每项最多 128 bytes；拟配置身份，不代表实际 ANGLE 后端或扩展能力 |
| `display` | `width`, `height`, `density_dpi` | 逻辑像素各 240..4096；整数密度 72..960；不含超采样倍率，不推断方向 |
| `memory` | `ram_mb` | 128..8192 MiB；guest 拟报告 RAM，不是 DexVM heap limit 或宿主分配请求 |
| `locale` | `language`, `region` | 两位小写/大写 ASCII 字母（如 en/US）；只检查格式，不承诺翻译或地区行为 |
| `locale` | `timezone` | v1 有限集合 `UTC`、`Asia/Shanghai`；不依赖宿主时区数据库，其他名称明确拒绝 |

预设不包含 ANDROID_ID、路径、安装实例、游戏包名、命令行或 quirk。
ANDROID_ID 继续由每实例沙盒管理。新增支持值先修改 schema 和定向回归，不通过自由扩展表绕过校验。

## 初始数据

| ID | 用途 | 屏幕 / 密度 | RAM / 核数 |
| --- | --- | --- | --- |
| `generic-wvga` | 通用低分辨率示例 | 480×800 / 240 dpi | 512 MiB / 1 |
| `generic-hd` | 通用 HD 示例 | 720×1280 / 320 dpi | 1024 MiB / 2 |
| `generic-tablet` | 通用平板示例 | 800×1280 / 213 dpi | 2048 MiB / 4 |

上述值为项目定义的虚拟配置，不是厂商硬件参数。可从现有文件复制创建预设，先修改 ID/文件名，
再填写完整数据并校验。当前不作为运行时 bundled payload 装配；后续消费接入时再定义加载、
未知 ID 和配置覆盖规则。
