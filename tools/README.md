# macOS 小工具

都在目标机的 macOS 下运行，不需要放进 EFI。

| 目录 | 用途 |
|---|---|
| `powerkey/` | 短按电源键弹出“关机 / 重启 / 关闭屏幕 / 取消”面板（需要 v0.1.50 起的 SSDT-PWRBEC）。`bash install.sh` 安装为当前用户的 LaunchAgent |
| `usb/usbwatch.sh` | 制作 USB 映射时用：每秒对比一次 IOUSB 树，打印插入和拔出的端口 |
| `pen/` | 抓取手写笔 / 触摸的原始 HID 报告（`hidsniff.swift`），并解析报告描述符（`hid_rdesc.py`） |
| `power/battwatch.sh` | 用电池供电时，每隔几秒读一次整机功耗（mW） |
