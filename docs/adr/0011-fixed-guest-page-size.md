# ADR-0011：固定 4 KiB guest 页并分离宿主后备粒度

- 状态：接受
- 日期：2026-08-04

## 背景

Android ARMv7 ABI 与本项目支持的 API 19/22/23 Bionic 以 4 KiB 页面为基础。此前
`AddressSpace` 直接把宿主页尺寸暴露为 guest 页尺寸，在 Apple Silicon 的 16 KiB
宿主页上会使 ELF 段、kernel helper、`clone` 栈与 `madvise` 范围产生错误的对齐和权限
语义；同一宿主页中的多个 4 KiB guest 页也无法通过宿主 `mprotect` 独立控制。

## 决定

`AddressSpace` 的 guest 页尺寸固定为 4096 字节。映射存在性与 read/write/execute 权限
都按 guest 页独立记账；宿主 reservation 按实际宿主页尺寸提交可读写、不可执行的后备
存储，仅在该宿主页覆盖的所有 guest 页均未映射时释放。

CPU 仍只能通过 `CheckedMemoryBus` 访问或取指，由它在读写宿主后备前强制检查 guest
账本。`AddressSpace` 不提供可绕过检查的公共宿主指针，因此 W^X 与 execute-only 等
guest 语义不依赖宿主页面保护粒度。

## 结果

- 4 KiB 对齐的 Android ELF 与 syscall 范围在 Windows、Linux、macOS 上语义一致。
- Apple Silicon 上共享一个 16 KiB 宿主页的四个 guest 页可独立映射、保护和释放。
- 宿主页面保护不再作为 guest 权限的第二份状态，避免两份权限账本失配。
- 若未来引入可直接访问 guest 后备的 JIT fastmem，必须先增加显式的受保护访问契约；
  当前决定不授权绕过 `CheckedMemoryBus`。
