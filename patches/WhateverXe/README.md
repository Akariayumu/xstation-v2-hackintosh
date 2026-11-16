# WhateverXe 补丁（X-STATION V2）

本目录维护两份针对上游 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 的 X-STATION V2（i7-1165G7 B0 + Sharp 4K eDP `SHP14A8`）补丁：

| 补丁文件 | 目标上游版本 | 用途 |
|---|---|---|
| `wex-0.8.2-xstation-v1.patch` | **0.8.2**（tag `0.8.2` `4512191b` / `main` `2bad7171`） | **当前 `EFI v2.0` 现役版本**（`EFI/OC/Kexts/WhateverXe.kext`）：将本机 4K 时序保护与面板速率恢复无损迁移到 0.8.2，支持真 `DC9` 断电冷唤醒与 S0ix Deep Idle（含 Render `FORCEWAKE` 成对平衡、GuC 休眠与全 TU 内核栈预算适配） |
| `candidate-11.patch` | **0.5.0**（tag `0.5.0`，commit `c6cd8ec`） | 旧版 `v1` 分支（`v0.1.55`）使用，保留作历史对照与回退参考 |

## 启动参数

| 启动参数 | 作用 | 默认 |
|---|---|---|
| `-wex-tgl-sdt-guard` | 显示时序保护：Apple 驱动算出的 eDP 时序无效（全零未写入导致除零 panic）时，用本次已校验的输入补齐；并导出 `TGLPowerFuseLive` 轮询记录 | 关，需显式打开 |
| `-wex-no-panel-keep` | 关屏或进 `DC9` 时不强制面板保持供电（允许 `PP_CONTROL` 正常断电进入深层省电） | 关 |
| `-wex-sink-rate-restore` | 开机第一次快照记下面板的 `LINK_RATE_SET`（DPCD `0x115`），之后每次链路训练前如果不同就写回（最多重试 3 次，每次间隔 1 ms） | 关 |

`-wex-no-panel-keep -wex-sink-rate-restore` 配合 0.8.2 原生的 `DC9` 显示引擎冷恢复状态机，可在**去掉 `-noDC9`** 的前提下稳定完成关屏与 S0ix 睡眠后的面板断电再点亮：面板断电后会丢掉 BIOS 设置的 `0x115=0x06`（5.4 Gbps HBR2），链路训练前由 `-wex-sink-rate-restore` 自动写回。

补丁里还有早期候选 8–10 的实验开关（`-wex-sink-d0`、`-wex-sink-oui`、`-wex-sink-rate-select`）和诊断开关 `-wex-display-trace`，默认均关闭。每次训练前会做一次只读的 DPCD 快照（约 89 字节 AUX 读，`TGLSinkStateTrace`）。

## 构建（WhateverXe 0.8.2 + `wex-0.8.2-xstation-v1.patch`）

0.8.2 使用上游 `tools/build.py`（固定 `MacKernelSDK` commit `05094e5e88cec7caedbfb35e8449ed0db94bf95b` 与 `Lilu` tag `1.7.2`）。在安装了 Xcode Command Line Tools 的 macOS 上直接执行：

```bash
./patches/WhateverXe/build-0.8.2.sh
```

脚本会自动检出 WhateverXe `0.8.2`、应用 `wex-0.8.2-xstation-v1.patch`、运行 `tools/test.py` 主机测试套件，并调用 `python3 tools/build.py --release` 产出：

- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/WhateverXe.kext`
- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/boot-stack.json`
- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/build-manifest.json`

若仅在 Linux 或 macOS 上运行主机单测与契约测试：

```bash
./patches/WhateverXe/build-0.8.2.sh --tests-only
```

## 旧版构建（WhateverXe 0.5.0 + `candidate-11.patch`）

```bash
git clone https://github.com/b00t0x/WhateverXe && cd WhateverXe
git checkout 0.5.0
git apply /path/to/candidate-11.patch
# 按 0.5.0 上游 README 说明在 macOS 上构建；测试：python3 tools/test.py
```

## 许可证

WhateverXe 源自 WhateverGreen，使用 BSD 3-Clause（见 `LICENSE-WhateverXe.txt`，© vit9696）。
本目录补丁同样以 BSD 3-Clause 发布，原文件中的版权声明均保留。
