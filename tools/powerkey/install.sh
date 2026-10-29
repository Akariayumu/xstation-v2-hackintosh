#!/bin/bash
# 安装/卸载"短按电源键弹关机框"启动项（当前用户 LaunchAgent）
# 用法：bash install.sh          安装并启动
#       bash install.sh remove   卸载
set -e
LABEL=org.xstation.powerkeydialog
DIR="$HOME/Library/Application Support/XStation"
BIN="$DIR/PowerKeyDialog"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
SRC="$(cd "$(dirname "$0")" && pwd)/PowerKeyDialog.swift"

launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
# bootout 是异步的，等旧实例完全卸载后再 bootstrap，否则会报 125
for _ in 1 2 3 4 5 6 7 8 9 10; do
    launchctl print "gui/$(id -u)/$LABEL" >/dev/null 2>&1 || break
    sleep 0.5
done
if [ "${1:-}" = remove ]; then
    rm -f "$PLIST" "$BIN"
    echo "removed"
    exit 0
fi

mkdir -p "$DIR" "$HOME/Library/LaunchAgents"
swiftc -O "$SRC" -o "$BIN"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$LABEL</string>
    <key>ProgramArguments</key><array><string>$BIN</string></array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>ProcessType</key><string>Background</string>
    <key>StandardErrorPath</key><string>/tmp/$LABEL.log</string>
</dict>
</plist>
EOF
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "installed: $PLIST"
