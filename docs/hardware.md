# 硬件基线（X-STATION V2）

数据于 2026-10-22 在 Linux Live 下采集。“证据”一栏是作者本地采集目录里的文件名，不在本仓库中。

## 1. 主机

| 项目 | 值 | 证据 |
|---|---|---|
| 厂商/型号 | X-STATION / V2 | `system/dmi.txt` |
| 主板 | X-STATION `EM_E134_M_V1.0` | `system/dmi.txt` |
| BIOS | AMI `TGL_E134_V4.1.12`（2021-12-15） | `system/dmi.txt` |
| CPU | Intel Core i7-1165G7，4C/8T，家族 6 / 型号 140 / stepping 1，microcode `0xbe` | `system/cpu.txt` |
| 内存 | 15 GiB 可用（约 16 GB，板载） | `system/memory.txt` |
| Secure Boot | **Disabled**，平台处于 **Setup Mode** | `system/secureboot.txt` |
| 启动模式 | UEFI（采集自 Linux） | `system/uefi-boot-entries.txt` |

## 2. 显示

| 项目 | 值 | 证据 |
|---|---|---|
| iGPU | Iris Xe `8086:9A49`（TigerLake-LP GT2），PCI `0000:00:02.0` | `pci/lspci-nnk.txt` |
| 内屏 | Sharp `SHP14A8` / `LQ134R1` | `display/card1-eDP-1-edid.bin` |
| 分辨率 | 3840×2400（EDID 与 DRM modes 一致） | `display/card1-eDP-1-modes.txt` |
| 接口 | eDP，DRM 名 `card1-eDP-1`，`connected` + `enabled` | `display/card1-eDP-1-status.txt` |
| VBT 路由 | **LFP1 / handle 0x0008 → DP-A，AUX-A，最大 4 lane** | `work/reports/vbt-decoded.txt` |
| 背光 | VBT 含 PWM/Display Engine 参数；**DSDT 无 PNLF 设备**（见 ACPI 审计） | `docs/acpi-audit.md` |

## 3. 输入 / 音频 / 网络 / 存储

| 项目 | 值 | 证据 |
|---|---|---|
| 触摸/笔 | Wacom `WCOM4815`（I²C HID，HID ID `056A:48EE`），ACPI `\_SB.PC00.I2C1.TSC1`，I²C 地址 `0x0A`，GPIO 中断由 `_INI` 写为 INT1；GPIO 芯片 INT34C5 | `input/proc-input-devices.txt`、`work/acpi/dsdt.dsl:61682` |
| 音频 | Realtek ALC256（HDA Intel PCH，`10ec:0256` / `8086:a0c8`） | `audio/cards.txt`、`audio/card0-codec#0.txt` |
| 无线 | Intel Wi-Fi 6 AX201 `8086:A0F0` | `pci/lspci-nnk.txt` |
| 蓝牙 | Intel 组合模块，控制器 MAC 略 | `network/bluetooth.txt` |
| 有线网 | USB 以太网 WCH `1a86:5394`，CDC ECM | `usb/usb-devices.txt`、`network/nmcli.txt` |
| SSD | **Kingston `OM8PDP31024B-A01`** 953.9 GB NVMe | `storage/lsblk.txt` |

> **2026-10-25 更正**：旧版本把触控写成“USB HID”、把有线网写成“ASIX”；实际触控是 `i2c-WCOM4815`（I²C HID），旧有线网是 WCH `1a86:5394`（CDC ECM）。本表已按原始 USB/input 采集更正。

> **与路线图不一致的地方**：路线图 §1 写的是 `OM3PDP3`，实机 `lsblk` 为
> `OM8PDP31024B-A01`。本工作区以实测为准；这不影响 EFI 构建，但影响“确认目标磁盘身份”的判据。

## 4. BIOS 设置

- DVMT Pre-Allocated：出厂 60 MB，适配时改为 **96 MB**（WhateverXe 推荐 96 MB 以上，见 CHANGELOG v0.1.19）。
- CFG Lock：开启，BIOS 里没有改。OpenCore 用 `AppleXcpmCfgLock` 处理。
- Secure Boot：关闭。
- VT-d、Above 4G Decoding：开启。BIOS 没有 CSM 选项。
