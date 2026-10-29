# 第三方组件

版本对应 EFI v0.1.55（2026-10-29 实机验证）。许可证以各上游仓库为准，下表为 2026-10-29 查询结果。
每个发布包的下载地址和 SHA-256 见 [thirdparty.lock](thirdparty.lock)。

## 由脚本下载（本仓库不包含）

运行 `scripts/fetch-thirdparty.sh` 获取。

| 组件 | 版本 | 上游 | 许可证 |
|---|---|---|---|
| OpenCore（OpenCore.efi、BOOTx64.efi、OpenRuntime.efi、ResetNvramEntry.efi） | 1.0.8 DEBUG | [acidanthera/OpenCorePkg](https://github.com/acidanthera/OpenCorePkg) | BSD-3-Clause |
| HfsPlus.efi | OcBinaryData `c2a98980d3` | [acidanthera/OcBinaryData](https://github.com/acidanthera/OcBinaryData) | 未声明（Apple 来源） |
| Lilu | 1.7.2 | [acidanthera/Lilu](https://github.com/acidanthera/Lilu) | BSD-3-Clause |
| VirtualSMC、SMCBatteryManager | 1.3.8 | [acidanthera/VirtualSMC](https://github.com/acidanthera/VirtualSMC) | BSD-3-Clause |
| AppleALC（主程序） | 1.9.8 | [acidanthera/AppleALC](https://github.com/acidanthera/AppleALC) | BSD-3-Clause |
| NVMeFix | 1.1.3 | [acidanthera/NVMeFix](https://github.com/acidanthera/NVMeFix) | BSD-3-Clause |
| BlueToolFixup | 2.7.2 | [acidanthera/BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) | GPL-2.0 |
| VoodooPS2Controller（含 VoodooPS2Keyboard） | 2.3.7 | [acidanthera/VoodooPS2](https://github.com/acidanthera/VoodooPS2) | APSL-2.0 |
| VoodooI2C（主程序、VoodooI2CServices、VoodooInput） | 2.9.1 | [VoodooI2C/VoodooI2C](https://github.com/VoodooI2C/VoodooI2C) | GPL-3.0 |
| VoodooI2CHID | 2.9.1（随 VoodooI2C 发布包） | [VoodooI2C/VoodooI2CHID](https://github.com/VoodooI2C/VoodooI2CHID) | 仓库未声明 |
| ECEnabler | 1.0.6 | [1Revenger1/ECEnabler](https://github.com/1Revenger1/ECEnabler) | 未声明 |
| USBToolBox | 1.2.0 | [USBToolBox/kext](https://github.com/USBToolBox/kext) | 未声明 |
| AirportItlwm（Sequoia） | 2.4.0-alpha（commit `738d4c6`） | [laobamac/itlwm](https://github.com/laobamac/itlwm)（fork 自 [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm)） | GPL-2.0 |
| IntelBluetoothFirmware、IntelBTPatcher | 2.4.0 | [OpenIntelWireless/IntelBluetoothFirmware](https://github.com/OpenIntelWireless/IntelBluetoothFirmware) | GPL-3.0 |
| AMFIPass | 1.4.1 Mod | WhateverXe Graphics 的 [`extras/`](https://github.com/b00t0x/WhateverXe-Graphics/tree/444b348e7ee757ded480ff46d781d5efb0f5fb38/extras)（commit `444b348`），原版来自 [Dortania](https://github.com/dortania/OpenCore-Legacy-Patcher) | 闭源（© Dhinak G，保留所有权利） |

## 随本仓库提供的第三方派生文件

| 文件 | 来源 | 许可证 | 说明 |
|---|---|---|---|
| `EFI/OC/Kexts/WhateverXe.kext` | [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 0.5.0（commit `c6cd8ec`）+ `patches/WhateverXe/candidate-11.patch` | BSD-3-Clause（源自 WhateverGreen，© vit9696） | 补丁、构建方法和上游许可证全文在 `patches/WhateverXe/` |
| `EFI/OC/Kexts/VoodooI2C.kext/Contents/PlugIns/VoodooGPIO.kext` | VoodooI2C 上游 CI 构建（VoodooI2C commit [`6be653a`](https://github.com/VoodooI2C/VoodooI2C/commit/6be653a2d2234ac1f7464ebf12716314975c3920)，VoodooGPIO 子模块 commit [`d415406`](https://github.com/VoodooI2C/VoodooGPIO/commit/d415406ecf25b5e322d3db9a4abab5719106fac1)），未修改 | GPL-3.0，全文见 `licenses/VoodooGPIO-GPL-3.0.txt` | 2.9.1 正式版的 VoodooGPIO 不认 Tiger Lake LP（`INT34C5`），这个提交加入了 `VoodooGPIOTigerLakeLP`。对应源码即上述公开提交 |
| `EFI/OC/Kexts/VoodooI2C.kext/Contents/Info.plist` | VoodooI2C 2.9.1 | GPL-3.0 | 唯一改动：`VoodooI2CPCILakeController` 的 `IOPCIMatch` 加入 `0xa0e88086&0xFFFCFFFF`（本机 I²C 控制器） |
| `EFI/OC/Kexts/AppleALC.kext/Contents/Info.plist` | AppleALC 1.9.8 | BSD-3-Clause | 唯一改动：ALC256 layout 19 的 `ConfigData` 和 `WakeConfigData` 末尾追加 `02050010 02040020`（coef `0x10` 写 `0x0020`，清除扬声器静音位） |

## 系统内安装的组件（不在 EFI 里，本仓库不分发）

| 组件 | 上游 | 许可证 |
|---|---|---|
| WhateverXe Graphics（Apple 图形驱动、编译器组件，经修改） | [b00t0x/WhateverXe-Graphics](https://github.com/b00t0x/WhateverXe-Graphics) | 打包工具 BSD-2-Clause；**驱动本体为 Apple / Intel 二进制，不授予再分发权利** |
| AppleIntelTGLCompat | 源码在 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe/tree/main/AppleIntelTGLCompat)，二进制随 WhateverXe Graphics 发布 | BSD-2-Clause |

## 本仓库自己的部分（BSD-3-Clause）

- `EFI/OC/ACPI/`、`acpi/` 中的 SSDT：PWRBEC、HIDDSTA、PNLF、PLUG、EC、RTCAWAC
- `EFI/OC/Kexts/UTBMap.kext`（只有 Info.plist 的 USB 映射）
- `EFI/OC/Kexts/XSHIDDLog.kext` 及其源码 `src/XSHIDDLog/`
- `patches/WhateverXe/` 中的补丁（原文件的版权声明保留）
- `scripts/`、`tools/`、`docs/`、`CHANGELOG.md`
