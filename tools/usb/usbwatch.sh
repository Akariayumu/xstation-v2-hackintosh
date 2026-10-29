#!/bin/bash
# 插拔映射用：每秒对比一次 IOUSB 树，打印新增(+)/移除(-)的设备。
# 位置 ID 前两位是控制器：14 = PCH XHCI，0d = TCSS TXHC；第三位（十六进制）是根端口号，
# 例如 14300000 = XHCI 端口 3（HS03），14d00000 = XHCI 端口 13（SS01）。spd：1 全速 2 高速 3 超速。
snap() {
    ioreg -p IOUSB -w0 -l | awk '
        /\+-o / { if (n) print n " spd=" s; n = $0; sub(/^[ |]*\+-o /, "", n); sub(/  <class.*/, "", n); s = "?" }
        /"Device Speed" =|"USBSpeed" =/ { s = $NF }
        END { if (n) print n " spd=" s }' | grep -v "Root Hub" | sort
}
prev=$(snap)
echo "$(date +%T) watching, $(echo "$prev" | wc -l | tr -d ' ') devices present"
while true; do
    sleep 1
    cur=$(snap)
    comm -13 <(echo "$prev") <(echo "$cur") | sed "s/^/$(date +%T) + /"
    comm -23 <(echo "$prev") <(echo "$cur") | sed "s/^/$(date +%T) - /"
    prev=$cur
done
