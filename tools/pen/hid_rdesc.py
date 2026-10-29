#!/usr/bin/env python3
"""简易 HID 报告描述符解析：按 Report ID 列出集合与字段（用途页/用途、位宽、逻辑范围）。
用法：python3 hid_rdesc.py rdesc.hex"""
import sys

PAGES = {0x01: 'GD', 0x09: 'Btn', 0x0D: 'Dig', 0x0C: 'Csm'}
DIG = {0x01: 'Digitizer', 0x02: 'Pen', 0x04: 'TouchScreen', 0x05: 'TouchPad', 0x0E: 'DevConfig', 0x20: 'Stylus',
       0x22: 'Finger', 0x30: 'TipPressure', 0x32: 'InRange', 0x3C: 'Invert', 0x3D: 'XTilt', 0x3E: 'YTilt',
       0x42: 'TipSwitch', 0x44: 'Barrel', 0x45: 'Eraser', 0x47: 'Confidence', 0x48: 'Width', 0x49: 'Height',
       0x51: 'ContactID', 0x54: 'ContactCount', 0x55: 'ContactMax', 0x56: 'ScanTime', 0x5A: 'SecondaryBarrel',
       0x5B: 'TransducerSerial', 0x41: 'Twist', 0x3B: 'BatteryStrength'}
GD = {0x01: 'Pointer', 0x02: 'Mouse', 0x30: 'X', 0x31: 'Y', 0x32: 'Z', 0x38: 'Wheel'}

def uname(page, u):
    t = {0x0D: DIG, 0x01: GD}.get(page, {})
    return f"{PAGES.get(page, hex(page))}:{t.get(u, hex(u))}"

def sval(data):
    n = int.from_bytes(data, 'little')
    if data and data[-1] & 0x80:
        n -= 1 << (8 * len(data))
    return n

d = bytes.fromhex(open(sys.argv[1]).read().strip())
i = 0; page = 0; usages = []; rid = 0; rsize = rcount = 0; lmin = lmax = 0; depth = 0
bits = {}
while i < len(d):
    b = d[i]; size = [0, 1, 2, 4][b & 3]; tag = b & 0xFC; data = d[i + 1:i + 1 + size]; i += 1 + size
    v = int.from_bytes(data, 'little')
    if tag == 0x04: page = v
    elif tag == 0x08: usages.append((v >> 16, v & 0xFFFF) if size == 4 else (page, v))
    elif tag == 0x84: rid = v; print(f"{'  ' * depth}-- Report ID {v} (0x{v:02x})")
    elif tag == 0x74: rsize = v
    elif tag == 0x94: rcount = v
    elif tag == 0x14: lmin = sval(data)
    elif tag == 0x24: lmax = sval(data)
    elif tag == 0xA0:
        u = usages[0] if usages else (page, 0)
        print(f"{'  ' * depth}Collection({['Phys','App','Logical','Report','NamedArr','UsageSw','UsageMod'][v] if v < 7 else v}) {uname(*u)}")
        depth += 1; usages = []
    elif tag == 0xC0: depth -= 1
    elif tag in (0x80, 0x90, 0xB0):
        kind = {0x80: 'In', 0x90: 'Out', 0xB0: 'Feat'}[tag]
        names = ','.join(uname(*u) for u in usages) or ('const' if v & 1 else '-')
        key = (kind, rid); off = bits.get(key, 0); bits[key] = off + rsize * rcount
        print(f"{'  ' * depth}{kind:4} rid={rid:<3} bit {off:4}+{rsize}x{rcount} [{lmin}..{lmax}] {names}{' const' if v & 1 else ''}")
        usages = []
    elif tag == 0x88: pass
