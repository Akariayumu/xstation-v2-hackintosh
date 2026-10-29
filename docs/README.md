# 文档

| 文件 | 内容 |
|---|---|
| [hardware.md](hardware.md) | 硬件清单（Linux 下采集的 PCI/ACPI/USB 身份） |
| [research/power-button-20261026.md](research/power-button-20261026.md) | 电源键调查：Linux 下走 EC Query `0x54`，macOS 下为什么没有事件 |
| [research/audio-speaker-20261025.md](research/audio-speaker-20261025.md) | ALC256 扬声器无声调查，layout 选择与 `alcapplid` |
| [research/pen-20261028.md](research/pen-20261028.md) | Wacom AES 手写笔：报告描述符、VoodooI2C 2.9.1 适配方法 |
| [research/wex-clean-rewrite-20261027.md](research/wex-clean-rewrite-20261027.md) | 在上游 WhateverXe 0.5.0 上重写最小补丁的过程 |

`research/` 下是**当时的调查记录**，保留原样，文中的“当前状态”“下一步”指写作当天。
最终结论以仓库根目录的 [CHANGELOG.md](../CHANGELOG.md) 为准，例如电源键最终由 v0.1.50 的 `SSDT-PWRBEC` 解决，
扬声器由 v0.1.44 的 coef `0x10` 写入解决。文中提到的 `work/`、`private/` 等路径是作者本地工作目录，不在本仓库中。
