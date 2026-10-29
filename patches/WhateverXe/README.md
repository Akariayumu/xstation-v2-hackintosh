# WhateverXe 补丁（候选 11）

`EFI/OC/Kexts/WhateverXe.kext` 是在上游 WhateverXe **0.5.0**（tag `0.5.0`，commit `c6cd8ec`）上打 `candidate-11.patch` 后编译的。

| 启动参数 | 作用 | 默认 |
|---|---|---|
| `-wex-tgl-sdt-guard` | 显示时序保护：Apple 驱动算出的 eDP 时序无效（会导致除零 panic）时，用本次已校验的输入补齐；并在 PG2/PG3 电源井上电时轮询等待 | 关，需显式打开 |
| `-wex-no-panel-keep` | 关屏时不强制面板保持供电 | 关 |
| `-wex-sink-rate-restore` | 开机第一次快照记下面板的 `LINK_RATE_SET`（DPCD `0x115`），之后每次链路训练前如果不同就写回 | 关 |

`-wex-no-panel-keep -wex-sink-rate-restore` 一起用解决“关屏后面板断电，再点亮时链路训练失败、黑屏”：
面板断电后会丢掉 BIOS 设置的 `0x115=06`（5.4 Gbps），源端按 8.1 Gbps 训练就失败。

补丁里还有候选 8–10 的实验开关（`-wex-sink-d0`、`-wex-sink-oui`、`-wex-sink-rate-select`）和诊断开关 `-wex-display-trace`，v0.1.55 都没有打开。
每次训练前会做一次只读的 DPCD 快照（约 89 字节 AUX 读）。

## 构建

```bash
git clone https://github.com/b00t0x/WhateverXe && cd WhateverXe
git checkout 0.5.0
git apply /path/to/candidate-11.patch
# 按上游 README 的说明在 macOS 上构建；测试：python3 tools/test.py
```

候选 11 在 macOS 上构建，上游测试 135 个全部通过。

## 许可证

WhateverXe 源自 WhateverGreen，使用 BSD 3-Clause（见 `LICENSE-WhateverXe.txt`，© vit9696）。
本补丁同样以 BSD 3-Clause 发布，原文件中的版权声明均保留。
