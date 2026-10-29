#!/bin/bash
# 电池供电时每 N 秒读一次整机功耗。
# 本机电池按 mWh 汇报，SMCBatteryManager 把 _BST 的放电功率（mW）原样放进 Amperage/InstantAmperage，
# 所以这两个字段的绝对值就是功率（mW）；Voltage 恒为标称值，不能用来换算。
# 用法：bash battwatch.sh [次数=24] [间隔秒=5]
n=${1:-24}; dt=${2:-5}
for ((i=1; i<=n; i++)); do
    ioreg -rc AppleSmartBattery -w0 | python3 -c '
import re, sys, time
t = sys.stdin.read()
def g(k):
    m = re.search(r"\"%s\" = (\w+)" % k, t)
    return m.group(1) if m else None
def s64(v):
    v = int(v); return v - (1 << 64) if v >= 1 << 63 else v
avg, inst = -s64(g("Amperage")), -s64(g("InstantAmperage"))
print("%s ac=%s avg=%.2fW inst=%.2fW cap=%smWh" % (time.strftime("%H:%M:%S"), g("ExternalConnected"), avg / 1000, inst / 1000, g("CurrentCapacity")), flush=True)
'
    sleep "$dt"
done
