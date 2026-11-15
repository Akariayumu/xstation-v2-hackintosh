# WhateverXe 补丁（X-STATION V2）

本目录维护两份针对上游 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 的 X-STATION V2（i7-1165G7 B0 + 4K eDP）补丁：

| 补丁文件 | 目标上游版本 | 用途 |
|---|---|---|
| `candidate-11.patch` | **0.5.0**（tag `0.5.0`，commit `c6cd8ec`） | 当前仓库 `EFI/OC/Kexts/WhateverXe.kext`（v0.1.55 生产基线）使用，保留作即时回退锚点 |
| `wex-0.8.2-xstation-v1.patch` | **0.8.2**（tag `0.8.2` `4512191b` / `main` `2bad7171`） | Phase 0 / Phase 1 S0ix 准备分支：把 `candidate-11.patch` 无损迁移到 0.8.2（含 Render `FORCEWAKE` 成对平衡、GuC 休眠与全 TU 内核栈预算适配） |

## 启动参数

| 启动参数 | 作用 | 默认 |
|---|---|---|
| `-wex-tgl-sdt-guard` | 显示时序保护：Apple 驱动算出的 eDP 时序无效（全零未写入导致除零 panic）时，用本次已校验的输入补齐；并导出 `TGLPowerFuseLive` 轮询记录 | 关，需显式打开 |
| `-wex-no-panel-keep` | 关屏时不强制面板保持供电（允许 `PP_CONTROL` 正常断电进入深层省电） | 关 |
| `-wex-sink-rate-restore` | 开机第一次快照记下面板的 `LINK_RATE_SET`（DPCD `0x115`），之后每次链路训练前如果不同就写回（最多重试 3 次，每次间隔 1 ms） | 关 |

`-wex-no-panel-keep -wex-sink-rate-restore` 一起用解决“关屏后面板断电，再点亮时链路训练失败、黑屏”：
面板断电后会丢掉 BIOS 设置的 `0x115=0x06`（5.4 Gbps HBR2），源端按异常速率训练就失败。

补丁里还有候选 8–10 的实验开关（`-wex-sink-d0`、`-wex-sink-oui`、`-wex-sink-rate-select`）和诊断开关 `-wex-display-trace`，默认均关闭。
每次训练前会做一次只读的 DPCD 快照（约 89 字节 AUX 读，`TGLSinkStateTrace`）。

## 构建（WhateverXe 0.8.2 + `wex-0.8.2-xstation-v1.patch`）

0.8.2 使用上游 `tools/build.py`（固定 `MacKernelSDK` commit `05094e5e88cec7caedbfb35e8449ed0db94bf95b` 与 `Lilu` tag `1.7.2`）。在安装了 Xcode Command Line Tools 的 macOS 上直接执行：

```bash
./patches/WhateverXe/build-0.8.2.sh
```

脚本会自动检出 WhateverXe `0.8.2`、应用 `wex-0.8.2-xstation-v1.patch`、运行 `tools/test.py` 主机测试套件，并调用 `python3 tools/build.py --release` 产出：

- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/WhateverXe.kext`
- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/boot-stack.json`
- `third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/build-manifest.json`

产出后可通过 `tools/s0ix/stage_usb_efi.py` 生成隔离的 U 盘测试 EFI（自动强制 `WriteFlash = False`，不修改仓库生产 `EFI/OC/config.plist`）：

```bash
python3 tools/s0ix/stage_usb_efi.py \
  --phase 0 \
  --wex-kext third_party/wex-0.8.2-build/WhateverXe/build/RELEASE/WhateverXe.kext \
  --out /tmp/xstation-usb-phase0
```

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
