#!/bin/bash
# 以当前用户 GUI 会话的一次性 LaunchAgent 运行 hidsniff（这样"输入监控"权限归 hidsniff 本身）
# 用法：bash run-sniff.sh [秒数]   输出 /tmp/hidsniff.out
SEC=${1:-30}; L=org.xstation.hidsniff; P=/tmp/$L.plist; BIN="$HOME/xstation-pen/hidsniff"
launchctl bootout gui/$(id -u)/$L 2>/dev/null; sleep 0.5
cat > $P <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$L</string>
<key>ProgramArguments</key><array><string>$BIN</string><string>$SEC</string></array>
<key>RunAtLoad</key><true/>
<key>StandardOutPath</key><string>/tmp/hidsniff.out</string>
<key>StandardErrorPath</key><string>/tmp/hidsniff.out</string>
</dict></plist>
PL
: > /tmp/hidsniff.out
launchctl bootstrap gui/$(id -u) $P
