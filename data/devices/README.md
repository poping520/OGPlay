# 设备预设 · schema 1

本目录是随运行时打包的机型模板。每个文件模拟一台 Android 4.4.4（API 19）手机，
游戏通过 `android.os.Build`、`SystemProperties` 和 native `__system_property_get` 读到同一份身份。
GUI 的 `device_preset` 设置与 CLI `run-apk --device-preset <id>` 选择模板，默认 `gt-i9300`。

当前运行时只消费 `[product]` 与 `[build]`；`[cpu]`、`[gpu]`、`[display]`、`[memory]` 已校验，
留给后续 CPU/GPU 身份与推荐分辨率使用，暂不改变 cpuinfo、GL 字符串、屏幕或内存。

## 校验

在仓库根运行（Python 3.11+，只用标准库）：

```powershell
python tools/validate_devices.py
python tools/validate_devices.py --devices data/devices
```

可执行 schema 的唯一入口为 [validate_devices.py](../../tools/validate_devices.py) 的 `SCHEMA`；
运行时加载器（`session::LoadDevicePreset`）执行相同规则。未知键、缺字段、错误类型和重复 TOML 键全部拒绝，
没有隐式默认或类型转换。

## 文件与身份

- UTF-8 TOML，每文件不超过 16 KiB；目录包含 1..128 个预设，只枚举直属 `.toml` 文件，符号链接拒绝。
- 文件名严格为 `<id>.toml`，内置模板以机型型号的小写作 ID（如 `gt-i9505`），同代不同版本各占一个文件；ID 以小写字母开头，由小写字母/数字及单个连字符组成，最多 64 bytes。
- 所有字段必填。普通文本非空、无首尾空白与控制字符，长度以 UTF-8 bytes 计。
- “属性文本”会成为系统属性值：1..91 bytes 可打印 ASCII（`PROP_VALUE_MAX` 减终止符），无首尾空格。

| 表 | 字段 | 系统属性 / 约束 |
| --- | --- | --- |
| 根 | `schema` | 固定整数 1 |
| 根 | `id`, `label`, `description` | ID 64、显示名 128、说明 512 bytes |
| `product` | `manufacturer`, `brand`, `name`, `model`, `device`, `board` | `ro.product.<字段>`；属性文本 |
| `product` | `cpu_abi` | `ro.product.cpu.abi`；固定 `"armeabi-v7a"` |
| `build` | `product` | `ro.build.product`；属性文本 |
| `build` | `fingerprint` | `ro.build.fingerprint`；必须为 `<brand>/<name>/<device>:4.4.4/<id>/<incremental>:user/release-keys`，后两段为 `[A-Za-z0-9._-]+` |
| `build` | `version_release`, `version_sdk` | `ro.build.version.release/sdk`；固定 `"4.4.4"`、`19` |
| `cpu` | `name`, `cores` | 名称最多 128 bytes；核数 1..32（尚未消费） |
| `gpu` | `vendor`, `renderer` | 各最多 128 bytes（尚未消费） |
| `display` | `width`, `height`, `density_dpi` | 竖屏逻辑像素 240..4096；密度 72..960（尚未消费） |
| `memory` | `ram_mb` | 128..8192 MiB（尚未消费） |

预设不包含 ANDROID_ID、序列号、路径或游戏包名；ANDROID_ID 由每实例沙盒管理。

## 内置模板

| ID | 机型 | SoC / GPU | 屏幕 / 密度 | RAM / 核数 |
| --- | --- | --- | --- | --- |
| `gt-i9100` | Galaxy S2 | Exynos 4210 / Mali-400 MP | 480×800 / 240 | 1024 MiB / 2 |
| `gt-i9300` | Galaxy S3（默认） | Exynos 4412 / Mali-400 MP | 720×1280 / 320 | 1024 MiB / 4 |
| `gt-i9500` | Galaxy S4 | Exynos 5410 / PowerVR SGX 544MP | 1080×1920 / 480 | 2048 MiB / 4 |
| `gt-i9505` | Galaxy S4 | Snapdragon 600 / Adreno 320 | 1080×1920 / 480 | 2048 MiB / 4 |
| `sm-g900f` | Galaxy S5 | Snapdragon 801 / Adreno 330 | 1080×1920 / 480 | 2048 MiB / 4 |

`ro.product.*` 与 `ro.build.product` 取自各机型国际版的真实值。运行时固定为 4.4.4，而这些机型都没有
官方 4.4.4 固件，因此 fingerprint 按真实格式合成：版本段统一为 4.4.4/KTU84Q（本仓库 ROM 的
build id），incremental 采用该机型的固件命名风格，不对应真实发布的固件。

新增模板：复制现有文件，改 ID/文件名并填写完整数据后运行校验；GUI 选项列表
（`src/frontend/library/settings.cpp` 与 `webui/apps/gui/settings/settings.ts` 的显示名）需同步加入新 ID。
