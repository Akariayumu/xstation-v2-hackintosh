# 短按电源键弹出"关机/取消/关闭屏幕/重启"面板

依赖 EFI v0.1.50 起的电源键 EC 修复（`docs/research/power-button-20261026.md`）。

- `PowerKeyDialog.swift`：轮询 PWRB/AppleACPIButton 的 HID 事件计数，有按键就弹出四按钮面板。
  关机/重启发给 loginwindow（`shut`/`rest`），和系统面板一样会先让应用正常退出；
  "关闭屏幕"执行 `pmset displaysleepnow`（代替系统睡眠，本机睡眠未验证）。
  屏幕关着时短按电源键只点亮屏幕（`caffeinate -u`），不弹面板。
  按住 Control 时不处理，交给系统弹它自己的面板。不需要任何隐私权限。
- `install.sh`：编译并安装为当前用户的 LaunchAgent `org.xstation.powerkeydialog`，登录后自动运行。

```bash
bash install.sh          # 安装/更新
bash install.sh remove   # 卸载
```

日志：`/tmp/org.xstation.powerkeydialog.log`

为什么不用系统自带的面板：AppleScript 只能调出带 60 秒倒计时的关机确认框（`aevtrsdn`）；
模拟的 Control+电源键（NX_POWER_KEY 系统事件）系统不响应，2026-10-28 已实测。

2026-10-29 实测：面板关屏 → 短按电源键点亮（不弹面板）；关屏 → 键盘/触控板/触摸屏点亮，均正常，无新 panic。
日志 `work/reports/powerkey-agent-test-20261029.log`。
